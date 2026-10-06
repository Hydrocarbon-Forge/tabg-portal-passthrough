// ws.h — a minimal RFC6455 WebSocket CLIENT for the Portal server plugin's link
// to the passthrough hub (ws://127.0.0.1:25600, role "guest").
//
// Modeled on the reference universal-modder .../gta/src/ws.h, adapted to be
// cross-platform (winsock on Windows for the real 32-bit plugin, POSIX sockets
// on Linux so the fake_portal harness exercises the exact same code here).
//
// A background thread connects (and reconnects every second), answers pings, and
// queues received text messages; send()/poll() are safe from any thread. No
// exception ever propagates out of a public method (all paths return bool), so
// nothing unwinds across the Source plugin boundary. No global constructors.
//
// Client-to-server frames are masked per RFC6455 §5.3 (required of clients); the
// hub (src/link/link.py via the websockets package) unmasks them.
#ifndef PP_WS_H
#define PP_WS_H

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <stdint.h>

class WsClient {
public:
    WsClient() {}
    ~WsClient() { stop(); }

    // Start the background connect/recv thread. host is a dotted-quad ("127.0.0.1").
    void start(const char *host, int port);
    // Stop the thread and close the socket. Safe to call more than once.
    void stop();

    bool connected() const { return m_connected; }
    // Bumped on every successful (re)connect so the caller can resend its hello.
    int generation() const { return m_generation; }

    // Queue one text frame. Returns false if not currently connected.
    bool send(const std::string &text);
    // Pop one received text message into `message`. Returns false if none queued.
    bool poll(std::string &message);

private:
    void run();
    bool open();
    void closeSocket();
    bool sendFrame(int opcode, const char *data, size_t size);
    bool recvAll(void *buffer, size_t size);

    std::string m_host;
    int m_port = 0;
    uintptr_t m_socket = ~uintptr_t(0);
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_generation{0};
    std::mutex m_sendLock;
    std::mutex m_queueLock;
    std::deque<std::string> m_queue;
    std::thread m_thread;
    uint32_t m_maskState = 0x9E3779B9u;
    bool m_started = false;
};

#endif // PP_WS_H
