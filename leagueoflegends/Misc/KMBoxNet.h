#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace KMBoxNet {

enum class State { Disconnected, Connecting, Connected, Failed };

void Connect(const char* ip, int port, const char* uuid);
void Disconnect();
void Shutdown();
State GetState();
void GetError(char* out, size_t size);

bool Move(int x, int y);
bool MoveTo(int dx, int dy);
bool LeftDown();
bool LeftUp();
bool LeftClick();
bool RightDown();
bool RightUp();
bool RightClick();
bool MiddleDown();
bool MiddleUp();
bool MiddleClick();
bool KeyDown(unsigned int hidCode);
bool KeyUp(unsigned int hidCode);
bool KeyPress(unsigned int hidCode);
bool Wheel(int delta);

const char* StateString();

} // namespace KMBoxNet
