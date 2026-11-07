#pragma once
#include <cstdint>

struct FunctionsAddress {
    static uintptr_t PythonUtils_setExecLocked;
    static uintptr_t PythonUtils_PyGILState_EnsureMC;
    static uintptr_t PythonUtils_PyImport_AddModuleMC;
    static uintptr_t PythonUtils_PyModule_GetDictMC;
    static uintptr_t PythonUtils_PyRun_StringFlagsMC;
    static uintptr_t PythonUtils_PyGILState_ReleaseMC;

    static void detect();

    static void setVersion(const char* v);

    static const char* detectReason();

    static uintptr_t moduleBase();

    static bool pyTrace();
};
