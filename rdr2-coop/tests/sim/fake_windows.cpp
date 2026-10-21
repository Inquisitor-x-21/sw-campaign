#include "fake_windows.h"

#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <mutex>
#include <set>
#include <thread>

#include "main.h"

namespace sim {
namespace {

std::set<int> g_keys;
std::string g_modulePath = "RDR2Coop.asi";

std::mutex g_mutex;
std::condition_variable g_cv;
int g_nextScript = 0;
int g_current = 0;          // the script thread that may run
bool g_scriptTurn = false;  // true while it runs a frame
thread_local int t_scriptId = 0;

void waitForTurn(std::unique_lock<std::mutex>& lock) {
    g_cv.wait(lock, [] { return g_scriptTurn && g_current == t_scriptId; });
}

}  // namespace

void setKeyDown(int vk, bool down) {
    if (down) {
        g_keys.insert(vk);
    } else {
        g_keys.erase(vk);
    }
}

void setModulePath(const std::string& path) { g_modulePath = path; }

void startScript(void (*scriptMain)()) {
    std::unique_lock<std::mutex> lock(g_mutex);
    const int id = ++g_nextScript;
    g_current = id;
    g_scriptTurn = true;
    std::thread([scriptMain, id] {
        {
            std::unique_lock<std::mutex> own(g_mutex);
            t_scriptId = id;
            waitForTurn(own);
        }
        scriptMain();
    }).detach();
    g_cv.wait(lock, [] { return !g_scriptTurn; });
}

void runScriptFrame() {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_scriptTurn = true;
    g_cv.notify_all();
    g_cv.wait(lock, [] { return !g_scriptTurn; });
}

}  // namespace sim

void scriptWait(DWORD) {
    std::unique_lock<std::mutex> lock(sim::g_mutex);
    sim::g_scriptTurn = false;
    sim::g_cv.notify_all();
    sim::waitForTurn(lock);
}

SHORT GetAsyncKeyState(int vk) { return sim::g_keys.count(vk) != 0 ? static_cast<SHORT>(0x8000) : 0; }

HWND GetForegroundWindow() { return reinterpret_cast<HWND>(1); }

DWORD GetWindowThreadProcessId(HWND, DWORD* processId) {
    if (processId != nullptr) *processId = static_cast<DWORD>(GetCurrentProcessId());
    return 1;
}

unsigned long GetCurrentProcessId() { return static_cast<unsigned long>(getpid()); }

DWORD GetModuleFileNameA(HMODULE, char* path, DWORD size) {
    if (size == 0) return 0;
    const size_t n = std::min<size_t>(sim::g_modulePath.size(), size - 1);
    std::memcpy(path, sim::g_modulePath.data(), n);
    path[n] = '\0';
    return static_cast<DWORD>(n);
}

void GetLocalTime(SYSTEMTIME* t) {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    *t = SYSTEMTIME{};
    t->wYear = static_cast<WORD>(tm.tm_year + 1900);
    t->wMonth = static_cast<WORD>(tm.tm_mon + 1);
    t->wDay = static_cast<WORD>(tm.tm_mday);
    t->wHour = static_cast<WORD>(tm.tm_hour);
    t->wMinute = static_cast<WORD>(tm.tm_min);
    t->wSecond = static_cast<WORD>(tm.tm_sec);
}
