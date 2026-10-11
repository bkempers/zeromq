#include <string>
#include <iostream>
#include <csignal>

#include <zmq.hpp>
#include "zhelpers.hpp"

using namespace std::chrono_literals;

#define CLIENT_IDENTITY "CLIENT_1"
#define POLL_TIMEOUT 5000ms
#define POLL_RETRIES 3

static bool running = true;
void signal_handler(int sig_val) { running = false; }

static zmq::socket_t *client_create(zmq::context_t &context)
{
    printf("CLIENT: connecting to server...\n");
    zmq::socket_t *client = new zmq::socket_t(context, ZMQ_DEALER);
    char identity[9] = {CLIENT_IDENTITY};
    client->set( zmq::sockopt::routing_id, identity);
    client->connect("tcp://localhost:5761");

    int stay_alive = 0;
    client->set(zmq::sockopt::linger, stay_alive);

    return client;
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printf("printing help info\n");
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
    zmq::context_t context(1);
    zmq::socket_t *client = client_create(context);

    int request_nbr = 0;
    int retries_left = POLL_RETRIES;
    try {
        while (running) {
            printf("CLIENT: sending request...\n");
            char request_string[16] = {};
            snprintf(request_string, sizeof(request_string),"request #%d", ++request_nbr);
            client->send(zmq::str_buffer(request_string), zmq::send_flags::none);

            printf("CLIENT: expecting reply...\n");
            bool expect_reply = true;
            while(expect_reply) {
                zmq::pollitem_t items[] = { { *client, 0, ZMQ_POLLIN, 0 } };
                zmq::poll(items, 1, std::chrono::duration(POLL_TIMEOUT));

                if (items[0].revents & ZMQ_POLLIN) {
                    printf("\n%s", CLIENT_IDENTITY);
                    s_dump(*client);
                    expect_reply = false;
                } else if (--retries_left == 0){
                    printf("CLIENT: server appears to be offline..\n");
                    running = false;
                    break;
                } else {
                    printf("CLIENT: no response from server, retrying...\n");
                    delete client;
                    client = client_create(context);
                    client->send(zmq::str_buffer(request_string), zmq::send_flags::none);
                    expect_reply = false;
                }
            }
        }
    }
    catch (std::exception &e) {
        printf("CLIENT: got back exception: %s\n", e.what());
    }

    printf("CLIENT: ending program\n");
    client->close();
    context.close();
    return 0;
}

