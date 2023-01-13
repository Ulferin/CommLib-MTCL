ifndef CXX
CXX 	   = g++
endif
CXXFLAGS  += -std=c++17
INCS       = -I ..
ifdef DEBUG
	OPTIMIZE_FLAGS  += -g -fno-inline-functions
else
	OPTIMIZE_FLAGS  += -O3 -finline-functions -DNDEBUG
endif

ifdef SINGLE_IO_THREAD
	CXXFLAGS +=-DSINGLE_IO_THREAD
endif

ifeq ($(findstring MPI,$(TPROTOCOL)),MPI)
	CXX 	  = mpicxx
	CXXFLAGS += -DENABLE_MPI
	TARGET    = ../protocols/stop_accept
ifdef MPI_HOME
	INCS   += `pkg-config --cflags-only-I $(MPI_HOME)/lib/pkgconfig/ompi-cxx.pc`
	LIBS   += `pkg-config --libs $(MPI_HOME)/lib/pkgconfig/ompi-cxx.pc`
endif
endif

ifeq ($(findstring MQTT, $(TPROTOCOL)),MQTT)
	CXXFLAGS += -DENABLE_MQTT
ifdef PAHO_HOME
	INCS += -I${PAHO_HOME}/include
	LIBS += -L${PAHO_HOME}/lib
endif
	LIBS     += -Wl,-rpath,${PAHO_HOME}/lib -lpaho-mqttpp3 -lpaho-mqtt3as -lpaho-mqtt3a
endif

ifeq ($(findstring UCX, $(TPROTOCOL)),UCX)
	CXXFLAGS += -DENABLE_UCX
ifdef UCX_HOME
	INCS += -I$(UCX_HOME)/include
	LIBS += -L$(UCX_HOME)/lib
endif
	LIBS += -Wl,-rpath,${UCX_HOME}/lib -lucp -luct -lucs -lucm
endif

CXXFLAGS         += -Wall
LIBS             += -pthread -lrt
INCLUDES          = $(INCS)

SOURCES           = $(wildcard *.cpp)
TARGET           += $(SOURCES:.cpp=)

.PHONY: all clean cleanall 
.SUFFIXES: .c .cpp .o

%.d: %.cpp
	@set -e; $(CXX) -MM $(INCLUDES) $(CXXFLAGS) $< \
		| sed 's/\($*\)\.o[ :]*/\1 $@ : /g' > $@; \
		[ -s $@ ] || rm -f $@
%.d: %.c
	@set -e; $(CC) -MM $(INCLUDES) $(CFLAGS)  $< \
		| sed 's/\($*\)\.o[ :]*/\1 $@ : /g' > $@; \
		[ -s $@ ] || rm -f $@
%.o: %.c
	$(CC) $(INCLUDES) $(CFLAGS) -c -o $@ $<
%: %.cpp
	$(CXX) $(INCLUDES) $(CXXFLAGS) $(OPTIMIZE_FLAGS) -o $@ $< $(LDFLAGS) $(LIBS)

all: $(TARGET)

clean: 
	-rm -fr $(TARGET) *~
cleanall: clean
	-rm -fr *.d uri_file.txt ../protocols/stop_accept

include $(SOURCES:.cpp=.d)