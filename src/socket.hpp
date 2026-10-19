#pragma once

#include <vector>
#include <tuple>
#include <string>
#include <iostream>
#include <chrono>
#include <memory>
#include <optional>

//zmq
#include <zmq.hpp>
#include <zmq_addon.hpp>
#include "zhelpers.hpp"
#include "mdp.h"

using namespace std::chrono_literals;

#define CLIENT_IDENTITY "CLIENT"
#define SERVER_IDENTITY "SERVER"
#define POLL_TIMEOUT 5000ms

class monitor : public zmq::monitor_t {
public:
    explicit monitor(std::string name): _name(std::move(name)) {}

    void on_event_connected(const zmq_event_t &, const char *addr) override {
        printf("%s: connected to addr: %s\n", _name.c_str(), addr);
    }

    // TODO: add more override events

private:
    std::string _name;
};

class socket {
public:
    socket(zmq::context_t &context, zmq::socket_type type, std::string name);

    ~socket();

    socket(const socket &) = delete;
    socket &operator=(const socket &) = delete;
    socket(socket &&) noexcept = default;
    socket &operator=(socket &&) noexcept = delete;

    // socket configuration
    void monitored();
    void drain_monitor(int timeout_ms = 0);
    void set_debug(bool enable) { _debug = enable; }

    // server socket
    void server(const std::string &server_sec, const std::string &zap_domain);
    
    // client socket
    void client(const std::string &server_pub, const std::string &client_pub, const std::string &client_sec);

    // lifecycle
    void bind(std::string address);
    void connect(std::string address);
    void reconnect();

    // messaging
    bool poll_in(std::chrono::milliseconds timeout);
    void send(zmq::multipart_t &msg);
    std::optional<zmq::multipart_t> recv(std::chrono::milliseconds timeout);

    zmq::socket_t &handle() { return *_socket; }
    const std::string &name() const { return _name; }

private:
    void open();

    zmq::context_t *_context;
    zmq::socket_type _type;
    std::string _name;
    std::string _endpoint;

    std::unique_ptr<zmq::socket_t> _socket;
    std::unique_ptr<monitor> _monitor;

    bool _bound = false;
    bool _debug = false;
};
