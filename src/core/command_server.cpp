#include "core/command_server.h"

#include <cstring>
#include <string>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
using sock_t = SOCKET;
#  define EMPOWER_CLOSESOCK closesocket
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
using sock_t = int;
#  define EMPOWER_CLOSESOCK ::close
#  define INVALID_SOCKET (-1)
#endif

namespace empower {

namespace {

const char* kResponse =
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 3\r\n"
    "Connection: close\r\n\r\nok\n";

// Pulls the scenario id out of a request line like
// "POST /trigger/null-deref HTTP/1.1".
std::string parse_command(const std::string& request) {
    const std::string marker = "/trigger/";
    auto pos = request.find(marker);
    if (pos == std::string::npos) return "";
    pos += marker.size();
    std::string id;
    for (char c = (pos < request.size() ? request[pos] : ' ');
         pos < request.size() && c != ' ' && c != '\r' && c != '\n' && c != '?';
         c = request[++pos]) {
        id.push_back(c);
    }
    return id;
}

} // namespace

CommandServer::~CommandServer() { stop(); }

bool CommandServer::start(int port, Handler handler) {
    handler_ = std::move(handler);

#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) return false;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(fd, 4) != 0) {
        EMPOWER_CLOSESOCK(fd);
        return false;
    }

    listen_fd_ = static_cast<long long>(fd);
    running_ = true;
    thread_ = std::thread(&CommandServer::run, this);
    return true;
}

void CommandServer::run() {
    while (running_) {
        sock_t client = accept(static_cast<sock_t>(listen_fd_), nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!running_) break;
            continue;
        }
        char buf[2048];
#if defined(_WIN32)
        int n = recv(client, buf, sizeof(buf) - 1, 0);
#else
        ssize_t n = recv(client, buf, sizeof(buf) - 1, 0);
#endif
        std::string command;
        if (n > 0) {
            buf[n] = '\0';
            command = parse_command(buf);
        }
        send(client, kResponse, static_cast<int>(std::strlen(kResponse)), 0);
        EMPOWER_CLOSESOCK(client);

        // Invoke after responding so a crashing scenario still ACKs the request.
        if (!command.empty() && handler_) handler_(command);
    }
}

void CommandServer::stop() {
    if (!running_) return;
    running_ = false;
    if (listen_fd_ != -1) {
        EMPOWER_CLOSESOCK(static_cast<sock_t>(listen_fd_));
        listen_fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
#if defined(_WIN32)
    WSACleanup();
#endif
}

} // namespace empower
