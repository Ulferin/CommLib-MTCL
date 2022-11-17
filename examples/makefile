CXX = g++-10
CXXFLAGS = -g -Wall -std=c++20

LDLIBS 			= -pthread

TARGETS = notify_on_connection.out
DEPS = ../manager.hpp ../protocolInterface.hpp ../handleUser.hpp ../handle.hpp ../protocols/tcp.hpp ../protocols/mpi.hpp

.PHONY = all clean cleanall

all: $(TARGETS)

notify_on_connection.out: notify_on_connection.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

%.out: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)

cleanall: clean
	rm -f ./*.out ./*.o ./*~
