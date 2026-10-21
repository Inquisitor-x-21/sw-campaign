// COMPILE-CHECK STAND-IN, NOT THE REAL SDK.
// Declares only the Script Hook RDR2 functions this project calls, so the
// plugin can be syntax-checked on machines without the SDK
// (cmake -DRDR2COOP_PLUGIN_COMPILE_CHECK=ON). Real builds use the SDK's own
// main.h and ScriptHookRDR2.lib from sdk/.
#pragma once

#include <windows.h>

#define IMPORT __declspec(dllimport)

IMPORT void scriptWait(DWORD time);
IMPORT void scriptRegister(HMODULE module, void (*LP_SCRIPT_MAIN)());
IMPORT void scriptUnregister(HMODULE module);

IMPORT void nativeInit(UINT64 hash);
IMPORT void nativePush64(UINT64 val);
IMPORT PUINT64 nativeCall();
