#pragma once

// Only the Win32 types and clock used by the real keyboard.cpp; game-free tests.
#include <cstdint>
#include <cstring>

using DWORD = std::uint32_t;
using WORD = std::uint16_t;
using BYTE = std::uint8_t;
using BOOL = int;
using std::memset;

DWORD GetTickCount();
