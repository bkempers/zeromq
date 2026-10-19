#include "socket.hpp"

socket::socket(zmq::socket_t socket, int type, std::string name) {
    printf("%s: creating socket...\n", name.c_str());

    _context = std::unique_ptr<zmq::context_t>(new zmq::context_t(1));
    _socket = std::unique_ptr<zmq::socket_t>(new zmq::socket_t(*_context, type));

    _monitor = std::unique_ptr<monitor>(new monitor());
    _monitor->init(*_socket, "inproc://conmon");
}

socket::~socket() {
    printf("%s: cleaning up socket\n", _identity.c_str());
}



void socket::connect(std::string address) {

}

void socket::send() {

}

void socket::recv() {

}
