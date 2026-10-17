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

import android.content.Context
import com.kael.texinject.paths.BuildPaths
import com.kael.texinject.nativecore.NativeCore
import java.io.File

/** 一个 MCP 模块（来自 z1yr 系统的 ModuleManager）。cn=中文名, cnCategory=中文分类。 */
data class McpModule(
    val name: String,
    val category: String,
    val enabled: Boolean,
    val cn: String = "",
    val cnCategory: String = ""
) {
    val display: String get() = cn.ifEmpty { name }
    val categoryDisplay: String get() = cnCategory.ifEmpty { category }
}

/**
 * 和游戏里的 MCP 模块系统通信。
 *
 * 原理：MCP 跑在游戏 Python 里，直接
 *   api.GetSystem('z1yr','Z1yrClientSystem')._manager
 * 就能拿到 ModuleManager → get_all() 列模块、get(name).toggle() 开关。
 * 安卓侧用 NativeCore.runPythonSource 执行，结果写 mcp_modules.txt 再读回。
 */
object McpBridge {
    private const val NS = "z1yr"
    private const val SYS = "Z1yrClientSystem"

    private fun file(c: Context) = File(BuildPaths.scriptsDir(c), "mcp_modules.txt")

    private fun q(s: String) = "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"

    /**
     * Python 前奏：稳健地拿到 MCP 的系统对象 + ClickGui 模块。
     * 系统名/模块路径都不是写死单点，而是候选 + 兜底扫描，
     * 这样同一个源码改编出来的 MCP（改名/换路径）也能用。
     */
    private fun pyPrelude(): String = buildString {
        append("import mod.client.extraClientApi as _api\n")
        append("def _get_sys():\n")
        append("    for _ns, _nm in [('z1yr','Z1yrClientSystem'),('z1yr','Z1yrSystem'),('z1yr','Z1yrClient'),('z1yrServer','Z1yrClientSystem')]:\n")
        append("        try:\n")
        append("            _s0 = _api.GetSystem(_ns, _nm)\n")
        append("            if _s0 is not None and getattr(_s0, '_manager', None) is not None:\n")
        append("                return _s0\n")
        append("        except Exception:\n")
        append("            pass\n")
        append("    try:\n")
        append("        import sys as _sysmod\n")
        append("        for _mn in list(_sysmod.modules.keys()):\n")
        append("            _mo = _sysmod.modules.get(_mn)\n")
        append("            if _mo is None: continue\n")
        append("            for _an in dir(_mo):\n")
        append("                if 'System' not in _an: continue\n")
        append("                _ob = getattr(_mo, _an, None)\n")
        append("                _mg = getattr(_ob, '_manager', None)\n")
        append("                if _mg is not None and hasattr(_mg, 'get_all'):\n")
        append("                    return _ob\n")
        append("    except Exception:\n")
        append("        pass\n")
        append("    return None\n")
        append("def _load_cg():\n")
        append("    try:\n")
        append("        import importlib\n")
        append("        for _pth in ('z1yrScripts.uiScript.ClickGui','Scripts.uiScript.ClickGui','uiScript.ClickGui'):\n")
        append("            try:\n")
        append("                return importlib.import_module(_pth)\n")
        append("            except Exception:\n")
        append("                pass\n")
        append("    except Exception:\n")
        append("        pass\n")
        append("    return None\n")
        // 内置中文映射（不依赖 MCP 版本；MCP 自带的会覆盖）
        append("_BUILTIN_CN = {").append(CN_FEATURES_LIT).append("}\n")
        append("_BUILTIN_CAT = {").append(CN_CATEGORIES_LIT).append("}\n")
        append("_BUILTIN_CP = {").append(CN_PARAMS_LIT).append("}\n")
        append("_BUILTIN_CO = {").append(CN_OPTIONS_LIT).append("}\n")
        append("def _merge_cn(_builtin, _mcp):\n")
        append("    _out = {}\n")
        append("    try:\n")
        append("        _out.update(_builtin)\n")
        append("    except Exception: pass\n")
        append("    try:\n")
        append("        for _k, _v in (_mcp or {}).items():\n")
        append("            try:\n")
        append("                if isinstance(_v, unicode): _v = _v.encode('utf-8')\n")
        append("            except Exception: pass\n")
        append("            _out[_k] = _v\n")
        append("    except Exception: pass\n")
        append("    return _out\n")
    }

    /** 跑一段 Python 把模块列表写到文件。 */
    fun refresh(context: Context) {
        val path = file(context).absolutePath
        val code = buildString {
            append(pyPrelude())
            append("_p = ").append(q(path)).append("\n")
            append("_CG = _load_cg()\n")
            append("_G = {_d[0]: _d[1] for _d in getattr(_CG, 'FEATURE_DEFS', [])} if _CG is not None else {}\n")
            append("_CN = _merge_cn(_BUILTIN_CN, getattr(_CG, 'CHINESE_FEATURES', {}) if _CG is not None else {})\n")
            append("_CAT = _merge_cn(_BUILTIN_CAT, getattr(_CG, 'CHINESE_CATEGORIES', {}) if _CG is not None else {})\n")
            append("try:\n")
            append("    _s = _get_sys()\n")
            append("    _m = getattr(_s, '_manager', None)\n")
            append("    _o = []\n")
            append("    if _m is not None:\n")
            append("        for _mod in _m.get_all():\n")
            append("            _n = getattr(_mod, 'name', '?')\n")
            append("            _c = _G.get(_n, getattr(_mod, 'category', ''))\n")
            append("            _o.append('%s|%s|%s|%s|%s' % (_n, _c, ")
            append("'1' if getattr(_mod, 'enabled', False) else '0', ")
            append("_CN.get(_n, ''), _CAT.get(_c, '')))\n")
            append("    else:\n")
            append("        _o.append('!ERR|_manager 不存在（MCP 没加载？）')\n")
            append("    open(_p, 'wb').write('\\n'.join(_o))\n")
            append("except Exception as _e:\n")
            append("    try:\n")
            append("        open(_p, 'wb').write('!ERR|%s' % _e)\n")
            append("    except Exception:\n")
            append("        pass\n")
        }
        try {
            NativeCore.runPythonSource(code)
        } catch (e: Throwable) {
        }
    }

    /** 读模块列表（读不到返回空）。 */
    fun load(context: Context): List<McpModule> {
        val f = file(context)
        if (!f.isFile) return emptyList()
        val out = mutableListOf<McpModule>()
        try {
            for (line in f.readText().split('\n')) {
                val p = line.trim().split('|')
                if (p.size >= 3 && p[0] != "!ERR") {
                    out.add(McpModule(
                        p[0], p[1], p[2] == "1",
                        if (p.size >= 4) p[3] else "",
                        if (p.size >= 5) p[4] else ""
                    ))
                }
            }
        } catch (e: Throwable) {
        }
        return out
    }

    /** 读取失败原因（没有返回 null）。 */
    fun lastError(context: Context): String? {
        val f = file(context)
        if (!f.isFile) return "还没刷新过（点「刷新模块」）"
        return try {
            f.readText().split('\n').firstOrNull { it.startsWith("!ERR|") }?.removePrefix("!ERR|")
        } catch (e: Throwable) {
            null
        }
    }

    /** 开关某个模块。 */
    fun toggle(context: Context, name: String) {
        // 复刻 ClickGui._toggleFeature 的特判：部分模块不是 .toggle()，而是动作方法。
        // 且 on_enable 直接调引擎组件（如相机）的模块必须在客户端线程跑，
        // runPythonSource 本身在 Java 线程执行 -> 用 AddTimer(0,...) 投到客户端 tick。
        val dispatch = buildString {
            append("def _t(_n=").append(q(name)).append("):\n")
            append("    try:\n")
            append("        _s = _get_sys()\n")
            append("        _m = getattr(_s, '_manager', None)\n")
            append("        if _m is None: return\n")
            append("        if _n == 'Mount':\n")
            append("            _mod = _m.get('mount')\n")
            append("            if _mod: _mod._summon()\n")
            append("        elif _n == 'KBChange':\n")
            append("            _mod = _m.get('kbchange')\n")
            append("            if _mod: _mod.trigger()\n")
            append("        elif _n == 'List':\n")
            append("            _mod = _m.get('list')\n")
            append("            if _mod: _mod._show()\n")
            append("        elif _n == 'GMPanel':\n")
            append("            _mod = _m.get('gmpanel')\n")
            append("            if _mod: _mod._open_panel()\n")
            append("        elif _n == 'Whitelist':\n")
            append("            _mod = _m.get('whitelist')\n")
            append("            if _mod: _mod.on_key_action()\n")
            append("        elif _n == 'Debug':\n")
            append("            _s._showDebug()\n")
            append("        elif _n == 'Notification':\n")
            append("            _s.notification = not _s.notification\n")
            append("        else:\n")
            append("            _mod = _m.get(_n)\n")
            append("            if _mod: _mod.toggle()\n")
            append("    except Exception:\n")
            append("        pass\n")
            append("_api.GetEngineCompFactory().CreateGame(_api.GetLevelId()).AddTimer(0.0, _t)\n")
        }
        runPython(
            pyPrelude() + dispatch
        )
    }

    // ---------------- 功能配置（移植 BP_mcp 的 FEATURE_PARAMS/TOGGLES） ----------------

    /** 一条配置项。kind: slider / toggle / dropdown。 */
    data class McpConfigItem(
        val label: String,
        val attr: String,
        val kind: String,
        val min: Float = 0f,
        val max: Float = 0f,
        val step: Float = 1f,
        val options: List<String> = emptyList(),
        val value: String = "",
        val optionLabels: List<String> = emptyList()
    )

    private fun configFile(c: Context) = File(BuildPaths.scriptsDir(c), "mcp_config.txt")

    /** 把某模块的配置定义（表 + 当前值）跑 Python 写到文件。name = 模块名(如 KillAura)。 */
    fun refreshConfig(context: Context, name: String) {
        val path = configFile(context).absolutePath
        val code = buildString {
            append(pyPrelude())
            append("_p = ").append(q(path)).append("\n")
            append("_name = ").append(q(name)).append("\n")
            append("_CG = _load_cg()\n")
            append("_P = getattr(_CG, 'FEATURE_PARAMS', {}) if _CG is not None else {}\n")
            append("_T = getattr(_CG, 'FEATURE_TOGGLES', {}) if _CG is not None else {}\n")
            append("_CP = _merge_cn(_BUILTIN_CP, getattr(_CG, 'CHINESE_PARAMS', {}) if _CG is not None else {})\n")
            append("_CO = _merge_cn(_BUILTIN_CO, getattr(_CG, 'CHINESE_OPTIONS', {}) if _CG is not None else {})\n")
            append("_o = []\n")
            append("try:\n")
            append("    _s = _get_sys()\n")
            append("    _mod = _s._manager.get(_name)\n")
            append("    def _g(a):\n")
            append("        o = _mod\n")
            append("        for p in str(a).split('.'):\n")
            append("            o = getattr(o, p, None)\n")
            append("            if o is None: return None\n")
            append("        return o\n")
            append("    def _skip(a):\n")
            append("        a = str(a).lower()\n")
            append("        return a == '_shortcut' or 'bind' in a\n")
            append("    for it in _P.get(_name, []):\n")
            append("        try:\n")
            append("            lab, attr, mn, mx, st = it[0], it[1], it[2], it[3], it[4]\n")
            append("            if _skip(attr): continue\n")
            append("            cur = _g(attr)\n")
            append("            _o.append('P|%s|%s|%s|%s|%s|%s' % (_CP.get(lab, lab), attr, mn, mx, st, '' if cur is None else cur))\n")
            append("        except Exception: pass\n")
            append("    for it in _T.get(_name, []):\n")
            append("        try:\n")
            append("            lab, attr = it[0], it[1]\n")
            append("            if _skip(attr): continue\n")
            append("            opts = it[2] if len(it) > 2 else None\n")
            append("            cur = _g(attr)\n")
            append("            if opts:\n")
            append("                _pairs = ','.join(['%s~%s' % (str(x), _CO.get(str(x), str(x))) for x in opts])\n")
            append("                _o.append('D|%s|%s|%s|%s' % (_CP.get(lab, lab), attr, _pairs, '' if cur is None else cur))\n")
            append("            else:\n")
            append("                _o.append('B|%s|%s|%s' % (_CP.get(lab, lab), attr, '1' if cur else '0'))\n")
            append("        except Exception: pass\n")
            append("except Exception as _e:\n")
            append("    _o.append('!ERR|%s' % _e)\n")
            append("open(_p, 'wb').write('\\n'.join(_o))\n")
        }
        runPython(code)
    }

    /** 读配置项（读不到返回空）。 */
    fun loadConfig(context: Context): List<McpConfigItem> {
        val f = configFile(context)
        if (!f.isFile) return emptyList()
        val out = mutableListOf<McpConfigItem>()
        try {
            for (line in f.readText().split('\n')) {
                val t = line.trim()
                if (t.isEmpty()) continue
                val p = t.split('|')
                when (p.getOrNull(0)) {
                    "P" -> if (p.size >= 6) out.add(McpConfigItem(
                        p[1], p[2], "slider",
                        p[3].toFloatOrNull() ?: 0f, p[4].toFloatOrNull() ?: 0f, p[5].toFloatOrNull() ?: 1f,
                        emptyList(), p.getOrElse(6) { "" }))
                    "B" -> if (p.size >= 3) out.add(McpConfigItem(p[1], p[2], "toggle", value = p.getOrElse(3) { "0" }))
                    "D" -> if (p.size >= 4) {
                        val opts = mutableListOf<String>()
                        val labs = mutableListOf<String>()
                        for (pair in p[3].split(',')) {
                            if (pair.isEmpty()) continue
                            val i = pair.indexOf('~')
                            if (i >= 0) { opts.add(pair.substring(0, i)); labs.add(pair.substring(i + 1)) }
                            else { opts.add(pair); labs.add(pair) }
                        }
                        out.add(McpConfigItem(p[1], p[2], "dropdown", options = opts,
                            value = p.getOrElse(4) { "" }, optionLabels = labs))
                    }
                }
            }
        } catch (e: Throwable) {
        }
        return out
    }

    /** 设置某模块的某个属性（复刻 ClickGui._setModuleAttr 的类型转换）。 */
    fun setConfig(context: Context, name: String, attr: String, value: String) {
        val code = buildString {
            append(pyPrelude())
            append("try:\n")
            append("    _s = _get_sys()\n")
            append("    _mod = _s._manager.get(").append(q(name)).append(")\n")
            append("    if _mod is not None:\n")
            append("        _attr = ").append(q(attr)).append("\n")
            append("        _val = ").append(q(value)).append("\n")
            append("        _parts = _attr.split('.')\n")
            append("        _obj = _mod\n")
            append("        for _p0 in _parts[:-1]:\n")
            append("            _obj = getattr(_obj, _p0, None)\n")
            append("            if _obj is None: break\n")
            append("        if _obj is not None:\n")
            append("            if _attr == 'multi_limit' and str(_val).lower() in ('inf','infinity','unlimited','-1'):\n")
            append("                setattr(_obj, _parts[-1], None)\n")
            append("            else:\n")
            append("                _cur = getattr(_obj, _parts[-1], None)\n")
            append("                if isinstance(_cur, bool):\n")
            append("                    _val = str(_val).strip().lower() in ('true','1','on','yes')\n")
            append("                elif isinstance(_cur, int):\n")
            append("                    try: _val = int(round(float(_val)))\n")
            append("                    except Exception: pass\n")
            append("                elif isinstance(_cur, float):\n")
            append("                    try: _val = float(_val)\n")
            append("                    except Exception: pass\n")
            append("                setattr(_obj, _parts[-1], _val)\n")
            append("except Exception:\n")
            append("    pass\n")
        }
        runPython(code)
    }

    private fun runPython(code: String) {
        Thread({
            try {
                NativeCore.runPythonSource(code)
            } catch (e: Throwable) {
            }
        }, "mcp-bridge").start()
    }
}

// 内置中文映射（合并 BP_mcp 与 z1yr_src 两版 ClickGui 的 CHINESE_* 表）
private const val CN_FEATURES_LIT = "'AutoTool':'自动工具','BlockIn':'方块围栏','AutoTrap':'自动围人','Fucker':'自动拆床','TriggerBot':'扳机','Aimbot':'自动瞄准','RodAim':'鱼竿自瞄','Criticals':'刀刀暴击','Autorod':'自动鱼竿','AutoClicker':'自动连点','AutoSoup':'自动喝汤','KillAura':'杀戮光环','Reach':'攻击距离','Velocity':'反击退','JumpReset':'跳跃重置','Fly':'飞行','Noclip':'穿墙','Bhop':'速度','RideFly':'坐骑飞行','ClickTP':'点击传送','Sprint':'自动疾跑','InventoryWalk':'背包行走','NoJumpDelay':'无跳跃延迟','SprintReset':'疾跑重置','Respawn':'原地复活','CEBridge':'CE桥接','Scaffold':'自动搭桥','Bridge6':'搭路 7.0','ESP':'透视','Zoom':'缩放','GaussBlur':'容器模糊','FogChange':'迷雾视效','KillEffect':'击杀特效','MotionCamera':'运动相机','Freecam':'自由视角','HUD':'界面显示','KeyDisplay':'按键显示','Spammer':'自动发言','Team':'队伍','FashionUnlock':'时装破解','Mount':'坐骑生成','Debug':'调试信息','GMPanel':'GM面板','PyRpc':'PyRpc日志','Whitelist':'白名单','Notification':'通知','ClickGui':'功能菜单','List':'玩家列表','AntiBot':'反机器人','KBChange':'击退移位','RideBhop':'坐骑速度','AutoWeb':'自动蜘蛛网','NoBreakDelay':'无挖矿延迟','RideTPaura':'坐骑百米','FastAnchor':'快速重生锚','AttackSlow':'攻击减速','NoThrowDelay':'无投掷延迟','Invisible':'隐身','Crasher':'崩溃器','Xray':'矿透','NoHurtCam':'无受击抖动','CombatBot':'战斗机器人','AutoShield':'自动举盾','CombatHelper':'战斗助手','AutoEat':'自动吃金苹果','ArrowAim':'弓箭自瞄','Combo':'连击提示','SprintCriticals':'疾跑暴击','ElytraCruise':'鞘翅巡航','InteractRange':'交互距离','CommandBlockViewer':'命令方块查看','HideExp':'隐藏经验条','BigFov':'大视角','CoordDisplay':'坐标显示','CameraWallClip':'视角穿墙','ParticleTrail':'粒子拖尾','SkyRender':'天空渲染','LineRender':'射线渲染','BorderRender':'渲染边框','TitleRender':'称号渲染','DetailInfo':'详细信息','HidePlayers':'屏蔽玩家','DeathLocation':'死亡地点','PopupBlocker':'屏蔽弹窗','ForceMinimap':'强制小地图','CustomUid':'自定义UID','Possess':'附身视角','AnimPlayer':'动作播放器','PetHelper':'宠物助手','PlayerTeleport':'传送玩家','CrystalAura':'水晶光环','ShulkerPreview':'潜影盒预览','AutoFWMace':'自动烟花锤','Blink':'闪现','MCP':'MCP 加载','PY':'PY 加载','AutoSave':'自动自救','CustomFog':'自定义迷雾','CustomStar':'自定义星星','BMWSpeed':'布吉岛速度','CameraClip':'摄像机穿墙','AutoFish':'自动钓鱼','ArrayList':'功能列表','CustomSky':'自定义天空','CustomMoon':'自定义月亮','CustomBright':'自定义亮度','CustomContrast':'自定义对比度','CustomSaturation':'自定义饱和度','CustomTint':'自定义色调'"
private const val CN_CATEGORIES_LIT = "'World':'世界','Combat':'战斗类','Movement':'移动类','Visual':'视觉类','Misc':'杂项类','Client':'客户端类'"
private const val CN_PARAMS_LIT = "'SneakDelay':'潜行延迟','ClickSpan':'点击间隔','RodDelay':'甩竿延迟','CPS':'CPS','Reach':'距离','Health':'血量','Delay':'延迟','PulseDelay':'脉冲延迟','Range':'范围','AutoSwitch':'自动切换','Radius':'半径','AirPlace':'空放自救','KeepY':'锁定搭路层','Descend':'下探层数','BridgeMode':'搭路模式','AntiAirPlace':'空放自救','RpcFallback':'服务端兜底','Swing':'揮手','RequireAim':'瞄准才放','Rotate':'转头','SwitchMode':'切换模式','SafeWalk':'安全行走','Shape':'铺法','Move':'移动','AxisMode':'轴向模式','Multiplier':'减速倍数','SlowTicks':'地面持续','AirSlowTicks':'空中持续','ResetOnHurt':'受击重置','DisableInAir':'空中关闭','RestoreMode':'切回武器','WeaponMode':'武器选择','FOV':'视野','MultiLimit':'目标上限','SwitchDelay':'切换延迟','SilentSwitch':'静默切换','Sticky':'粘沜锁定','TrackRange':'追踪范围','AttackRange':'攻击距离','KeepDist':'保持距离','HealHP':'回血阈值','HealKeep':'回血距离','Yaw':'转向角','Red':'红色','Green':'绿色','Blue':'蓝色','Smooth':'平滑','Predict':'预测','Distance':'距离','Speed':'速度','FallSpeed':'下落速度','YMotion':'垂直速度','X':'击退X','Y':'击退Y','Z':'击退Z','SwitchTick':'切换延迟','RestoreTick':'恢复延迟','RestartTick':'重挖延迟','AimConfirmTicks':'瞄准确认','ScanInterval':'扫描间隔','Intensity':'模糊强度','Density':'雾浓度','Random':'随机长度','Scale':'缩放','PosX':'位置X','PosY':'位置Y','Size':'大小','Alpha':'透明度','Immediate':'立即跳','MaxDisplay':'显示上限','OnClick':'点击触发','OnWeapon':'持武器','OnSneak':'潜行触发','OnHold':'按住触发','CanAttack':'可攻击','SwingAir':'空挥','ECBypass':'ECBypass','AttackMode':'攻击模式','Priority':'优先级','OnHoldAttack':'按住攻击','Criticals':'暴击','Sprint':'疾跑','ReleaseTime':'松开时长','ReleaseMode':'松开方式','TimingMode':'时长模式','CpsEnabled':'补充CPS','Mode':'模式','CPS Min':'最小CPS','CPS Max':'最大CPS','ChestESP':'箱子透视','ChestFOV':'箱子视野','ChestRadius':'箱子半径','CameraMode':'相机模式','MoveFix':'移动修正','Teambox':'队友框','Extend':'提前格数','Rotation':'转头模式','OnHoldBreak':'按住挖掘','Chinese':'中文','TranslateParams':'翻译配置','Shortcut':'快捷图标','Targethud':'目标面板','BJD':'布吉岛','Decode':'解码','ShowMessage':'显示消息','MidClick':'中键','Log':'日志','Notify':'通知','IgnoreTeam':'忽略队友','LeftClick':'按住锁定','WallCheck':'遮挡检测','Target':'目标','TargetFilter':'目标过滤','TargetX':'目标X','TargetZ':'目标Z','Count':'数量','Interval':'间隔','Duration':'时长','PreferEnchanted':'优先附魔','Hide':'隐藏','Anim':'动画','Dynamic':'动态','RotateMode':'转头模式','RotateFovReset':'超范围转回','RotateRange':'转头距离','AimAngle':'攻击视角','RotateFov':'转回角度','AimMode':'瞄准模式','PlaceRange':'放置距离','ExplodeRange':'引爆距离','ExplodeDelay':'引爆间隔','MinHealth':'最小血量','SurroundHealth':'自保血量','Place':'自动放置','Explode':'自动引爆','SelfProtect':'自我保护','Surround':'自动围圈','SurroundOnEnable':'开启即围圈','PlaceExpire':'放置过期','MultiPlace':'多点放置','FallDetect':'坠落检测','AirStuck':'坠落减速','YLimit':'高度上限','Budget':'射线预算','Refresh':'刷新间隔','MaxRender':'渲染上限','RenderTick':'渲染间隔','Diamond':'钻石','Iron':'铁','Gold':'金','Emerald':'绿宝石','Debris':'远古残骸','Redstone':'红石','Lapis':'青金石','Coal':'煤炭','Copper':'铜','Quartz':'石英','RestoreDelay':'恢复延迟','Burst':'连发数','Players':'假玩家数','StepDelay':'步进延迟','ClimbH':'爬升高度','LockRange':'锁定半径','SmashMin':'落高门槛','AttackCD':'出手间隔','RocketCD':'烟花间隔','ClimbPitch':'爬升俯仰','CruisePitch':'巡航俯仰','CruiseFall':'巡航落高','LowClimbPad':'近身余量','DiveMax':'俯冲上限','RecoverTick':'恢复时长','RecoverPitch':'恢复俯仰','ChainCD':'连击间隔','RocketWin':'烟花窗口','RocketBudget':'烟花预算','TakeoffCD':'起飞间隔','AirCD':'空中间隔','SilentRot':'静默转向','WeaponOnly':'仅持重锤','AutoGlide':'自动张翼','TakeoffAny':'无目标起飞','CruiseMode':'巡航接管','DiveRocket':'俯冲补烟花','RecoverRocket':'恢复补烟花','Chain':'链式硝击'"
private const val CN_OPTIONS_LIT = "'single':'单体','switch':'多体','multi':'群体','distance':'最近距离','health':'最少血量','angle':'准星最近','default':'默认','auto':'自动','manual':'手动','silent':'静默','bjd':'布吉岛','static':'静态','dynamic':'动态','off':'关闭','2d':'2D','3d':'3D','both':'两者','right':'右','left':'左','lowhop':'低跳','motion':'兔子跳','armor':'护甲颜色','namecolor':'名字颜色','prefix':'名字前缀','all':'全部','bed':'床','surround':'围堵','sword':'剑','axe':'斧头','mace':'重锤','w':'松开W','sprint':'松开疾跑','smart':'智能','custom':'自定义','player':'玩家','nonplayer':'非玩家','mob':'生物','client':'可见','normal':'正常','telly':'瞬移','tower':'搭高','bridge':'平铺','vertical':'锁Y轴','clutch':'受击自救'"
