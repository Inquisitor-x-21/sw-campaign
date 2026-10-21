#pragma once

#include "main.h"  // Script Hook RDR2 SDK; pulls in <windows.h>

extern HMODULE g_module;

// Runs forever on Script Hook's script fiber; registered from DllMain.
void coopScriptMain();
