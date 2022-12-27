#ifndef CONFIG_HPP
#define CONFIG_HPP


// -------------- some configuration parameters ----------------

const int IO_THREAD_POLLING_TIMEOUT= 10;   // milliseconds

// ------ TCP ------
const int TCP_BACKLOG              = 128;
const int TCP_POLL_TIMEOUT         = 10;   // microseconds

// ------ MPI ------
const int MPI_CONNECTION_TAG       = 0;
const int MPI_DISCONNECT_TAG       = 1;

// ------ MPIP2P ------
const int MPIP2P_POLL_TIMEOUT      = 10;   // microseconds
const int MPIP2P_DISCONNECT_TAG    = 42;   // <----??????
const char MPIP2P_STOP_PROCESS[]   = "stop_accept";
const char MPIP2P_PUBLISH_NAME[]   = "test_server";

// ------- MQTT -----
const int MQTT_POLL_TIMEOUT    = 10;   // milliseconds
const int MQTT_CONNECT_TIMEOUT = 100;  // milliseconds    // <----- ??????
const std::string MQTT_OUT_SUFFIX{"-out"};
const std::string MQTT_IN_SUFFIX{"-in"};
const std::string MQTT_MANAGER_PSWD{"manager_passwd"};
const std::string MQTT_CONNECTION_TOPIC{"-new_connection"};
const std::string MQTT_EXIT_TOPIC {"-exit"};
const std::string MQTT_SERVER_ADDRESS{ "tcp://localhost:1883" };   // <--- ?????


// ------- UCX ------



#endif 
