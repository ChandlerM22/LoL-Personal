#define NOMINMAX
#include "KMBoxNet.h"

#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace KMBoxNet {
namespace {

// KMBox B+ protocol constants
static constexpr uint32_t CMD_CONNECT      = 0xAF3D78;
static constexpr uint32_t CMD_MOUSE_MOVE   = 0xAF3D7A;
static constexpr uint32_t CMD_MOUSE_LEFT   = 0xAF3D7C;
static constexpr uint32_t CMD_MOUSE_RIGHT  = 0xAF3D7E;
static constexpr uint32_t CMD_MOUSE_MIDDLE = 0xAF3D80;
static constexpr uint32_t CMD_MOUSE_WHEEL  = 0xAF3D82;
static constexpr uint32_t CMD_KEYBOARD     = 0xAF3D84;
static constexpr uint32_t CMD_REBOOT       = 0xAF3D86;

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;      // 0x4D4B (MK)
    uint32_t cmd;
    uint32_t seq;
    uint32_t dataLen;
};

struct MouseMoveData {
    int16_t x;
    int16_t y;
    uint8_t  relative;   // 1 = relative, 0 = absolute
    uint8_t  reserved[3];
};

struct MouseButtonData {
    uint8_t  state;      // 1 = down, 0 = up
    uint8_t  reserved[3];
};

struct MouseWheelData {
    int16_t delta;
    uint8_t reserved[2];
};

struct KeyboardData {
    uint8_t  hidCode;
    uint8_t  state;      // 1 = down, 0 = up
    uint8_t  reserved[2];
};

struct ConnectData {
    char uuid[64];
};
#pragma pack(pop)

struct NetTransport {
    std::mutex mutex;
    std::atomic<State> state{State::Disconnected};
    char error[256] = {};
    char ip[64] = "192.168.2.188";
    int port = 16896;
    char uuid[64] = {};

    SOCKET sock = INVALID_SOCKET;
    sockaddr_in addr = {};
    std::atomic<uint32_t> seq{0};
    bool wsaInit = false;

    bool InitWSA() {
        if (wsaInit) return true;
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            snprintf(error, sizeof(error), "WSAStartup failed: %d", WSAGetLastError());
            return false;
        }
        wsaInit = true;
        return true;
    }

    bool CreateSocket() {
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            snprintf(error, sizeof(error), "Socket creation failed: %d", WSAGetLastError());
            return false;
        }
        // Set send/receive timeouts
        DWORD timeout = 500;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
        return true;
    }

    bool SendPacket(uint32_t cmd, const void* data, uint32_t dataLen) {
        if (sock == INVALID_SOCKET) return false;

        uint8_t buf[256] = {};
        PacketHeader* hdr = reinterpret_cast<PacketHeader*>(buf);
        hdr->magic = 0x4D4B;
        hdr->cmd = cmd;
        hdr->seq = seq.fetch_add(1);
        hdr->dataLen = dataLen;

        if (data && dataLen > 0 && dataLen <= sizeof(buf) - sizeof(PacketHeader)) {
            memcpy(buf + sizeof(PacketHeader), data, dataLen);
        }

        int total = static_cast<int>(sizeof(PacketHeader) + dataLen);
        int sent = sendto(sock, reinterpret_cast<const char*>(buf), total, 0,
                          reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        return sent == total;
    }

    bool SendConnect() {
        ConnectData cd = {};
        strncpy(cd.uuid, uuid, sizeof(cd.uuid) - 1);
        if (!SendPacket(CMD_CONNECT, &cd, sizeof(cd))) return false;

        // Wait for ACK
        uint8_t reply[256] = {};
        sockaddr_in from = {};
        int fromLen = sizeof(from);
        int n = recvfrom(sock, reinterpret_cast<char*>(reply), sizeof(reply), 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n < static_cast<int>(sizeof(PacketHeader))) return false;

        PacketHeader* rh = reinterpret_cast<PacketHeader*>(reply);
        return rh->cmd == CMD_CONNECT;
    }

    void Close() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
    }
};

static NetTransport g_net;

} // anonymous namespace

void Connect(const char* ip, int port, const char* uuid) {
    std::lock_guard<std::mutex> lock(g_net.mutex);

    // Disconnect existing
    g_net.Close();
    g_net.error[0] = 0;
    g_net.seq = 0;
    g_net.state = State::Connecting;

    if (ip && ip[0]) strncpy(g_net.ip, ip, sizeof(g_net.ip) - 1);
    if (port > 0) g_net.port = port;
    if (uuid && uuid[0]) strncpy(g_net.uuid, uuid, sizeof(g_net.uuid) - 1);

    if (!g_net.InitWSA()) {
        g_net.state = State::Failed;
        return;
    }

    if (!g_net.CreateSocket()) {
        g_net.state = State::Failed;
        return;
    }

    // Resolve address
    memset(&g_net.addr, 0, sizeof(g_net.addr));
    g_net.addr.sin_family = AF_INET;
    g_net.addr.sin_port = htons(static_cast<uint16_t>(g_net.port));
    if (inet_pton(AF_INET, g_net.ip, &g_net.addr.sin_addr) != 1) {
        snprintf(g_net.error, sizeof(g_net.error), "Invalid IP address: %s", g_net.ip);
        g_net.Close();
        g_net.state = State::Failed;
        return;
    }

    // Send connect/auth handshake
    if (!g_net.SendConnect()) {
        snprintf(g_net.error, sizeof(g_net.error), "KMBox B+ handshake failed at %s:%d", g_net.ip, g_net.port);
        g_net.Close();
        g_net.state = State::Failed;
        return;
    }

    g_net.state = State::Connected;
}

void Disconnect() {
    std::lock_guard<std::mutex> lock(g_net.mutex);
    g_net.Close();
    g_net.state = State::Disconnected;
    g_net.error[0] = 0;
}

void Shutdown() {
    Disconnect();
    if (g_net.wsaInit) {
        WSACleanup();
        g_net.wsaInit = false;
    }
}

State GetState() { return g_net.state; }

void GetError(char* out, size_t size) {
    if (!out || !size) return;
    std::lock_guard<std::mutex> lock(g_net.mutex);
    snprintf(out, size, "%s", g_net.error);
}

const char* StateString() {
    switch (g_net.state) {
        case State::Connected:  return "Connected";
        case State::Connecting: return "Connecting...";
        case State::Failed:     return "Failed";
        default:                return "Disconnected";
    }
}

bool Move(int x, int y) {
    if (g_net.state != State::Connected) return false;
    MouseMoveData md = {};
    md.x = static_cast<int16_t>((std::max)(-32768, (std::min)(32767, x)));
    md.y = static_cast<int16_t>((std::max)(-32768, (std::min)(32767, y)));
    md.relative = 1;
    return g_net.SendPacket(CMD_MOUSE_MOVE, &md, sizeof(md));
}

bool MoveTo(int dx, int dy) {
    // KMBox B+ supports larger relative moves than Makcu's +-127
    // but we still chunk at +-32767 for safety
    while (dx != 0 || dy != 0) {
        int sx = (std::max)(-32767, (std::min)(32767, dx));
        int sy = (std::max)(-32767, (std::min)(32767, dy));
        if (!Move(sx, sy)) return false;
        dx -= sx;
        dy -= sy;
    }
    return true;
}

bool LeftDown() {
    if (g_net.state != State::Connected) return false;
    MouseButtonData bd = {}; bd.state = 1;
    return g_net.SendPacket(CMD_MOUSE_LEFT, &bd, sizeof(bd));
}

bool LeftUp() {
    if (g_net.state != State::Connected) return false;
    MouseButtonData bd = {}; bd.state = 0;
    return g_net.SendPacket(CMD_MOUSE_LEFT, &bd, sizeof(bd));
}

bool LeftClick() {
    if (!LeftDown()) return false;
    Sleep(8 + (GetTickCount64() % 5)); // slight humanization
    return LeftUp();
}

bool RightDown() {
    if (g_net.state != State::Connected) return false;
    MouseButtonData bd = {}; bd.state = 1;
    return g_net.SendPacket(CMD_MOUSE_RIGHT, &bd, sizeof(bd));
}

bool RightUp() {
    if (g_net.state != State::Connected) return false;
    MouseButtonData bd = {}; bd.state = 0;
    return g_net.SendPacket(CMD_MOUSE_RIGHT, &bd, sizeof(bd));
}

bool RightClick() {
    if (!RightDown()) return false;
    Sleep(8 + (GetTickCount64() % 5));
    return RightUp();
}

bool MiddleDown() {
    if (g_net.state != State::Connected) return false;
    MouseButtonData bd = {}; bd.state = 1;
    return g_net.SendPacket(CMD_MOUSE_MIDDLE, &bd, sizeof(bd));
}

bool MiddleUp() {
    if (g_net.state != State::Connected) return false;
    MouseButtonData bd = {}; bd.state = 0;
    return g_net.SendPacket(CMD_MOUSE_MIDDLE, &bd, sizeof(bd));
}

bool MiddleClick() {
    if (!MiddleDown()) return false;
    Sleep(8 + (GetTickCount64() % 5));
    return MiddleUp();
}

bool KeyDown(unsigned int hidCode) {
    if (g_net.state != State::Connected) return false;
    KeyboardData kd = {};
    kd.hidCode = static_cast<uint8_t>(hidCode);
    kd.state = 1;
    return g_net.SendPacket(CMD_KEYBOARD, &kd, sizeof(kd));
}

bool KeyUp(unsigned int hidCode) {
    if (g_net.state != State::Connected) return false;
    KeyboardData kd = {};
    kd.hidCode = static_cast<uint8_t>(hidCode);
    kd.state = 0;
    return g_net.SendPacket(CMD_KEYBOARD, &kd, sizeof(kd));
}

bool KeyPress(unsigned int hidCode) {
    if (!KeyDown(hidCode)) return false;
    Sleep(8 + (GetTickCount64() % 5));
    return KeyUp(hidCode);
}

bool Wheel(int delta) {
    if (g_net.state != State::Connected) return false;
    MouseWheelData wd = {};
    wd.delta = static_cast<int16_t>(delta);
    return g_net.SendPacket(CMD_MOUSE_WHEEL, &wd, sizeof(wd));
}

} // namespace KMBoxNet
