INC = -I../include

server:
	$(CXX) -Wall -O2 -std=c++11 $(INC) -pthread test_server.cpp -o test_server

client:
	$(CXX) -Wall -O2 -std=c++11 $(INC) -pthread test_client.cpp -o test_client
