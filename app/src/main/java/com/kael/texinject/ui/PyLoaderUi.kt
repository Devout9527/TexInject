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
package com.kael.texinject.ui

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import com.kael.texinject.mcp.McpLoaderSource
import com.kael.texinject.ui.UiTheme
import com.kael.texinject.paths.BuildPaths
import com.kael.texinject.nativecore.NativeCore
import com.kael.texinject.nativecore.NativeLibLoader
import java.io.File

/**
 * 脚本加载器面板。顶部左右切换两类脚本：
 *  - 左「py」 ：pack_netease/scripts/ 下的 .py，点击即在游戏 Python(__main__)里直接执行。
 *  - 右「mcp」：pack_netease/scripts/ 或 scripts/mcp/ 下的 .mcp，点击用内置加载器注册并加载
 *              （load_mcp 逻辑来自 全局加载mcp和py.py，内嵌于 McpLoaderSource）。
 */
object PyLoaderUi {

    fun buildView(activity: Activity): View {
        val ctx = activity.applicationContext
        val density = activity.resources.displayMetrics.density
        fun d(v: Float) = (v * density + .5f).toInt()

        val root = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
        }

        // 顶部左右切换：左 py / 右 mcp
        var showMcp = false

        val pyChip = TextView(activity).apply {
            text = "py"
            gravity = Gravity.CENTER
            textSize = 14f
            typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            setPadding(0, d(8f), 0, d(8f))
            isClickable = true
        }
        val mcpChip = TextView(activity).apply {
            text = "mcp"
            gravity = Gravity.CENTER
            textSize = 14f
            typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
            setPadding(0, d(8f), 0, d(8f))
            isClickable = true
        }
        val switchRow = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            background = GradientDrawable().apply {
                setColor(UiTheme.glassWhite)
                cornerRadius = d(10f).toFloat()
                setStroke(d(1f), UiTheme.glassWhiteBorder)
            }
            setPadding(d(3f), d(3f), d(3f), d(3f))
        }
        switchRow.addView(pyChip, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        switchRow.addView(mcpChip, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply { leftMargin = d(3f) })
        root.addView(switchRow, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { bottomMargin = d(8f) })

        // 说明
        val hint = TextView(activity).apply {
            textSize = 11f
            setTextColor(Color.parseColor("#A9A9A9"))
            setPadding(0, 0, 0, d(8f))
        }
        root.addView(hint)

        // 状态行 + 刷新
        val status = TextView(activity).apply {
            textSize = 11f
            setTextColor(Color.parseColor("#8899A6"))
            setPadding(0, 0, 0, d(6f))
        }
        val refresh = mkButton(activity, "刷新列表", density)
        val topRow = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        topRow.addView(status, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        topRow.addView(refresh, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, d(38f)).apply { leftMargin = d(6f) })
        root.addView(topRow, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))

        val listHost = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        root.addView(listHost, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))

        fun updateChips() {
            fun style(chip: TextView, selected: Boolean) {
                chip.setTextColor(if (selected) Color.WHITE else UiTheme.glassWhiteText)
                chip.background = GradientDrawable().apply {
                    setColor(if (selected) UiTheme.cbHighlight else Color.TRANSPARENT)
                    cornerRadius = d(8f).toFloat()
                }
            }
            style(pyChip, !showMcp)
            style(mcpChip, showMcp)
            hint.text = if (showMcp)
                "把 .mcp 放到 pack_netease/scripts/ 或 scripts/mcp/\n点击即用内置加载器注册并加载"
            else
                "把 .py 放到 pack_netease/scripts/\n点击即在游戏 Python 里执行（可 import 网易 Mod API）"
        }

        fun addEmpty(msg: String) {
            listHost.addView(TextView(activity).apply {
                text = msg
                textSize = 12f
                setTextColor(Color.parseColor("#777777"))
                setPadding(0, d(6f), 0, d(6f))
            })
        }

        fun reload() {
            listHost.removeAllViews()
            val dir = BuildPaths.scriptsDir(ctx)
            status.text = "目录：${dir.absolutePath}"
            if (!showMcp) {
                val pyFiles = dir.listFiles { f -> f.isFile && f.name.endsWith(".py", ignoreCase = true) }
                    ?.sortedBy { it.name.lowercase() } ?: emptyList()
                if (pyFiles.isEmpty()) {
                    addEmpty("（没有 .py）")
                } else {
                    for (f in pyFiles) {
                        val card = mkButton(activity, "▶ ${f.name}", density)
                        card.setOnClickListener { runScript(activity, f, status) }
                        listHost.addView(card, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { bottomMargin = d(6f) })
                    }
                }
            } else {
                val mcpMap = LinkedHashMap<String, Pair<File, String>>()
                // MCP 独立目录（与 icon 同级）：pack_netease/mcp
                scanMcp(com.kael.texinject.paths.BuildPaths.mcpDir(ctx), "", mcpMap, 0)
                // 兼容旧位置
                scanMcp(dir, "", mcpMap, 0)
                scanMcp(File(dir, "mcp"), "", mcpMap, 0)
                val mcpFiles = mcpMap.values.sortedBy { it.second.lowercase() }

                // 一键关闭所有 MCP：反注册所有 system + 清模块；同时把快捷键收起来
                val stopAllBtn = mkButton(activity, "■ 一键关闭所有 MCP", density)
                stopAllBtn.setOnClickListener {
                    status.text = "正在关闭所有 MCP …"
                    Thread({
                        val ok = try { stopAllMcp() } catch (e: Throwable) { false }
                        activity.runOnUiThread {
                            status.text = if (ok) "已关闭所有 MCP" else "关闭失败（看 logcat）"
                            Toast.makeText(activity.applicationContext, if (ok) "已关闭所有 MCP" else "关闭失败", Toast.LENGTH_SHORT).show()
                            HotkeyStore.activeMcp = null
                            HotkeyLayer.rebuild(activity)
                        }
                    }, "mcp-stop-all").start()
                }
                listHost.addView(stopAllBtn, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { bottomMargin = d(10f) })

                if (mcpFiles.isEmpty()) {
                    addEmpty("（没有 .mcp，放到 scripts/ 或 scripts/mcp/，可放子目录）")
                } else {
                    for ((f, label) in mcpFiles) {
                        val card = mkButton(activity, "▸ $label", density)
                        card.setOnClickListener { runMcp(activity, f, status) }
                        listHost.addView(card, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { bottomMargin = d(6f) })
                    }
                }
            }
        }

        pyChip.setOnClickListener { showMcp = false; updateChips(); reload() }
        mcpChip.setOnClickListener { showMcp = true; updateChips(); reload() }
        refresh.setOnClickListener { reload() }

        updateChips()

        // 后台加载原生库（首次）
        Thread({
            val loaded = NativeLibLoader.load(BuildPaths::class.java)
            activity.runOnUiThread {
                if (!loaded) {
                    status.text = "原生库加载失败，无法执行脚本"
                    Toast.makeText(ctx, "原生库加载失败", Toast.LENGTH_SHORT).show()
                }
                reload()
            }
        }, "py-loader-init").start()

        reload()
        return root
    }

    /**
     * 递归扫描 .mcp。out 的 key = 绝对路径，value = (文件, 相对 base 的标签)。
     * 标签让二级目录自动带前缀，例如 scripts/mcp/combat/a.mcp -> "combat/a.mcp"。
     */
    private fun scanMcp(base: File, rel: String, out: MutableMap<String, Pair<File, String>>, depth: Int) {
        if (depth > 4) return
        val children = base.listFiles() ?: return
        for (f in children) {
            val r = if (rel.isEmpty()) f.name else "$rel/${f.name}"
            if (f.isFile && f.name.endsWith(".mcp", ignoreCase = true)) {
                out[f.absolutePath] = f to r
            } else if (f.isDirectory && !f.name.startsWith(".")) {
                scanMcp(f, r, out, depth + 1)
            }
        }
    }

    private fun runScript(activity: Activity, file: File, status: TextView) {
        status.text = "执行中：${file.name} …"
        // 移植 BP_mcp 的 .py 加载：走 z1yrScripts.api.py_loader.load_py
        // （fop.new_module 独立模块命名空间 + 脚本目录进 sys.path → 脚本内可 import 同目录模块）
        // 拿不到 py_loader 时，兜底在 __main__ 里 exec。
        val fp = pythonStr(file.absolutePath)
        val snippet = buildString {
            append("_z1yr_ok = False\n")
            append("try:\n")
            append("    from z1yrScripts.api.py_loader import load_py as _z1yr_lp\n")
            append("    _z1yr_lp(").append(fp).append(")\n")
            append("    _z1yr_ok = True\n")
            append("except Exception:\n")
            append("    _z1yr_ok = False\n")
            append("if not _z1yr_ok:\n")
            append("    try:\n")
            append("        with open(").append(fp).append(", 'r') as _z1yr_f:\n")
            append("            _z1yr_src = _z1yr_f.read()\n")
            append("        exec(compile(_z1yr_src, ").append(fp).append(", 'exec'), globals(), globals())\n")
            append("    except Exception:\n")
            append("        pass\n")
        }
        Thread({
            val ok = try {
                NativeCore.runPythonSource(snippet)
            } catch (e: Throwable) {
                activity.runOnUiThread { status.text = "异常：${e.message}" }
                false
            }
            activity.runOnUiThread {
                status.text = if (ok) "✓ 执行完成：${file.name}" else "✗ 执行失败：${file.name}（看 logcat: TexInjectNative）"
                if (ok) Toast.makeText(activity.applicationContext, "已执行：${file.name}", Toast.LENGTH_SHORT).show()
            }
        }, "py-run").start()
    }

    /** 关闭所有 MCP：复用 McpLoaderSource 里已有的反注册能力。 */
    private fun stopAllMcp(): Boolean {
        val snippet = McpLoaderSource.CODE + "\n" +
            "def _z1yr_stop_all():\n" +
            "    _n = 0\n" +
            "    try:\n" +
            "        _sr = _get_client_system_register()\n" +
            "    except Exception:\n" +
            "        _sr = None\n" +
            "    if _sr is not None:\n" +
            "        for _a in ('systemInstances', 'newSystemInstances'):\n" +
            "            _r = getattr(_sr, _a, None)\n" +
            "            if not _r:\n" +
            "                continue\n" +
            "            for _k in list(_r.keys()):\n" +
            "                try:\n" +
            "                    _ns, _, _sn = _k.partition(':')\n" +
            "                    _unregister_system(_ns, _sn)\n" +
            "                    _n += 1\n" +
            "                except Exception:\n" +
            "                    pass\n" +
            "    for _nm in [x for x in list(sys.modules)\n" +
            "                if x.startswith('z1yrScripts') or x.startswith('mcp_')\n" +
            "                or x.startswith('_mcp_')]:\n" +
            "        try:\n" +
            "            del sys.modules[_nm]\n" +
            "        except Exception:\n" +
            "            pass\n" +
            "    try:\n" +
            "        import gc\n" +
            "        gc.collect()\n" +
            "    except Exception:\n" +
            "        pass\n" +
            "    return _n\n" +
            "try:\n" +
            "    _safe_notify_message('\u00a7a[TexInject] \u5df2\u5173\u95ed\u6240\u6709 MCP')\n" +
            "except Exception:\n" +
            "    pass\n" +
            "_z1yr_stop_all()\n"
        return NativeCore.runPythonSource(snippet)
    }

    private fun runMcp(activity: Activity, file: File, status: TextView) {
        status.text = "提交中：${file.name} …"
        Thread({
            // MCP 注册(add_mod_mcp / RegisterSystem)必须在**游戏线程**跑：
            // 用游戏 timer 把 load_mcp 排到游戏线程执行（拿不到就同步跑兜底）。
            // 结果用游戏内左下角提示显示。
            val snippet = McpLoaderSource.CODE + "\n" +
                "def _z1yr_do_load():\n" +
                "    _r = load_mcp(" + pythonStr(file.absolutePath) + ")\n" +
                "    try:\n" +
                "        _notify(_r)\n" +
                "    except Exception:\n" +
                "        pass\n" +
                "try:\n" +
                "    _z1yr_gc = clientApi.GetEngineCompFactory().CreateGame(clientApi.GetLevelId())\n" +
                "    _z1yr_gc.AddTimer(0.05, _z1yr_do_load)\n" +
                "except Exception:\n" +
                "    _z1yr_do_load()\n"
            val ok = try {
                NativeCore.runPythonSource(snippet)
            } catch (e: Throwable) {
                activity.runOnUiThread { status.text = "异常：${e.message}" }
                false
            }
            activity.runOnUiThread {
                status.text = if (ok) "已提交加载：${file.name}（结果看游戏内提示）"
                else "✗ 提交失败：${file.name}（看 logcat: TexInjectNative）"
                if (ok) {
                    Toast.makeText(activity.applicationContext, "已提交 MCP：${file.name}", Toast.LENGTH_SHORT).show()
                    // 快捷键/胶囊跟着这个 MCP 走：加载后才显示
                    HotkeyStore.activeMcp = file
                    HotkeyLayer.rebuild(activity)
                }
            }
        }, "mcp-run").start()
    }

    // 生成 Python 字符串字面量（单引号，转义反斜杠和引号）
    private fun pythonStr(s: String): String {
        val esc = s.replace("\\", "\\\\").replace("'", "\\'")
        return "'" + esc + "'"
    }

    private fun mkButton(activity: Activity, text: String, density: Float): Button = Button(activity).apply {
        this.text = text
        textSize = 13f
        typeface = Typeface.create("sans-serif-medium", Typeface.BOLD)
        setTextColor(UiTheme.glassWhiteText)
        val p = (density * 14f + .5f).toInt()
        val q = (density * 10f + .5f).toInt()
        setPadding(p, q, p, q)
        background = GradientDrawable().apply {
            setColor(UiTheme.glassWhite)
            cornerRadius = (10f * density)
            setStroke((1f * density).toInt(), UiTheme.glassWhiteBorder)
        }
    }
}