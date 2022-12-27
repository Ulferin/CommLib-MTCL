#ifndef MQTT_HPP
#define MQTT_HPP

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
#include <atomic>

#include "mqtt/client.h"

#include "../handle.hpp"
#include "../protocolInterface.hpp"


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

    ssize_t send(const void* buff, size_t size) {
        if(closing) {
            errno = ECONNRESET;
            return -1;
        }

        client->publish(out_topic, buff, size);

        return size;
    }

    ssize_t receive(void* buff, size_t size){

        while(true) {
            if(!messages.empty()) {
                auto msg = messages.front();
                strncpy((char*)buff, msg.first.c_str(), msg.second+1);
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
                    strncpy((char*)buff, msg->get_payload().c_str(), size);
                    return size;
                }
            }

            if(closing) {
                MTCL_MQTT_PRINT("receive: closing connection\n");
                return 0;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(MQTT_POLL_TIMEOUT));

        }
        
        return 0;
    }


    ~HandleMQTT() {
        delete client;
    }

};


class ConnMQTT : public ConnType {
private:
    std::string manager_name, new_connection_topic;
    std::string appName;
    size_t count = 0;
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
			MTCL_MQTT_PRINT("createClient: session already present. Skipping subscribe.\n");
        }

    }

protected:
    
    mqtt::client *newConnClient;

    std::atomic<bool> finalized = false;
    bool listening = false;
    
    std::map<HandleMQTT*, bool> connections;  // Active connections for this Connector
    REMOVE_CODE_IF(std::shared_mutex shm);

public:

   ConnMQTT(){};
   ~ConnMQTT(){};

    int init(std::string s) {
        appName = s;
        newConnClient = new mqtt::client(MQTT_SERVER_ADDRESS, appName);
        auto connOpts = mqtt::connect_options_builder()
            .keep_alive_interval(std::chrono::seconds(30))
            .automatic_reconnect(std::chrono::seconds(2), std::chrono::seconds(30))
            .clean_session(true)
            .finalize();

		mqtt::connect_response rsp = newConnClient->connect(connOpts);
        if(rsp.is_session_present()) {
			MTCL_MQTT_ERROR("Session already present. Use a different ID for the MQTT client\n");
            return -1;
        }
		
        return 0;
    }

    int listen(std::string s) {
        manager_name = s.substr(s.find(":")+1, s.length());
        new_connection_topic = manager_name + MQTT_CONNECTION_TOPIC;

		MTCL_MQTT_PRINT("listening on the connection topic: %s\n", new_connection_topic.c_str());
        
        newConnClient->subscribe({new_connection_topic}, {0});
        
        listening = true;
        return 0;
    }

    void update() {
        if(!listening) return;

        REMOVE_CODE_IF(std::unique_lock ulock(shm, std::defer_lock));

        // Consume messages
        mqtt::const_message_ptr msg;
        bool res = newConnClient->try_consume_message(&msg);
        (void)res;

        if (msg) {
            // New message on new connection topic, we create new client and
            // connect it to the topic specified on the message + predefined suffix
            if(msg->get_topic() == new_connection_topic) {
                
                mqtt::client* aux_cli =
                    new mqtt::client(MQTT_SERVER_ADDRESS,
                        msg->to_string().append(MQTT_USER_SUFFIX).append(MQTT_IN_SUFFIX));
                createClient(msg->to_string().append(MQTT_IN_SUFFIX), aux_cli);

                // Must send the ACK to the remote end otherwise I can miss some
                // messages
                auto pubmsg = mqtt::make_message(msg->to_string().append(MQTT_OUT_SUFFIX), "ack");
                pubmsg->set_qos(1);
                aux_cli->publish(pubmsg);

                HandleMQTT* handle = new HandleMQTT(this, aux_cli,
                    msg->to_string().append(MQTT_OUT_SUFFIX),
                    msg->to_string().append(MQTT_IN_SUFFIX));

                REMOVE_CODE_IF(ulock.lock());
                connections.insert({handle, false});
                addinQ({true, handle});
                REMOVE_CODE_IF(ulock.unlock());
            }
        }
        else {
			if (!newConnClient->is_connected()) {
				MTCL_MQTT_PRINT("update: lost connection, waiting a while for reconnecting\n");
				std::this_thread::sleep_for(std::chrono::milliseconds(MQTT_CONNECT_TIMEOUT));
				if (!newConnClient->is_connected()) {
					MTCL_MQTT_PRINT("update: no connection yet, keep going...\n");
					return;
				}
				MTCL_MQTT_PRINT("update: re-established connection\n");
			}
		}
		
		REMOVE_CODE_IF(ulock.lock());
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
					// NOTE: called with ulock lock hold. Double lock if there is the IO-thread!
					addinQ({false, handle});
                }
            }
        }
		REMOVE_CODE_IF(ulock.unlock());        
    }


    // String for connection composed of manager_id:topic
    Handle* connect(const std::string& address) {
        std::string manager_id = address.substr(0, address.find(":"));
        std::string topic = appName + std::to_string(count++);
        mqtt::string topic_out = topic+MQTT_OUT_SUFFIX;
        mqtt::string topic_in = topic+MQTT_IN_SUFFIX;

        mqtt::client *client = new mqtt::client(MQTT_SERVER_ADDRESS, topic);
        mqtt::connect_options connOpts;
	    connOpts.set_keep_alive_interval(20);
        connOpts.set_clean_session(true);

        client->connect(connOpts);
        client->subscribe({topic_out, topic_out+"exit"}, {0,0});

		MTCL_MQTT_PRINT("connecting to: %s\n", (manager_id+MQTT_CONNECTION_TOPIC).c_str());
		
		auto pubmsg = mqtt::make_message(manager_id + MQTT_CONNECTION_TOPIC, topic);
		pubmsg->set_qos(1);
		client->publish(pubmsg);

        // Waiting for ack before proceeding
        auto msg = client->consume_message();

        // we write on topic + MQTT_IN_SUFFIX and we read from topic + MQTT_OUT_SUFFIX
        HandleMQTT* handle = new HandleMQTT(this, client, topic_in, topic_out, true);
		{
			REMOVE_CODE_IF(std::unique_lock lock(shm));
			connections[handle] = false;
        }
        return handle;
    }


    void notify_close(Handle* h) {
        REMOVE_CODE_IF(std::unique_lock l(shm));
        HandleMQTT* handle = reinterpret_cast<HandleMQTT*>(h);
        if (!handle->closing){
            std::string aux("");
            handle->client->publish(mqtt::make_message(handle->out_topic+"exit", aux));
            handle->client->disconnect();
        }
        connections.erase(reinterpret_cast<HandleMQTT*>(h));
    }


    void notify_yield(Handle* h) override {
        REMOVE_CODE_IF(std::unique_lock l(shm));
        connections[reinterpret_cast<HandleMQTT*>(h)] = true;
    }

    void end() {
        auto modified_connections = connections;
        for(auto& [handle, to_manage] : modified_connections)
            if(to_manage)
                setAsClosed(handle);

        delete newConnClient;
    }

};

#endif
