// TEST DOUBLE for the Script Hook RDR2 SDK's main.h. Lets plugin/game.cpp and
// plugin/avatar.cpp compile on Linux and call into a small simulated world
// (tests/sim/fake_game.cpp) instead of the real game.
#pragma once

#include <cstdint>

typedef int BOOL;
typedef uint32_t DWORD;
typedef uint64_t UINT64;
typedef UINT64* PUINT64;
typedef void* HMODULE;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

void scriptWait(DWORD time);
void nativeInit(UINT64 hash);
void nativePush64(UINT64 val);
PUINT64 nativeCall();
