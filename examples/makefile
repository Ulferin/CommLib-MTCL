CXX = g++-10
CXXFLAGS = -g -Wall -std=c++17

LDLIBS 			= -pthread

DEFINE = -D
ifdef PROTOCOL
	DEFINE+=PROT_$(PROTOCOL)
else
	DEFINE+=PROT_TCP
endif


ifeq ($(findstring MPI,$(PROTOCOL)),MPI)
CXX = mpicxx
endif

ifeq ($(PROTOCOL), MQTT)
LDLIBS = -lpthread -lpaho-mqtt3a -lpaho-mqttpp3
endif


TARGETS = pingpong
DEPS = ../manager.hpp ../protocolInterface.hpp ../handleUser.hpp ../handle.hpp ../protocols/*

.PHONY = all clean cleanall

all: $(TARGETS)

pingpong: pingpong.cpp $(DEPS)
ifeq ($(CXX), mpicxx)
	mpicxx $(CXXFLAGS) ../protocols/stop_accept.cpp -o stop_accept.out $(LDFLAGS) $(DEFINE)
endif
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS) $(DEFINE)

%.out: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)

cleanall: clean
	rm -f ./*.out ./*.o ./*~
