#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace empower {

// A minimal localhost HTTP server that lets the demo be driven remotely:
//   POST /trigger/<scenario>   -> invokes the handler with "<scenario>"
//
// It is deliberately tiny (one connection at a time) - enough to remotely
// control the Fleet Control Center for a live demo or from a script.
class CommandServer {
public:
    using Handler = std::function<void(const std::string& command)>;

    ~CommandServer();

    // Binds 127.0.0.1:<port> and serves on a background thread. Returns false
    // if the socket could not be opened.
    bool start(int port, Handler handler);
    void stop();

private:
    void run();

    std::thread thread_;
    std::atomic<bool> running_{false};
    long long listen_fd_ = -1;
    Handler handler_;
};

} // namespace empower
