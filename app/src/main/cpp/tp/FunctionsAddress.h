/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once
#include <cstdint>

/**
 * Python C API 函数在 libminecraftpe.so 中的地址。
 *
 * 两个版本：
 *   - 3.9.15（com.netease.x19）     — 原始硬编码地址
 *   - 3.10  （com.netease.mctest）   — 签名扫描得到（4/6 唯一命中）
 *
 * 运行时通过 detect() 自动选择：读 /proc/self/cmdline 判断包名。
 * detect() 在首次访问时懒触发（线程安全）。
 */
struct FunctionsAddress {
    static uintptr_t PythonUtils_setExecLocked;
    static uintptr_t PythonUtils_PyGILState_EnsureMC;
    static uintptr_t PythonUtils_PyImport_AddModuleMC;
    static uintptr_t PythonUtils_PyModule_GetDictMC;
    static uintptr_t PythonUtils_PyRun_StringFlagsMC;
    static uintptr_t PythonUtils_PyGILState_ReleaseMC;

    /** 检测当前进程包名，选择对应地址表。线程安全，可重复调用。 */
    static void detect();
};
