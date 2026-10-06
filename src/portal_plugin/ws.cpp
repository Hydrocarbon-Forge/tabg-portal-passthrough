// ws.cpp — RFC6455 WebSocket client implementation. See ws.h.
//
// Platform sockets: winsock2 on Windows (the shipped 32-bit plugin), plain
// POSIX BSD sockets on Linux (so tests/fake_portal/fake_portal.cpp links the
// same file). The protocol framing is identical on both.
#include "ws.h"

#include <chrono>
#include <vector>
#include <cstring>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET pp_socket_t;
#  define PP_INVALID_SOCKET INVALID_SOCKET
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   typedef int pp_socket_t;
#  define PP_INVALID_SOCKET (-1)
   static inline int closesocket(int s) { return ::close(s); }
#endif

namespace {
const uintptr_t kNoSocket = ~uintptr_t(0);
}

void WsClient::start(const char *host, int port) {
    if (m_started) return;
    m_host = host ? host : "127.0.0.1";
    m_port = port;
    m_stop = false;
    m_started = true;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    m_thread = std::thread([this] { run(); });
}

void WsClient::stop() {
    if (!m_started) return;
    m_stop = true;
    closeSocket();
    if (m_thread.joinable())
        m_thread.join();
    m_started = false;
#ifdef _WIN32
    WSACleanup();
#endif
}

bool WsClient::open() {
    pp_socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == PP_INVALID_SOCKET)
        return false;
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)m_port);
    inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);
    if (connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return false;
    }
    int noDelay = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char *>(&noDelay), sizeof(noDelay));

    // RFC6455 opening handshake. A fixed Sec-WebSocket-Key is fine for a
    // trusted localhost hub (the Python `websockets` server accepts it).
    const std::string request =
        "GET / HTTP/1.1\r\nHost: " + m_host + ":" + std::to_string(m_port) +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";
    m_socket = (uintptr_t)s;
    {
        std::lock_guard<std::mutex> lock(m_sendLock);
        const char *p = request.data();
        size_t left = request.size();
        while (left > 0) {
            int n = ::send(s, p, (int)left, 0);
            if (n <= 0) { m_socket = kNoSocket; closesocket(s); return false; }
            p += n; left -= (size_t)n;
        }
    }
    // Read the HTTP response headers up to the blank line.
    std::string response;
    char c;
    while (response.size() < 4096 && response.find("\r\n\r\n") == std::string::npos) {
        if (!recvAll(&c, 1)) { m_socket = kNoSocket; closesocket(s); return false; }
        response += c;
    }
    if (response.compare(0, 12, "HTTP/1.1 101") != 0) {
        m_socket = kNoSocket;
        closesocket(s);
        return false;
    }
    return true;
}

void WsClient::closeSocket() {
    std::lock_guard<std::mutex> lock(m_sendLock);
    if (m_socket != kNoSocket) {
        closesocket((pp_socket_t)m_socket);
        m_socket = kNoSocket;
    }
    m_connected = false;
}

bool WsClient::recvAll(void *buffer, size_t size) {
    char *p = static_cast<char *>(buffer);
    while (size > 0) {
        if (m_socket == kNoSocket) return false;
        int n = recv((pp_socket_t)m_socket, p, (int)size, 0);
        if (n <= 0)
            return false;
        p += n;
        size -= (size_t)n;
    }
    return true;
}

bool WsClient::sendFrame(int opcode, const char *data, size_t size) {
    std::lock_guard<std::mutex> lock(m_sendLock);
    if (m_socket == kNoSocket)
        return false;
    std::vector<char> frame;
    frame.reserve(size + 14);
    frame.push_back((char)(0x80 | opcode));
    if (size < 126)
        frame.push_back((char)(0x80 | size));
    else if (size < 65536) {
        frame.push_back((char)(0x80 | 126));
        frame.push_back((char)(size >> 8));
        frame.push_back((char)(size));
    } else {
        frame.push_back((char)(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            frame.push_back((char)((uint64_t)size >> (8 * i)));
    }
    // Client frames MUST be masked (RFC6455 §5.3). A cheap LCG keeps the mask
    // varying without pulling in <random>'s global state.
    m_maskState = m_maskState * 1664525u + 1013904223u;
    const uint32_t mask = m_maskState;
    const char *m = reinterpret_cast<const char *>(&mask);
    frame.insert(frame.end(), m, m + 4);
    for (size_t i = 0; i < size; ++i)
        frame.push_back(data[i] ^ m[i & 3]);
    const char *p = frame.data();
    size_t left = frame.size();
    while (left > 0) {
        int n = ::send((pp_socket_t)m_socket, p, (int)left, 0);
        if (n <= 0)
            return false;
        p += n;
        left -= (size_t)n;
    }
    return true;
}

bool WsClient::send(const std::string &text) {
    if (!m_connected)
        return false;
    if (!sendFrame(0x1, text.data(), text.size())) {
        closeSocket();
        return false;
    }
    return true;
}

bool WsClient::poll(std::string &message) {
    std::lock_guard<std::mutex> lock(m_queueLock);
    if (m_queue.empty())
        return false;
    message = std::move(m_queue.front());
    m_queue.pop_front();
    return true;
}

void WsClient::run() {
    std::string partial;
    while (!m_stop) {
        if (!m_connected) {
            if (open()) {
                m_connected = true;
                ++m_generation;
            } else {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
        }

        unsigned char head[2];
        if (!recvAll(head, 2)) { closeSocket(); continue; }
        const int opcode = head[0] & 0x0F;
        const bool fin = (head[0] & 0x80) != 0;
        uint64_t size = head[1] & 0x7F;
        if (size == 126) {
            unsigned char ext[2];
            if (!recvAll(ext, 2)) { closeSocket(); continue; }
            size = ((uint64_t)ext[0] << 8) | ext[1];
        } else if (size == 127) {
            unsigned char ext[8];
            if (!recvAll(ext, 8)) { closeSocket(); continue; }
            size = 0;
            for (int i = 0; i < 8; ++i)
                size = (size << 8) | ext[i];
        }
        unsigned char mask[4] = {0, 0, 0, 0};
        const bool masked = (head[1] & 0x80) != 0;
        if (masked && !recvAll(mask, 4)) { closeSocket(); continue; }
        std::string payload((size_t)size, '\0');
        if (size > 0 && !recvAll(&payload[0], payload.size())) { closeSocket(); continue; }
        if (masked)
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] ^= mask[i & 3];

        switch (opcode) {
        case 0x0: // continuation
        case 0x1: // text
            partial += payload;
            if (fin) {
                std::lock_guard<std::mutex> lock(m_queueLock);
                m_queue.push_back(std::move(partial));
                partial.clear();
                if (m_queue.size() > 1024) // latest-wins: never let a stall grow unbounded
                    m_queue.pop_front();
            }
            break;
        case 0x8: // close
            closeSocket();
            break;
        case 0x9: // ping -> pong
            sendFrame(0xA, payload.data(), payload.size());
            break;
        default:
            break;
        }
    }
}
