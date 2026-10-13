#include <string>
#include <iostream>
#include <csignal>

// zmq
#include <zmq.hpp>
#include <zmq_addon.hpp>
#include "zhelpers.hpp"
#include "mdp.h"

// locals
#include "loader.hpp"

using namespace std::chrono_literals;

#define CLIENT_IDENTITY "CLIENT"
#define POLL_TIMEOUT 5000ms
#define POLL_RETRIES 3

static bool debug = false;
static std::atomic<bool> running{true};
void signal_handler(int sig_val) { running = false; }

class client {
public:
    client(std::string address, bool debug = false): 
    _debug(debug)
    {
        connect(address);
    }

    virtual ~client()
    {
        printf("CLIENT: cleaning up client\n");
        delete _socket;
        delete _context;
    }

    void connect(std::string address)
    {
        const std::string server_pub = load_key("keys/server.pub", true);
        const std::string client_pub = load_key("keys/clients/client.pub", false);
        const std::string client_sec = load_key("keys/client.secret", true);

        _context = new zmq::context_t(1);

        printf("CLIENT: connecting to server...\n");

        _socket = new zmq::socket_t(*_context, ZMQ_DEALER);
        std::string identity = std::string(CLIENT_IDENTITY) + "_" + std::to_string(within(0x10000));
        _socket->set(zmq::sockopt::curve_serverkey, server_pub);
        _socket->set(zmq::sockopt::curve_publickey, client_pub);
        _socket->set(zmq::sockopt::curve_secretkey, client_sec);
        _socket->set(zmq::sockopt::routing_id, identity);
        _socket->set(zmq::sockopt::linger, 0);

        _socket->connect(address);
        if (_debug) {
            printf("CLIENT: identify=%s\n", identity.c_str());
        }
    }

    void send(std::string service, const std::string &body)
    {
        zmq::multipart_t request;

        //  Prefix request with protocol frames
        //  Frame 0: empty (REQ emulation)
        //  Frame 1: "MDPCxy" (six bytes, MDP/Client x.y)
        //  Frame 2: Service name (printable string)
        request.addstr("");
        request.addstr(std::string(k_mdp_client));
        request.addstr(service);
        request.addstr(body);

        if (_debug) {
            printf("CLIENT: sending to '%s': {%s}\n", service.c_str(), request.str().c_str());
        }
        request.send(*_socket);
    }

    zmq::multipart_t recv(const std::string &exp_service)
    {
        //  Poll socket for a reply, with timeout
        zmq::pollitem_t items[] = { { *_socket, 0, ZMQ_POLLIN, 0 } };
        zmq::poll(items, 1, std::chrono::duration(POLL_TIMEOUT));
       
        //  If we got a reply, process it
        if (items[0].revents & ZMQ_POLLIN) {
            zmq::multipart_t msg(*_socket);
            if (_debug) {
                printf("CLIENT: received reply: %s\n\n", msg.str().c_str());
            }

            if (msg.size() < 4) return {};

            std::string delim   = msg.popstr();
            std::string header  = msg.popstr();
            std::string service = msg.popstr();

            // reply validation
            if (!delim.empty()) { 
                printf("CLIENT: bad delimiter\n"); 
                return {}; 
            }

            if (header != k_mdp_client) { 
                printf("CLIENT: bad header [%s]\n", header.c_str()); 
                return {}; 
            }

            if (service != exp_service) { 
                printf("CLIENT: stale reply for '%s'\n", service.c_str()); 
                return {}; 
            }

            return msg;
        } else if (_debug) {
            printf("CLIENT: timed out waiting for reply\n");
        }

        return {};
    }

private:
    zmq::context_t *_context;
    zmq::socket_t *_socket;
    const bool _debug = false;
};

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printf("printing help info\n");
        } else if (arg == "--debug") {
            printf("CLIENT: enabling debug prints\n");
            debug = true;
        } else {
            std::cout << "Unknown option: " << arg << "\n";
        }
    }

    // handle signals
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGSEGV, signal_handler);
    std::signal(SIGABRT, signal_handler);

    // create client socket
    std::string address = "tcp://localhost:5761";
    client *session = new client(address, debug);

    int request_nbr = 0;
    while(running) {
        try {
            std::string body = "request #" + std::to_string(++request_nbr);

            bool delivered = false;

            for (int attempt = 0; attempt < POLL_RETRIES && running; ++attempt) {
                if (attempt > 0) {
                    printf("CLIENT: no response, reconnecting (%d/%d)...\n",
                    attempt + 1, POLL_RETRIES);
                    delete session;
                    session = new client(address, debug);
                }

                session->send("echo", body);

                for (;;) {
                    zmq::multipart_t reply = session->recv("echo");
                    if (reply.empty()) break;                    // -> next attempt

                    std::string status = reply.popstr();
                    std::string text   = reply.empty() ? std::string{} : reply.popstr();

                    if (status == k_mdpw_final) {
                        printf("CLIENT: final   [%s]\n", text.c_str());
                        delivered = true;
                        break;
                    }
                    printf("CLIENT: partial [%s]\n", text.c_str());  // fresh 5s window
                }

                if (delivered) break;
            }

            if (!delivered) {
                printf("CLIENT: server appears to be offline, giving up\n");
                running = false;
            }
        }
        catch (std::exception &e) {
            printf("CLIENT: got back exception: %s\n", e.what());
            running = false;
        }
    }

    delete session;
    return 0;
}

