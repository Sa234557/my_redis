CXX      = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17

all: server client

server: server.cpp hashtable.cpp heap.cpp avl.cpp zset.cpp
	$(CXX) $(CXXFLAGS) -o server server.cpp hashtable.cpp heap.cpp avl.cpp zset.cpp

client: client.cpp
	$(CXX) $(CXXFLAGS) -o client client.cpp

clean:
	rm -f server client

.PHONY: all clean
