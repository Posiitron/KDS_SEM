CXX = g++
CXXFLAGS = -std=c++11 -Wall

all: sender receiver

sender: sender.cpp
	$(CXX) $(CXXFLAGS) -o sender sender.cpp -lcrypto

receiver: receiver.cpp
	$(CXX) $(CXXFLAGS) -o receiver receiver.cpp -lcrypto

clean:
	rm -f sender receiver