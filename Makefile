CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17

HEADERS  := protocol.hpp fec.hpp net.hpp

all: sender receiver

sender: sender.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o sender sender.cpp

receiver: receiver.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o receiver receiver.cpp

clean:
	rm -f sender receiver
