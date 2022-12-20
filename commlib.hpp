#ifndef COMMLIB_HPP
#define COMMLIB_HPP

#include "manager.hpp"
#include "protocols/tcp.hpp"

#ifndef EXCLUDE_MPI
#include "mpi.h"
#include "protocols/mpi.hpp"
#include "protocols/mpip2p.hpp"
#endif

#ifdef MQTT
#include "protocols/mqtt.hpp"
#endif

#endif