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
#include "FunctionsAddress.h"

// 网易 x19 3.9.15.297907 (high)。Python 区整体 +0x1A90（相对 3.9.5）。
uintptr_t FunctionsAddress::PythonUtils_setExecLocked       = 0x1351EE70;
uintptr_t FunctionsAddress::PythonUtils_PyGILState_EnsureMC = 0x12356D88;
uintptr_t FunctionsAddress::PythonUtils_PyImport_AddModuleMC = 0x122D63F0;
uintptr_t FunctionsAddress::PythonUtils_PyModule_GetDictMC  = 0x1224ABA0;
uintptr_t FunctionsAddress::PythonUtils_PyRun_StringFlagsMC = 0x123586A4;
uintptr_t FunctionsAddress::PythonUtils_PyGILState_ReleaseMC = 0x12356E20;
