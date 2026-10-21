// Test controls for tests/sim/fake_windows.cpp: the Windows calls
// plugin/script.cpp makes, and a lockstep stand-in for Script Hook's script
// fiber.
#pragma once

#include <string>

namespace sim {

void setKeyDown(int vk, bool down);
void setModulePath(const std::string& path);  // what GetModuleFileNameA reports

// Starts scriptMain on its own thread and returns once it reaches its first
// scriptWait(). Calling it again is Script Hook restarting the script: the old
// thread stays parked in scriptWait() for good, like an abandoned fiber.
void startScript(void (*scriptMain)());
// Lets the script run until its next scriptWait(). Script and test never run
// at the same time, so game calls never overlap.
void runScriptFrame();

}  // namespace sim
