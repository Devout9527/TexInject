package com.kael.texinject.nativecore

import com.kael.texinject.GamePaths
import java.io.File

object NativeCompat {

    private val stageFlag = File("/data/local/tmp/texinject_stage")

    val diagnosticMode: Boolean
        get() = stageFlag.exists()

    val pythonAllowed: Boolean
        get() = true

    fun why(): String =
        if (GamePaths.version.isEmpty()) "游戏版本未知"
        else "游戏版本 ${GamePaths.version}"
}
