#pragma once
// Bedrock Forge client - [BF] write small report files off the game thread. The text is built on the caller's thread
// (in memory, cheap); the disk write happens on a short-lived worker, and only when the text changed since the last
// write of that path. Report writes used to run on the client thread and could stall a frame by 3-12 ms on servers.
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace bedrock_forge::client::asyncfile {
inline void write(std::filesystem::path path, std::string text) {
    static std::mutex                            mx;
    static std::map<std::string, std::string>    last;
    {
        std::lock_guard l(mx);
        auto& prev = last[path.string()];
        if (prev == text) return;
        prev = text;
    }
    std::thread([path = std::move(path), text = std::move(text)] {
        try {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        } catch (...) {}
    }).detach();
}
} // namespace bedrock_forge::client::asyncfile
