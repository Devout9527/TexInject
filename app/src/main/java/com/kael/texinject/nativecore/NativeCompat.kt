/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of TexInject.
 *
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
package com.kael.texinject.nativecore

import com.kael.texinject.GamePaths
import java.io.File

/**
 * native Python 执行的版本闸门。
 *
 * 3.10 的 libminecraftpe.so 里 Python 相关符号整体换了一批，而
 * tp/FunctionsAddress.cpp 的 ADDR_310 还没补全（setExecLocked 仍为 0）。
 * 在 3.10 上跑 runPythonSource 会按 3.9 的偏移 `base + 0x1351EE70` 跳过去，
 * 落点是个野地址，直接 SIGSEGV（日志里是 NCCrashHandler sig=11、无 Java 栈）。
 *
 * 所以：>= 3.10 一律不执行 native Python（MCP 刷新 / py 脚本面板），
 * 悬浮层其它功能（音乐条、悬浮球、材质注入）完全不受影响。
 */
object NativeCompat {

    /** 诊断开关：存在该文件时强制放行，方便继续二分定位。 */
    private val stageFlag = File("/data/local/tmp/texinject_stage")

    val diagnosticMode: Boolean
        get() = stageFlag.exists()

    /**
     * 当前版本是否允许执行 native Python。
     *
     * 3.10 的 6 个地址里，其余 5 个都按结构比对确认过；第 6 个（setExecLocked）
     * 在 3.9 只是 .bss 里一个默认为 0 的 int，写 false 等于把 0 再写一遍（空操作），
     * 所以 3.10 上直接跳过写，其余照常执行 —— 不再整段拦掉。
     */
    val pythonAllowed: Boolean
        get() = true

    /** 拒绝原因（日志用）。 */
    fun why(): String =
        if (GamePaths.version.isEmpty()) "游戏版本未知"
        else "游戏版本 ${GamePaths.version}"
}
