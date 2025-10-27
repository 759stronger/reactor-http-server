all:client 
tcpserver:server.cc
	g++ -std=c++11 $^ -o $@   -lpthread
client:client4.cpp
	g++ -std=c++11 $^ -o $@    -lpthread
server:tcp_srv.cc
	g++ -std=c++11 $^ -o $@   -lpthread