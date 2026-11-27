#pragma once
#include "MakcuTransport.h"
#include "KMBoxNet.h"
#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>

#include <Windows.h>
#include <setupapi.h>
#include <devguid.h>

namespace InputDevice {

enum class DeviceType { Makcu, KMBoxBPlusCOM, KMBoxBPlusNet };
inline DeviceType g_deviceType = DeviceType::KMBoxBPlusCOM; // default to KMBox B+ (serial)

inline std::vector<std::string> ListPorts()
{
    std::vector<std::string> ports;
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return ports;
    SP_DEVINFO_DATA d{}; d.cbSize = sizeof(d);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &d); ++i) {
        char buf[512]; DWORD n = 0;
        if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &d, SPDRP_FRIENDLYNAME,
                NULL, (PBYTE)buf, sizeof(buf), &n) && n > 0) {
            buf[(std::min)((DWORD)(sizeof(buf)-1), n)] = '\0';
            ports.emplace_back(buf);
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return ports;
}

// Makcu (serial) connect
inline void Connect(const char* port = "", int baud = 115200)
{
    g_deviceType = DeviceType::Makcu;
    Makcu::Connect(port, baud, false);
}

// KMBox B+ serial (COM) connect — same km.* commands, no handshake
inline void ConnectKMBoxCOM(const char* port = "", int baud = 115200)
{
    g_deviceType = DeviceType::KMBoxBPlusCOM;
    Makcu::Connect(port, baud, true);
}

// KMBox B+ (network/UDP) connect
inline void ConnectKMBoxNet(const char* ip = "192.168.2.188", int port = 16896, const char* uuid = "")
{
    g_deviceType = DeviceType::KMBoxBPlusNet;
    KMBoxNet::Connect(ip, port, uuid);
}

inline void Disconnect()
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        KMBoxNet::Disconnect();
    else
        Makcu::Disconnect();
}

inline void Shutdown()
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        KMBoxNet::Shutdown();
    else
        Makcu::Shutdown();
}

inline bool IsConnected()
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::GetState() == KMBoxNet::State::Connected;
    return Makcu::GetState() == Makcu::State::Connected;
}

inline const char* StateString()
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::StateString();

    switch (Makcu::GetState()) {
        case Makcu::State::Connected:    return "Connected";
        case Makcu::State::Connecting:   return "Connecting...";
        case Makcu::State::Failed:       return "Failed";
        default:                         return "Disconnected";
    }
}

inline void GetError(char* out, size_t size)
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        KMBoxNet::GetError(out, size);
    else
        Makcu::GetError(out, size);
}

inline bool Move(int x, int y)
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::Move(x, y);

    x = (std::max)(-128, (std::min)(127, x));
    y = (std::max)(-128, (std::min)(127, y));
    char cmd[64];
    int n = snprintf(cmd, sizeof(cmd), "km.move(%d,%d)\r\n", x, y);
    return n > 0 && Makcu::Send(cmd, n, true);
}

inline bool MoveTo(int dx, int dy)
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::MoveTo(dx, dy);

    while (dx != 0 || dy != 0) {
        int sx = (std::max)(-127, (std::min)(127, dx));
        int sy = (std::max)(-127, (std::min)(127, dy));
        if (!Move(sx, sy)) return false;
        dx -= sx;
        dy -= sy;
    }
    return true;
}

inline bool LeftDown()
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::LeftDown();
    static const char cmd[] = "km.left(1)\r\n";
    return Makcu::Send(cmd, sizeof(cmd)-1);
}

inline bool LeftUp()
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::LeftUp();
    static const char cmd[] = "km.left(0)\r\n";
    return Makcu::Send(cmd, sizeof(cmd)-1);
}

inline bool LeftClick()
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::LeftClick();
    LeftDown();
    Sleep(10);
    return LeftUp();
}

inline bool RightDown()
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::RightDown();
    static const char cmd[] = "km.right(1)\r\n";
    return Makcu::Send(cmd, sizeof(cmd)-1);
}

inline bool RightUp()
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::RightUp();
    static const char cmd[] = "km.right(0)\r\n";
    return Makcu::Send(cmd, sizeof(cmd)-1);
}

inline bool RightClick()
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::RightClick();
    RightDown();
    Sleep(10);
    return RightUp();
}

inline bool KeyDown(unsigned int hidCode)
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::KeyDown(hidCode);
    char cmd[64];
    int n = snprintf(cmd, sizeof(cmd), "km.press(%u)\r\n", hidCode);
    return n > 0 && Makcu::Send(cmd, n);
}

inline bool KeyUp(unsigned int hidCode)
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::KeyUp(hidCode);
    char cmd[64];
    int n = snprintf(cmd, sizeof(cmd), "km.release(%u)\r\n", hidCode);
    return n > 0 && Makcu::Send(cmd, n);
}

inline bool KeyPress(unsigned int hidCode)
{
    if (g_deviceType == DeviceType::KMBoxBPlusNet)
        return KMBoxNet::KeyPress(hidCode);
    KeyDown(hidCode);
    Sleep(10);
    return KeyUp(hidCode);
}

}
