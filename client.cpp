#include <iostream>
#include <string>
#include "boost/asio.hpp"

namespace asio = boost::asio;
using boost::asio::ip::tcp;

void send (tcp::socket & socket,
           std::string const& msg,
           boost::system::error_code & error) {
    asio::write( socket, asio::buffer(msg), error );
}

std::string recv (tcp::socket & socket, boost::system::error_code & error) {
    asio::streambuf receive_buffer;
    asio::read(socket, receive_buffer, error);
    if( error && error != boost::asio::error::eof ) {
        throw error;
    }
    else {
        const char* data = asio::buffer_cast<const char*>(receive_buffer.data());
        return std::string{ data };
    }
}

int main() {
    asio::io_service io;
    
    boost::system::error_code err;
    std::string command, msg;
    for (;;) {
        tcp::socket socket(io);
        socket.connect( tcp::endpoint( asio::ip::address::from_string("127.0.0.1"), 1234 ));
        std::cin >> command;
        if (command == "on") {
            msg = "set-led-state on\n";
        } else if (command == "off") {
            msg = "set-led-state off\n";
        } else if (command == "state?") {
            msg = "get-led-state\n";
        } else if (command == "color?") {
            msg = "get-led-color\n";
        } else if (command == "color") {
            std::string color;
            std::cin >> color;
            msg = "set-led-color " + color + "\n";
        } else if (command == "rate") {
            int new_rate;
            std::cin >> new_rate;
            std::cerr << new_rate;
            msg = std::string("set-led-rate ") + std::to_string(new_rate) + "\n";
        } else if (command == "rate?") {
            msg = "get-led-rate\n";
        }
        else {
            std::cerr << "Wrong command\n";
            continue;
        }
        send(socket, msg, err);
        auto ack = recv(socket, err);
        if (!err || err == asio::error::eof ) {
            std::cout << ">> " << ack << "\n";
        } else {
            std::cout << err.what() << "\n";
        }
    }
 
}