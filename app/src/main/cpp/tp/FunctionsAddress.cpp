/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "FunctionsAddress.h"
#include <cstdio>
#include <cstring>
#include <mutex>

// ── 3.9.15.297907（com.netease.x19）原始地址 ──────────────────────────
static constexpr uintptr_t ADDR_3915[] = {
    0x1351EE70,  // setExecLocked
    0x12356D88,  // PyGILState_Ensure
    0x122D63F0,  // PyImport_AddModule
    0x1224ABA0,  // PyModule_GetDict
    0x123586A4,  // PyRun_StringFlags
    0x12356E20,  // PyGILState_Release
};

// ── 3.10 正式版（3.10.x）XREF 定位 ───────────────────────────────
//    方法：以签名扫描命中的 PyEval_EvalCode 为锚点做交叉引用 + 与 3.9.15
//    反汇编逐条结构比对（寄存器分配、指令序列、函数内偏移全部一一对应）。
static constexpr uintptr_t ADDR_310[] = {
    0x00000000,  // setExecLocked       ⚠️ 数据地址（段间间隙），无法 XREF，保持 0
    0x119C0178,  // PyGILState_Ensure   ✅ 结构比对：距函数头 +0x34 处计数 +1，与 3.9.15 完全一致
    0x11940A68,  // PyImport_AddModule  ✅ 特征链唯一命中，且被 0x11940B58 调用
    0x118BE774,  // PyModule_GetDict    ✅ 相对 PyEval_EvalCode 偏移 -0x5D7BC，与 3.9.15 一致
    0x119C1A94,  // PyRun_StringFlags   ✅ XREF + 结构比对（5 参数搬运字节级相同）
    0x119C0210,  // PyGILState_Release  ✅ 结构比对：距函数头 +0x48 处计数 -1，与 3.9.15 完全一致
};

// 参考锚点（同一构建内，供后续版本重新定位用）：
//   PyEval_EvalCode                 0x1191BF30
//   PyArena_New                     0x119BF368
//   PyArena_Free                    0x119BF3FC
//   PyParser_ASTFromStringObject    0x119C2848
//   run_mod                         0x1192E4FC
uintptr_t FunctionsAddress::PythonUtils_setExecLocked        = 0;
uintptr_t FunctionsAddress::PythonUtils_PyGILState_EnsureMC  = 0;
uintptr_t FunctionsAddress::PythonUtils_PyImport_AddModuleMC = 0;
uintptr_t FunctionsAddress::PythonUtils_PyModule_GetDictMC   = 0;
uintptr_t FunctionsAddress::PythonUtils_PyRun_StringFlagsMC  = 0;
uintptr_t FunctionsAddress::PythonUtils_PyGILState_ReleaseMC = 0;

static std::mutex g_detectMutex;
static bool g_detected = false;

static bool isTestBuild() {
    FILE* f = fopen("/proc/self/cmdline", "r");
    if (!f) return false;
    char buf[256] = {0};
    if (fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return strstr(buf, "mctest") != nullptr;
    }
    fclose(f);
    return false;
}

void FunctionsAddress::detect() {
    std::lock_guard<std::mutex> lock(g_detectMutex);
    if (g_detected) return;
    g_detected = true;

    const uintptr_t* a = isTestBuild() ? ADDR_310 : ADDR_3915;
    PythonUtils_setExecLocked        = a[0];
    PythonUtils_PyGILState_EnsureMC  = a[1];
    PythonUtils_PyImport_AddModuleMC = a[2];
    PythonUtils_PyModule_GetDictMC   = a[3];
    PythonUtils_PyRun_StringFlagsMC  = a[4];
    PythonUtils_PyGILState_ReleaseMC = a[5];
}
