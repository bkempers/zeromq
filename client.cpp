#include <string>
#include <iostream>
#include <csignal>

#include <zmq.hpp>
#include "zhelpers.hpp"
#include "zmsg.hpp"
#include "mdp.h"

using namespace std::chrono_literals;

#define CLIENT_IDENTITY "CLIENT_1"
#define POLL_TIMEOUT 5000ms
#define POLL_RETRIES 3

static bool debug = false;
static bool running = true;
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
        _context = new zmq::context_t(1);

        printf("CLIENT: connecting to server...\n");

        _socket = new zmq::socket_t(*_context, ZMQ_DEALER);
        char identity[9] = {CLIENT_IDENTITY};
        _socket->set( zmq::sockopt::routing_id, identity);
        
        _socket->connect(address);

        int stay_alive = 0;
        _socket->set(zmq::sockopt::linger, stay_alive);
    }

    void send(std::string service, zmsg *&request_p)
    {
        assert (request_p);
        zmsg *request = request_p;

        //  Prefix request with protocol frames
        //  Frame 0: empty (REQ emulation)
        //  Frame 1: "MDPCxy" (six bytes, MDP/Client x.y)
        //  Frame 2: Service name (printable string)
        request->push_front (service.c_str());
        request->push_front (k_mdp_client.data());
        request->push_front ("");
        if (_debug) {
            printf("CLIENT: sending request to '%s' service\n", service.c_str());
           request->dump();
        }
        request->send(*_socket);
    }

    zmsg *recv(void)
    {
        //  Poll socket for a reply, with timeout
        zmq::pollitem_t items[] = { { *_socket, 0, ZMQ_POLLIN, 0 } };
        zmq::poll(items, 1, std::chrono::duration(POLL_TIMEOUT));
       
        //  If we got a reply, process it
        if (items[0].revents & ZMQ_POLLIN) {
           zmsg *msg = new zmsg(*_socket);
           if (_debug) {
               printf("CLIENT: received reply:");
               msg->dump();
           }
           //  Don't try to handle errors, just assert noisily
           assert (msg->parts() >= 4);

           assert (msg->pop_front().length() == 0);  // empty message

           ustring header = msg->pop_front();
           assert (header.compare((unsigned char *)k_mdp_client.data()) == 0);

           ustring service = msg->pop_front();
           assert (service.compare((unsigned char *)service.c_str()) == 0);

           return msg;
        } else if (_debug) {
            printf("CLIENT: timed out waiting for reply\n");
        }

        return 0;
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
    client *session = new client(address);

    int request_nbr = 0;
    int retries_left = POLL_RETRIES;
    try {
        while (running) {
            char request_string[16] = {};
            snprintf(request_string, sizeof(request_string),"request #%d", ++request_nbr);
            
            zmsg *request = new zmsg(request_string);
            session->send("echo", request);

            bool expect_reply = true;
            while(expect_reply) {
                zmsg *reply = session->recv();

                if (reply) {
                    delete reply;

                    expect_reply = false;
                    retries_left = POLL_RETRIES; // reset retry
                } else if (--retries_left == 0){
                    printf("CLIENT: server appears to be offline..\n");
                    running = false;

                    break;
                } else {
                    printf("CLIENT: no response from server, retrying...\n");
                    delete session;

                    session = new client(address);
                    zmsg *request = new zmsg(request_string);
                    session->send("echo", request);
                    
                    expect_reply = false;
                }
            }
        }
    }
    catch (std::exception &e) {
        printf("CLIENT: got back exception: %s\n", e.what());
    }

    delete session;
    return 0;
}

