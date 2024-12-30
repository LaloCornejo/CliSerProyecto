CXX = g++
CXXFLAGS = -Wall -std=c++11

all: client server

client: client.cpp
	$(CXX) $(CXXFLAGS) client.cpp -o client

server: server.cpp
	$(CXX) $(CXXFLAGS) server.cpp -o server

clean:
	rm -f client server

logs:
	rm -f *.log && touch server.log && touch client.log

.PHONY: all clean logs

