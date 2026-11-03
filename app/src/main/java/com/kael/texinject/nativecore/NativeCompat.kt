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
 * native Python 执行的放行判断。
 */
object NativeCompat {

    /** 诊断开关：存在该文件时进入诊断模式。 */
    private val stageFlag = File("/data/local/tmp/texinject_stage")

    val diagnosticMode: Boolean
        get() = stageFlag.exists()

    /** 地址表由 FunctionsAddress::detect() 按 build-id 选择，这里一律放行。 */
    val pythonAllowed: Boolean
        get() = true

    /** 日志用。 */
    fun why(): String =
        if (GamePaths.version.isEmpty()) "游戏版本未知"
        else "游戏版本 ${GamePaths.version}"
}
