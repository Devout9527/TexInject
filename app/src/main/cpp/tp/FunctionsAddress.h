/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once
#include <cstdint>

/**
 * Python C API 函数在 libminecraftpe.so 中的偏移。
 * detect() 按 build-id 选表，首次访问懒触发（线程安全）。
 */
struct FunctionsAddress {
    static uintptr_t PythonUtils_setExecLocked;
    static uintptr_t PythonUtils_PyGILState_EnsureMC;
    static uintptr_t PythonUtils_PyImport_AddModuleMC;
    static uintptr_t PythonUtils_PyModule_GetDictMC;
    static uintptr_t PythonUtils_PyRun_StringFlagsMC;
    static uintptr_t PythonUtils_PyGILState_ReleaseMC;

    /** 按 libminecraftpe.so 的 build-id 选择地址表。线程安全，可重复调用。 */
    static void detect();

    /** 由 Kotlin 层传入游戏 versionName（如 3.9.15.297907 / 3.10.0.300878）。 */
    static void setVersion(const char* v);

    /** detect() 的结论，用于日志诊断。 */
    static const char* detectReason();

    /** libminecraftpe.so 的 load bias；失败返回 0。 */
    static uintptr_t moduleBase();

    /** /data/local/tmp/texinject_py_trace 存在时输出逐步日志。 */
    static bool pyTrace();
};
