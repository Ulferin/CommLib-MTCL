CXX 	   = g++
CXXFLAGS  += -std=c++17
INCS       = -I ..
ifdef DEBUG
    OPTIMIZE_FLAGS  += -g -fno-inline-functions
else
    OPTIMIZE_FLAGS  += -O3 -finline-functions -DNDEBUG
endif

ifdef TPROTOCOL
	TPROTO +=-DENABLE_${TPROTOCOL}
endif

ifeq ($(findstring MPI,$(TPROTOCOL)),MPI)
	CXX 	  = mpicxx
	CXXFLAGS += $(TPROTO)
	TARGET    = ../protocols/stop_accept
ifdef MPI_HOME
	INCS   += `pkg-config --cflags-only-I $(MPI_HOME)/lib/pkgconfig/ompi-cxx.pc`
	LIBS   += `pkg-config --libs $(MPI_HOME)/lib/pkgconfig/ompi-cxx.pc`
endif
endif

CXXFLAGS            += -Wall
LIBS                += -pthread
INCLUDES             = $(INCS)

SOURCES              = $(wildcard *.cpp)
TARGET              += $(SOURCES:.cpp=)

.PHONY: all clean cleanall 
.SUFFIXES: .c .cpp .o

%.d: %.cpp
	set -e; $(CXX) -MM $(INCLUDES) $(CXXFLAGS) $< \
		| sed 's/\($*\)\.o[ :]*/\1 $@ : /g' > $@; \
		[ -s $@ ] || rm -f $@
%.d: %.c
	set -e; $(CC) -MM $(INCLUDES) $(CFLAGS)  $< \
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