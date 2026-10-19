#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <iostream>

// zmq
#include <zmq.hpp>
#include <zmq_addon.hpp>
#include "zhelpers.hpp"
#include "mdp.h"

// locals
#include "loader.hpp"
#include "auth.hpp"

//protobuf
#include "message.pb.h"

using namespace std::chrono_literals;

#define WORKER_TIMEOUT 1000ms

static bool debug = false;
static std::atomic<bool> running{true};
void signal_handler(int sig_val) { running = false; }

class service {
public:
    service(zmq::context_t &ctx, int sock_type)
        : ctx_(ctx),
          worker_(ctx_, sock_type) {}

    void reply(const std::string &client_id, const std::string &command, std::string_view status, const std::string &body) {
        zmq::multipart_t msg;

        msg.addstr(client_id);
        msg.addstr(command);
        msg.addstr(std::string(status));
        msg.addstr(body);

        msg.send(worker_);
    }

    void work() {
        worker_.connect("inproc://backend");

        zmq::pollitem_t items[] = { { worker_, 0, ZMQ_POLLIN, 0 } };

        while(running) {
            try {
                zmq::poll(items, 1, std::chrono::duration(WORKER_TIMEOUT));
                if (!(items[0].revents & ZMQ_POLLIN)) continue;

                zmq::multipart_t msg(worker_);
                if (msg.size() < 2) continue;

                std::string client_id = msg.popstr();
                std::string command = msg.popstr();
                std::string body = msg.empty() ? std::string() : msg.popstr();

                printf("WORKER: id=[%s], cmd=[%s] body=[%s]\n", client_id.c_str(), command.c_str(), body.c_str());

                if (command == "echo") {
                    int partials = within(4);
                    for (int i = 0; i < partials; ++i) {
                        s_sleep(within(500) + 1);
                        reply(client_id, command, k_mdpw_partial, body + " (partial " + std::to_string(i + 1) + ")");
                    }
                    reply(client_id, command, k_mdpw_final, body);
                } else {
                    reply(client_id, command, k_mdpw_final, "ERROR unknown command");
                }
              

            }
            catch (std::exception &e) {
                printf("WORKER: got back exception: %s\n", e.what());
                running = false;
            }
        }
    }

private:
    zmq::context_t &ctx_;
    zmq::socket_t worker_;
};

class server {
public:
    server(std::string address, bool debug = false):
    _debug(debug)
    {
        _context = new zmq::context_t(1);

        // authentication backend
        auto allowed = load_allowlist("keys/clients");
        const std::string server_sec = load_key("keys/server.secret", true);

        zmq::socket_t zap(*_context, zmq::socket_type::rep);
        zap.set(zmq::sockopt::linger, 0);
        zap.bind("inproc://zeromq.zap.01");
        _zap_thread = std::thread(zap_handler, std::move(zap), std::move(allowed));
        
        // server backend/frontend infrastructure
        _socket_frontend = new zmq::socket_t(*_context, ZMQ_ROUTER);
        _socket_frontend->set(zmq::sockopt::curve_server, 1);
        _socket_frontend->set(zmq::sockopt::curve_secretkey, server_sec);
        _socket_frontend->set(zmq::sockopt::zap_domain, "teal");
        _socket_frontend->set(zmq::sockopt::linger, 0);
        _socket_frontend->bind(address);

        _socket_backend = new zmq::socket_t(*_context, ZMQ_DEALER);
        _socket_backend->bind("inproc://backend");

        _worker = new service(*_context, ZMQ_DEALER);
        _service_thread = std::thread(std::bind(&service::work, _worker));
    }

    virtual ~server()
    {
        printf("SERVER: ending program\n");

        running = false;

        if (_service_thread.joinable()) _service_thread.join();

        _zap_thread.join(); // wait to finish 
        
        delete _worker;
        delete _socket_frontend;
        delete _socket_backend;
        delete _context;
    }

    void start()
    {
        std::array<zmq::pollitem_t, 2> items = {{ 
            { *_socket_frontend, 0, ZMQ_POLLIN, 0 },
            { *_socket_backend, 0, ZMQ_POLLIN, 0}
        }};

        while(running) {
            try {
                zmq::poll(items.data(), items.size(), std::chrono::duration(HEARTBEAT_INTERVAL));

                if (items[0].revents & ZMQ_POLLIN) { // frontend recv
                    zmq::multipart_t msg(*_socket_frontend);

                    if (_debug) {
                        printf("SERVER: received message: %s\n\n", msg.str().c_str());
                    }

                    // forward everything to backend service
                    msg.send(*_socket_backend);
                } 

                if (items[1].revents & ZMQ_POLLIN) { // backend recv
                    zmq::multipart_t msg(*_socket_backend);

                    if (_debug) {
                        printf("SERVER: sending message: %s\n", msg.str().c_str());
                    }

                    msg.send(*_socket_frontend);
                }
            }
            catch (std::exception &e) {
                printf("SERVER: got back exception: %s\n", e.what());
                running = false;
            }
        }
    }
private:
    service *_worker;
    std::thread _service_thread;
    std::thread _zap_thread;

    zmq::context_t *_context;
    zmq::socket_t *_socket_frontend;
    zmq::socket_t *_socket_backend;

    const bool _debug = false;
};

int main(int argc, char *argv[]) 
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printf("help print\n");
        } else if (arg == "--debug") {
            printf("SERVER: enabling debug prints\n");
            debug = true;
        } else {
            std::cout << "Unknown option: " << arg << "\n";
        }
    }

    if (!zmq_has("curve")) {
        printf("libzmq has no CURVE\n");
        return 1;
    }

    std::tuple<int, int, int> version = zmq::version();
    printf("running zmq version %d.%d.%d\n", 
            std::get<0>(version), 
            std::get<1>(version), 
            std::get<2>(version));

    // handle signals
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGSEGV, signal_handler);
    std::signal(SIGABRT, signal_handler);

    // create server
    std::string address = "tcp://localhost:5761";
    server *session = new server(address, debug);

    session->start();

    delete session;
    return 0;
}
