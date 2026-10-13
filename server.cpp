#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <iostream>

#include <zmq.hpp>
#include <zmq_addon.hpp>

#include "zhelpers.hpp"
#include "mdp.h"

#include "message.pb.h"

using namespace std::chrono_literals;

#define HEARTBEAT_LIVENESS 3
#define HEARTBEAT_INTERVAL 3000ms
#define HEARTBEAT_EXPIRY (HEARTBEAT_LIVENESS * HEARTBEAT_INTERVAL)

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
                zmq::poll(items, 1, std::chrono::duration(HEARTBEAT_INTERVAL));
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
        
        _socket_frontend = new zmq::socket_t(*_context, ZMQ_ROUTER);
        _socket_backend = new zmq::socket_t(*_context, ZMQ_DEALER);

        _socket_frontend->bind(address);
        _socket_backend->bind("inproc://backend");

        //single backend worker thread
        _worker = new service(*_context, ZMQ_DEALER);

        _service_thread = std::thread(std::bind(&service::work, _worker));
        // _service_thread.detach();

        // start proxy - no questions asked forward BLOCKS
        // zmq::proxy(*_socket_frontend, *_socket_backend);
    }

    virtual ~server()
    {
        printf("SERVER: ending program\n");

        running = false;

        if (_service_thread.joinable()) _service_thread.join();
        
        delete _worker;
        delete _socket_frontend;
        delete _socket_backend;
        delete _context;
        // _socket_frontend->close();
        // _socket_backend->close();
        // _context->close();
    }

    void reply(const std::string &client_id, const std::string &command, std::string_view status, const std::string &body) {
        zmq::multipart_t msg;

        msg.addstr(client_id);
        msg.addstr("");
        msg.addstr(std::string(k_mdp_client));
        msg.addstr(command);
        msg.addstr(std::string(status));
        msg.addstr(body);

        msg.send(*_socket_frontend);
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
                        printf("SERVER: received message: %s\n", msg.str().c_str());
                    }

                    if (msg.size() < 4) {
                        return;
                    }

                    std::string client_id = msg.popstr();
                    msg.pop(); //empty delimiter message
                    std::string header = msg.popstr();
                    std::string command = msg.popstr();

                    if (header != k_mdp_client) return;

                    if (command == "ping") {
                        reply(client_id, command, k_mdpw_final, {"pong"});
                        return;
                    }

                    // forward everything to backend service
                    msg.pushstr(command);
                    msg.pushstr(client_id);
                    msg.send(*_socket_backend);
                } 

                if (items[1].revents & ZMQ_POLLIN) { // backend recv
                    zmq::multipart_t msg(*_socket_backend);

                    if (msg.size() < 3) return;

                    std::string client_id = msg.popstr();
                    msg.pushstr(std::string(k_mdp_client));
                    msg.pushstr("");
                    msg.pushstr(client_id);

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
