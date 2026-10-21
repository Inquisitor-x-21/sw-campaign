#include "plugin.h"

HMODULE g_module = nullptr;

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_module = module;
            scriptRegister(module, coopScriptMain);
            break;
        case DLL_PROCESS_DETACH:
            scriptUnregister(module);
            break;
        default:
            break;
    }
    return TRUE;
}
