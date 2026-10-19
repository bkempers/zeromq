#include "socket.hpp"

socket::socket(zmq::context_t &context, zmq::socket_type type, std::string name): 
    _context(&context),
    _type(type),
    _name(std::move(name))
{
    open();
}

socket::~socket() {
    printf("%s: cleaning up socket\n", _name.c_str());
}

void socket::open() {
    printf("%s: creating socket...\n", _name.c_str());

    _socket = std::make_unique<zmq::socket_t>(*_context, _type);

    _socket->set(zmq::sockopt::routing_id, _name);
    _socket->set(zmq::sockopt::linger, 0);
}

void socket::server(const std::string &server_sec, const std::string &zap_domain) {
    _socket->set(zmq::sockopt::curve_server, true);
    _socket->set(zmq::sockopt::curve_secretkey, server_sec);
    _socket->set(zmq::sockopt::zap_domain, zap_domain);
}

void socket::client(const std::string &server_pub, const std::string &client_pub, const std::string &client_sec) {
    _socket->set(zmq::sockopt::curve_serverkey, server_pub);
    _socket->set(zmq::sockopt::curve_publickey, client_pub);
    _socket->set(zmq::sockopt::curve_secretkey, client_sec);
}

void socket::monitored() {
    _monitor = std::make_unique<monitor>(_name);
    _monitor->init(*_socket, "inproc://monitor-" + _name);
}

void socket::drain_monitor(int timeout_ms) {
    if (_monitor) {
        while (_monitor->check_event(timeout_ms)) {}
    }
}

void socket::bind(std::string address) {
    _endpoint = std::move(address);
    _bound = true;
    _socket->bind(_endpoint);
    printf("%s: bound to %s\n", _name.c_str(), _endpoint.c_str());
    drain_monitor(500);
}

void socket::connect(std::string address) {
    _endpoint = std::move(address);
    _bound = false;
    _socket->connect(_endpoint);
    printf("%s: connecting to %s\n", _name.c_str(), _endpoint.c_str());
    drain_monitor(500);
}

void socket::reconnect() {
      if (_endpoint.empty()) return;

      printf("%s: reconnecting to %s\n", _name.c_str(), _endpoint.c_str());

      if (_bound) {
          _socket->unbind(_endpoint);
          _socket->bind(_endpoint);
          drain_monitor(500);
      } else {
          _socket->disconnect(_endpoint);
          _socket->connect(_endpoint);
          drain_monitor(500);
      }
}

bool socket::poll_in(std::chrono::milliseconds timeout) {
    zmq::pollitem_t items[] = { { *_socket, 0, ZMQ_POLLIN, 0 } };
    zmq::poll(items, 1, timeout);
    drain_monitor();
    return (items[0].revents & ZMQ_POLLIN) != 0;
}

void socket::send(zmq::multipart_t &msg) {
    if (_debug) {
        printf("%s: sending %s\n", _name.c_str(), msg.str().c_str());
    }
}

std::optional<zmq::multipart_t> socket::recv(std::chrono::milliseconds timeout) {
    if (!poll_in(timeout)) {
        if (_debug) {
            printf("%s: timed out waiting for reply\n", _name.c_str());
        }
        
        return std::nullopt;
    }

    zmq::multipart_t msg(*_socket);
    if (_debug) {
      printf("%s: received %s\n", _name.c_str(), msg.str().c_str());
    }

    return msg;
}
