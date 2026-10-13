#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <sys/stat.h>

namespace fs = std::filesystem;

std::string load_key(const fs::path& path, bool is_secret)
{
    if (is_secret) {
        struct stat st{};
        if (stat(path.c_str(), &st) != 0 || (st.st_mode & (S_IRWXG | S_IRWXO))) {
            throw std::runtime_error(path.string() + ": secret key must be mode 0600");
        }
    }
    std::ifstream in(path);
    std::string key;
    std::getline(in, key);
    if (key.size() != 40) {
        throw std::runtime_error(path.string() + ": not a 40-char Z85 key");
    }
    return key;
}

// Allow list: Z85 public key -> user_id (file name without extension).
std::unordered_map<std::string, std::string> load_allowlist(const fs::path& dir)
{
    std::unordered_map<std::string, std::string> allowed;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".pub") {
            allowed.emplace(load_key(entry.path(), false), entry.path().stem().string());
        }
    }
    return allowed;
}
