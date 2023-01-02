#ifndef CONFIG_HPP
#define CONFIG_HPP


// -------------- some configuration parameters ----------------

// all timeouts are in microseconds
const unsigned IO_THREAD_POLL_TIMEOUT  = 10; 

// ------ TCP ------
const unsigned TCP_BACKLOG             = 128;
const unsigned TCP_POLL_TIMEOUT        = 10; 

// ------ MPI ------
const unsigned MPI_POLL_TIMEOUT        = 10; 
const unsigned MPI_CONNECTION_TAG      = 0;
const unsigned MPI_DISCONNECT_TAG      = 1;

// ------ MPIP2P ------
const unsigned MPIP2P_POLL_TIMEOUT     = 10; 
const unsigned MPIP2P_DISCONNECT_TAG   = 42;   // <----??????
const char MPIP2P_STOP_PROCESS[]   = "stop_accept";
const char MPIP2P_PUBLISH_NAME[]   = "test_server";

// ------- MQTT -----
const unsigned MQTT_POLL_TIMEOUT       = 10;   // milliseconds
const unsigned MQTT_CONNECT_TIMEOUT    = 100;  // milliseconds  // <----- ??????
const std::string MQTT_OUT_SUFFIX{"-out"};
const std::string MQTT_IN_SUFFIX{"-in"};
const std::string MQTT_MANAGER_PSWD{"manager_passwd"};
const std::string MQTT_CONNECTION_TOPIC{"-new_connection"};
const std::string MQTT_EXIT_TOPIC {"-exit"};
const std::string MQTT_SERVER_ADDRESS{ "tcp://localhost:1883" };// <--- ?????


// ------- UCX ------



#endif 
