boost_path := /usr/include/

client: client.cpp
	g++ -std=c++17 -I $(boost_path) client.cpp -o client

server: server.cpp
	g++ -std=c++17 -I $(boost_path) server.cpp -o server