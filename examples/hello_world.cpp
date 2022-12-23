/*
 * Simple "hello world" example client-server example.
 * To run the server:  ./hello_world 0
 * To run the client:  ./hello_world 1
 * 
 * Testing TCP:
 * ^^^^^^^^^^^^
 *   $> make cleanall all
 *   $> ./hello_world 0 &
 *   $> mpirun -n 4 ./hello_world 1
 *   $> pkill -HUP hello_world
 *
 * Testing (plain) MPI:
 * ^^^^^^^^^^^^^^^^^^^^
 *   $> TPROTOCOL=MPI make cleanall all
 *   $> mpirun -n 1 ./hello_world 0 : -n 3 ./hello_world 1 &
 *   $> sleep 3; pkill -HUP ./hello_world
 *
 * MPIP2P: (va compilato stop_accept ?????? da rivedere)
 *  /home/massimo/DistributedFF/ompi-install/bin/ompi-server --report-uri uri_file.txt --no-daemonize
 *  funziona se piu' client si lanciano con diverse mpirun
 *
 * MQTT:
 *   https://github.com/eclipse/paho.mqtt.c   (API)
 *   https://test.mosquitto.org   (BROKER)
 *
 *
 */

#include <csignal>
#include <cassert>
#include <iostream>
#include "commlib.hpp"

using namespace std::chrono_literals;

std::string welcome{"Hello!"}; // welcome message
std::string bye{"Bye!"};       // bye bye message
const int max_msg_size=100;    // max size
// for a gentle exit 
static volatile sig_atomic_t stop=0;
void signal_handler(int) { stop=1; }

// It waits for new connections, sends a welcome message to the connected client,
// then echoes the input message to the client up to the bye message.
void Server() {	
	Manager::listen("TCP:0.0.0.0:42000");
	//Manager::listen("MPI:0:10");
	//Manager::listen("MPIP2P:test");

	char buff[max_msg_size+1];
	while(!stop) {
		// Is there something ready?
		auto handle = Manager::getNext(300ms);
		if (!handle.isValid()) continue; // timeout expires

		// Yes. Is it a new connection?
		if(handle.isNewConnection()) {
			// yes it is, sending the welcome message
			handle.send(welcome.c_str(), welcome.length());
			continue;
		}
		// it is not a new connection, sending back the message to the client.
		// We read first the message size (an int) and then the payload.
		int r=0;
		int size=0;
		if ((r=handle.receive(&size, sizeof(size)))==0) {
			std::cerr << "The client unexpectedly closed the connection. Bye! (1)\n";
			handle.close();
			continue;
		}
		assert(size<max_msg_size); // check
		if ((r=handle.receive(buff, size))==0) {
			std::cerr << "The client unexpectedly closed the connection. Bye! (2)\n";
			handle.close();
			continue;
		}
		buff[size]='\0';
		if (std::string(buff) == bye) {
			std::cout << "The client sent the bye message! Goodbye!\n";
			handle.close();
			continue;
		}		
		if (handle.send(buff, r)<=0) {
			std::cerr << "Error sending the message back to the client, close handle\n";
			handle.close();
		}
	}
	std::cout << "server exit!\n";
}
// It connects to the server waiting for the welcome message. Then it sends the string
// "ciao" incrementally to the server, receiving each message back from the server.
void Client() {
	char buff[10];
	auto handle = Manager::connect("TCP:0.0.0.0:42000");
	//auto handle = Manager::connect("MPI:0:10");
	//auto handle = Manager::connect("MPIP2P:test");
	do {
		if(handle.isValid()) {
			// wait for the welcome message
			if(handle.receive(buff, welcome.length())<=0) break;
			buff[0]='c'; buff[1]='i'; buff[2]='a';
			buff[3]='o'; buff[4]='!';buff[5]='\0';
			
			// now sending the string "ciao" incrementally
			for(int i=1;i<=5;++i) {
				if (handle.send(&i, sizeof(int))<=0) break;
				if (handle.send(buff, i)<=0) break;
				char rbuf[i+1];
				if (handle.receive(rbuf, i)<=0) break;
				rbuf[i]='\0';
				std::cout << "Read: \"" << rbuf << "\"\n";
			}
			// we can just close the handle here, but we are polite and say Bye!
			int r = bye.length();
			if (handle.send(&r, r)<=0) break;
			if (handle.send(bye.c_str(), r)<=0) break;
		}
	} while(false);
	handle.close();
}

int main(int argc, char** argv){
    if(argc < 2) {
		std::cerr << "Usage:" << argv[0] << " <0|1>\n";
        return -1;
    }
	std::signal(SIGHUP,  signal_handler);
	std::signal(SIGINT,  signal_handler);
	std::signal(SIGTERM, signal_handler);
	std::signal(SIGQUIT, signal_handler);

    Manager::init();   
    if (std::stol(argv[1]) == 0)
		Server();            
    else
		Client();	
    Manager::finalize();
	
    return 0;
}
