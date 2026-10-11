#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <iostream>

#include <zmq.hpp>
#include "zhelpers.hpp"

#include "message.pb.h"

static bool running = true;
void signal_handler(int sig_val) { running = false; }

class service {
public:
    service(zmq::context_t &ctx, int sock_type)
        : ctx_(ctx),
          worker_(ctx_, sock_type) {}

    void work() {
        worker_.connect("inproc://backend");

        try {
            while (true) {
                zmq::message_t identity;
                zmq::message_t msg;
                zmq::message_t copied_id;
                zmq::message_t copied_msg;
                worker_.recv(identity, zmq::recv_flags::none);
                worker_.recv(msg, zmq::recv_flags::none);

                printf("id:[%s] msg:[%s]\n", identity.to_string().c_str(), msg.to_string().c_str());

                int replies = within(5);
                for (int reply = 0; reply < replies; ++reply) {
                    s_sleep(within(1000) + 1);
                    copied_id.copy(identity);
                    copied_msg.copy(msg);
                    worker_.send(copied_id, zmq::send_flags::sndmore);
                    worker_.send(copied_msg, zmq::send_flags::none);
                }
            }
        }
        catch (std::exception &e) {}
    }

private:
    zmq::context_t &ctx_;
    zmq::socket_t worker_;
};

int main(int argc, char *argv[]) 
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printf("help print\n");
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

    // setup server
    zmq::context_t context(1);
    zmq::socket_t frontend(context, ZMQ_ROUTER);
    zmq::socket_t backend(context, ZMQ_DEALER);
    frontend.bind("tcp://localhost:5761");
    backend.bind("inproc://backend");

    //single backend worker thread
    service *worker = new service(context, ZMQ_DEALER);
    std::thread service_thread(std::bind(&service::work, worker));
    service_thread.detach();

    while(running) {
        try {
            zmq::proxy(frontend, backend);
        }
        catch (std::exception &e) {
            printf("SERVER: got back exception: %s\n", e.what());
        }
    }

    printf("SERVER: ending program\n");

    service_thread.join();
    delete worker;

    frontend.close();
    backend.close();
    context.close();

    return 0;
}
