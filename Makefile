CXX = g++
CXXFLAGS = -std=c++11 -Wall

all: sender1 receiver1 sender2 receiver2

sender1: sender1.cpp
	$(CXX) $(CXXFLAGS) -o sender1 sender1.cpp -lcrypto

receiver1: receiver1.cpp
	$(CXX) $(CXXFLAGS) -o receiver1 receiver1.cpp -lcrypto

sender2: sender2.cpp
	$(CXX) $(CXXFLAGS) -o sender2 sender2.cpp -lcrypto

receiver2: receiver2.cpp
	$(CXX) $(CXXFLAGS) -o receiver2 receiver2.cpp -lcrypto

clean:
	rm -f sender1 receiver1 sender2 receiver2