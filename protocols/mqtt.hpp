#ifndef MPIP2P_HPP
#define MPIP2P_HPP

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <vector>
#include <queue>
#include <map>
#include <shared_mutex>
#include <thread>
#include <pthread.h>
#include <atomic>
#include <signal.h>

#include "mqtt/client.h"

#include "../handle.hpp"
#include "../protocolInterface.hpp"

#define MQTT_SLEEP 100

const std::string OUT_SUFFIX {"-out"};
const std::string IN_SUFFIX {"-in"};
const std::string USER_SUFFIX {"-user"};

const std::string MANAGER_PSWD {"manager_passwd"};

const std::string CONNECTION_TOPIC {"-new_connection"};

const std::string SERVER_ADDRESS { "tcp://localhost:1883" };



class HandleMQTT : public Handle {

public:
    bool closing = false;
    mqtt::client* client;
    mqtt::string out_topic, in_topic;
    std::queue<std::pair<std::string, ssize_t>> messages;


    HandleMQTT(ConnType* parent, mqtt::client* client,
        mqtt::string out_topic, mqtt::string in_topic, bool busy=true) :
            Handle(parent, busy), client(client),
            out_topic(out_topic), in_topic(in_topic) {}

    ssize_t send(const char* buff, size_t size) {

        /*TODO: check su closing prima o dopo c'è sempre il problema che potremmo
                fare una send */
        if(closing) {
            errno = ECONNRESET;
            return -1;
        }

        client->publish(out_topic, buff, size);

        return size;
    }

    ssize_t receive(char* buff, size_t size){

        while(true) {
            if(!messages.empty()) {
                auto msg = messages.front();
                strncpy(buff, msg.first.c_str(), msg.second+1);
                messages.pop();
                return msg.second;
            }

            mqtt::const_message_ptr msg;
            bool res = client->try_consume_message(&msg);

            // A message has been received (either on "in_topic" or "in_topic+exit")
            if(res) {
                if(msg->get_topic() == in_topic+"exit") {
                    errno = ECONNRESET;
                    closing = true;
                    return 0;
                }
                
                // This topic should have been subscribed upon creation of the
                // handle object
                if(msg->get_topic() == in_topic) {
                    /*TODO: overflow??? Se i messaggi nel broker sono FIFO non
                            dovrebbero esserci problemi */
                    strncpy(buff, msg->get_payload().c_str(), size);
                    return size;
                }
            }

            if(closing) {
                printf("[MQTT] Closing connection\n");
                return 0;
            }

            std::this_thread::sleep_for(std::chrono::microseconds(MQTT_SLEEP));

        }
        
        return 0;
    }


    ~HandleMQTT() {}

};


class ConnMQTT : public ConnType {
private:
    std::string manager_name, new_connection_topic;
    // enum class ConnEvent {close, yield};

    void createClient(mqtt::string topic, mqtt::client *aux_cli) {
        auto aux_connOpts = mqtt::connect_options_builder()
            .user_name(topic)
            .password("passwd")
            .keep_alive_interval(std::chrono::seconds(30))
            .automatic_reconnect(std::chrono::seconds(2), std::chrono::seconds(30))
            .clean_session(false)
            .finalize();

        mqtt::connect_response rsp = aux_cli->connect(aux_connOpts);

        // "listening" on topic-in
        if (!rsp.is_session_present()) {
            aux_cli->subscribe({topic, topic+"exit"}, {0, 0});
        }
        else {
            std::cout << "Session already present. Skipping subscribe.\n";
        }

    }

protected:
    
    // TODO: clean this up when closing
    mqtt::client *newConnClient;

    std::atomic<bool> finalized = false;
    bool listening = false;
    
    std::map<HandleMQTT*, bool> connections;  // Active connections for this Connector
    std::shared_mutex shm;

public:

   ConnMQTT(){};
   ~ConnMQTT(){};

    int init() {    
        return 0;
    }

    int listen(std::string s) {
        manager_name = s.substr(s.find(":")+1, s.length());
        new_connection_topic = manager_name + CONNECTION_TOPIC;
        
        newConnClient = new mqtt::client(SERVER_ADDRESS, manager_name);
        auto connOpts = mqtt::connect_options_builder()
            .keep_alive_interval(std::chrono::seconds(30))
            .automatic_reconnect(std::chrono::seconds(2), std::chrono::seconds(30))
            .clean_session(true)
            .finalize();


		mqtt::connect_response rsp = newConnClient->connect(connOpts);
        if (!rsp.is_session_present()) {
            // TODO: check the QoS value
			newConnClient->subscribe({new_connection_topic}, {0});
		}
		else {
			std::cout << "Session already present. Skipping subscribe." << std::endl;
		}
        
        return 0;
    }

    void update() {
        std::unique_lock ulock(shm, std::defer_lock);

        // Consume messages
        mqtt::const_message_ptr msg;
        bool res = newConnClient->try_consume_message(&msg);
        (void)res;

        if (msg) {
            // New message on new connection topic, we create new client and
            // connect it to the topic specified on the message + predefined suffix
            if(msg->get_topic() == new_connection_topic) {
                
                mqtt::client* aux_cli =
                    new mqtt::client(SERVER_ADDRESS,
                        msg->to_string().append(USER_SUFFIX).append(IN_SUFFIX));
                createClient(msg->to_string().append(IN_SUFFIX), aux_cli);

                // Must send the ACK to the remote end otherwise I can miss some
                // messages
                auto pubmsg = mqtt::make_message(msg->to_string().append(OUT_SUFFIX), "ack");
                pubmsg->set_qos(1);
                aux_cli->publish(pubmsg);

                HandleMQTT* handle = new HandleMQTT(this, aux_cli,
                    msg->to_string().append(OUT_SUFFIX),
                    msg->to_string().append(IN_SUFFIX));

                ulock.lock();
                connections.insert({handle, false});
                addinQ({true, handle});
                ulock.unlock();      
            }
        }
        else if (!newConnClient->is_connected()) {
            std::cout << "Lost connection" << std::endl;
            while (!newConnClient->is_connected()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            std::cout << "Re-established connection" << std::endl;
        }

        for (auto &[handle, to_manage] : connections) {
            if(to_manage) {
                mqtt::const_message_ptr msg;
                bool res = handle->client->try_consume_message(&msg);
                if(res) {
                    if(msg->get_topic() == handle->out_topic+"exit") {
                        handle->closing = true;
                    }
                    else {
                        handle->messages.push({msg->get_payload(), msg->get_payload().length()});
                    }
                        to_manage = false;
                        addinQ({false, handle});
                }
            }
        }
        

        return;
    }


    // String for connection composed of manager_id:topic
    Handle* connect(const std::string& address) {
        std::string manager_id = address.substr(0, address.find(":"));
        std::string topic = address.substr(manager_id.length()+1, address.length());
        mqtt::string topic_out = topic+OUT_SUFFIX;
        mqtt::string topic_in = topic+IN_SUFFIX;

        mqtt::client *client = new mqtt::client(SERVER_ADDRESS, address+(USER_SUFFIX)+(OUT_SUFFIX));
        mqtt::connect_options connOpts;
	    connOpts.set_keep_alive_interval(20);
        connOpts.set_clean_session(true);

        client->connect(connOpts);
        client->subscribe({topic_out, topic_out+"exit"}, {0,0});

		auto pubmsg = mqtt::make_message(manager_id + CONNECTION_TOPIC, topic);
		pubmsg->set_qos(1);
		client->publish(pubmsg);

        // Waiting for ack before proceeding
        auto msg = client->consume_message();

        // we write on topic + IN_SUFFIX and we read from topic + OUT_SUFFIX
        HandleMQTT* handle = new HandleMQTT(this, client, topic_in, topic_out, true);

        std::unique_lock lock(shm);
        connections[handle] = false;
        
        return handle;
    }


    void notify_close(Handle* h) {
        std::unique_lock l(shm);
        HandleMQTT* handle = reinterpret_cast<HandleMQTT*>(h);
        if (!handle->closing){
            std::string aux("");
            handle->client->publish(mqtt::make_message(handle->out_topic+"exit", aux));
            handle->client->disconnect();
        }
        connections.erase(reinterpret_cast<HandleMQTT*>(h));
        return;
    }


    void notify_yield(Handle* h) override {
        std::unique_lock l(shm);
        connections[reinterpret_cast<HandleMQTT*>(h)] = true;
    }

    void end() {
        auto modified_connections = connections;
        for(auto& [handle, to_manage] : modified_connections)
            if(to_manage)
                setAsClosed(handle);
    }

};

#endif