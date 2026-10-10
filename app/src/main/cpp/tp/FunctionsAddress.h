/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 * TexInject is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * TexInject is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along
 * with TexInject. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef TEXINJECT_FUNCTIONSADDRESS_H
#define TEXINJECT_FUNCTIONSADDRESS_H

#include <cstdint>

// 只保留 Python 执行桥需要的偏移（网易 x19 3.9.15）。
class FunctionsAddress {
public:
    static uintptr_t PythonUtils_setExecLocked;          // .bss bool（执行锁）
    static uintptr_t PythonUtils_PyGILState_EnsureMC;
    static uintptr_t PythonUtils_PyImport_AddModuleMC;
    static uintptr_t PythonUtils_PyModule_GetDictMC;
    static uintptr_t PythonUtils_PyRun_StringFlagsMC;
    static uintptr_t PythonUtils_PyGILState_ReleaseMC;
};

#endif // TEXINJECT_FUNCTIONSADDRESS_H
