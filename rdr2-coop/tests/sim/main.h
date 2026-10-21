// TEST DOUBLE for the Script Hook RDR2 SDK's main.h. Lets the plugin compile on
// Linux and call into a small simulated world (tests/sim/fake_game.cpp)
// instead of the real game. The real main.h pulls in <windows.h>, so the few
// Windows calls plugin/script.cpp makes are declared here too
// (tests/sim/fake_windows.cpp).
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

// --- <windows.h> stand-ins
typedef uint16_t WORD;
typedef int16_t SHORT;
typedef void* HWND;
#define MAX_PATH 260

struct SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};

SHORT GetAsyncKeyState(int vk);
HWND GetForegroundWindow();
DWORD GetWindowThreadProcessId(HWND window, DWORD* processId);
unsigned long GetCurrentProcessId();  // DWORD on Windows; unsigned long keeps the plugin's %lu right
DWORD GetModuleFileNameA(HMODULE module, char* path, DWORD size);
void GetLocalTime(SYSTEMTIME* time);
