#ifndef AUTH_HPP
#define AUTH_HPP

#include <zmq.hpp>
#include <zmq_addon.hpp>

#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

// ZAP handler (RFC 27).
// CURVE request frames:
//   [0] version "1.0"  
//   [1] request_id  
//   [2] domain  
//   [3] address
//   [4] routing_id     
//   [5] mechanism "CURVE"       
//   [6] client public key (32 bytes binary)
//
// Reply frames:
//   [0] version "1.0"  
//   [1] request_id  
//   [2] status_code  
//   [3] status_text
//   [4] user_id        
//   [5] metadata

static std::string key_to_z85(const zmq::message_t& key)
{
    if (key.size() != 32) {
        return {};
    }
    std::array<char, 41> out{};
    if (zmq_z85_encode(out.data(), static_cast<const uint8_t*>(key.data()), 32) == nullptr) {
        return {};
    }
    return std::string(out.data(), 40);
}

static void zap_handler(zmq::socket_t zap, std::unordered_map<std::string, std::string> allowed) {
    for (;;) {
        std::vector<zmq::message_t> req;
        try {
            if (!zmq::recv_multipart(zap, std::back_inserter(req))) {
                continue;
            }
        } catch (const zmq::error_t& e) {
            if (e.num() == ETERM) {
                break; // Context shutdown. Stop the handler.
            }
            throw;
        }

        if (req.size() < 6) {
            continue; // Malformed request.
        }

        const std::string version   = req[0].to_string();
        const std::string domain    = req[2].to_string();
        const std::string address   = req[3].to_string();
        const std::string mechanism = req[5].to_string();

        std::string status = "400";
        std::string text   = "Denied";
        std::string user_id;

        if (version == "1.0" && mechanism == "CURVE" && req.size() >= 7) {
            const std::string client_key = key_to_z85(req[6]);
            auto it = allowed.find(client_key);
            if (it != allowed.end()) {
                status  = "200";
                text    = "OK";
                user_id = it->second;
            }
            std::cout << "[ZAP] domain=" << domain << " addr=" << address
                      << " key=" << client_key << " -> " << status << "\n";
        }

        std::vector<zmq::message_t> rep;
        rep.emplace_back(std::string("1.0"));
        rep.push_back(std::move(req[1]));   // Echo request_id unchanged.
        rep.emplace_back(status);
        rep.emplace_back(text);
        rep.emplace_back(user_id);          // Shows as "User-Id" on received messages.
        rep.emplace_back();                 // Metadata (empty).
        zmq::send_multipart(zap, rep);
    }
}

#endif // AUTH_HPP
