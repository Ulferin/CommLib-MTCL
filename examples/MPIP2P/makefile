CXX 		= mpicxx
CXXFLAGS 	= -g -Wall -std=c++17

LDLIBS 		= -pthread

LIB_HOME 	= ../../

TARGETS = mpi_p2p_ping.out mpi_p2p_pong.out drop_connection.out try_connect.out
DEPS = stop_accept.out $(LIB_HOME)manager.hpp $(LIB_HOME)protocolInterface.hpp $(LIB_HOME)handleUser.hpp $(LIB_HOME)handle.hpp $(LIB_HOME)protocols/tcp.hpp $(LIB_HOME)protocols/mpi.hpp $(LIB_HOME)protocols/mpip2p.hpp

.PHONY = all clean cleanall

all: $(TARGETS)

ping_pong: mpi_p2p_ping.out mpi_p2p_pong.out
drop_conn: drop_connection.out try_connect.out

stop_accept.out: $(LIB_HOME)protocols/stop_accept.cpp
	mpicxx $(CXXFLAGS) $< -o $@ $(LDFLAGS)

%.out: %.cpp $(DEPS)
	mpicxx $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)

cleanall: clean
	rm -f ./*.out ./*.o ./*~
