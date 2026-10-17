# -*- coding: utf-8 -*-
# 游戏内 Python API 探测器（在 TexInject 模块「脚本」面板里点这个 .py 运行）
#
# 做什么：
#   1. 枚举当前 Python 环境里已加载的所有模块
#   2. 探测一批网易版 Minecraft 常见的候选 API 模块名，能 import 就 dir() 出成员
#   3. 对函数/方法尽量取 signature；类标注基类
#   4. 结果写到脚本同目录 api_probe_report.txt，并在游戏里弹一条通知
#
# 为什么：网易版 .mcp 是加密包，静态解不了；但运行时 sys.modules 里就是
# 已解开的真模块。本脚本把可用的 API 表面 dump 出来，方便写功能时照着调。

import sys, os, inspect, time, traceback

# ---------- 候选模块名（网易版 MC Python API 常见命名）----------
CANDIDATES = [
    # 顶层常见
    "mc", "client", "server", "netease", "minecraft", "game", "engine",
    "level", "world", "player", "entity", "block", "item", "blockitem",
    "chat", "command", "gui", "ui", "event", "events", "sound",
    "particle", "actor", "comp", "common", "redstone", "effect",
    "recipe", "inventory", "dimension", "biome", "scoreboard",
    # mod.* 命名空间
    "mod", "mod.client", "mod.server", "mod.common",
    "mod.client.api", "mod.server.api", "mod.common.api",
    "mod.client.event", "mod.server.event", "mod.common.event",
    "mod.client.comp", "mod.server.comp", "mod.common.comp",
    # client.* / server.* / common.*
    "client.api", "client.event", "client.comp", "client.system",
    "server.api", "server.event", "server.comp", "server.system",
    "common.api", "common.event", "common.comp",
    # 基类 / 配置
    "BaseMod", "BaseClientSystem", "BaseServerSystem",
    "ModConfig", "ServerModConfig", "ClientModConfig",
    "ClientSystem", "ServerSystem", "CommonSystem",
    # 工具
    "modUtil", "modcommon", "modcommon.api", "modcommon.event",
    "ExtraServerSystemAPI", "ExtraClientSystemAPI",
    "mod.common.modConfig", "mod.common.modBase",
    "Mod", "compFactory", "Factory",
    # 网易额外
    "netease.api", "netease.event", "netease.comp",
    "gameapi", "GameAPI", "mcpc", "modMain",
]


def try_import(name):
    """import 一个模块名，返回 (ok, module_or_error)"""
    try:
        m = __import__(name)
        for part in name.split(".")[1:]:
            m = getattr(m, part)
        return True, m
    except Exception as e:
        return False, f"{type(e).__name__}: {e}"


def describe(v):
    """描述一个值：类型 + （函数的话）签名"""
    try:
        if inspect.isclass(v):
            bases = ", ".join(b.__name__ for b in v.__bases__) if getattr(v, "__bases__", None) else ""
            return f"class {v.__name__}" + (f"({bases})" if bases else "")
        if inspect.isfunction(v) or inspect.ismethod(v) or inspect.isbuiltin(v) or callable(v):
            try:
                sig = str(inspect.signature(v))
            except (ValueError, TypeError):
                sig = "(?)"
            return f"{type(v).__name__}{sig}"
        if inspect.ismodule(v):
            return "module"
        if isinstance(v, (int, float, str, bool, type(None))):
            return f"{type(v).__name__} = {repr(v)[:60]}"
        return type(v).__name__
    except Exception as e:
        return f"<describe error: {e}>"


def probe(name):
    ok, r = try_import(name)
    if not ok:
        return {"ok": False, "err": r}
    members = {}
    for k in dir(r):
        if k.startswith("_"):
            continue
        try:
            members[k] = describe(getattr(r, k))
        except Exception as e:
            members[k] = f"<error: {e}>"
    return {"ok": True, "file": getattr(r, "__file__", "?"), "members": members}


def notify(msg):
    """尽量在游戏里弹通知；不知道确切 API 就试几个常见名，都不行就 print"""
    tried = [
        ("client.api", "SetPlayerChatFilter"),       # 随便一个；只为探测
    ]
    # 网易版常见通知：client/api 的 BroadcastEvent / SendSystemMessage 之类
    for mod_path, _ in tried:
        ok, m = try_import(mod_path)
        if ok:
            for fn_name in ("SendSystemMessage", "BroadcastEvent", "Notify",
                              "SendMessage", "SendCommand"):
                if hasattr(m, fn_name):
                    try:
                        getattr(m, fn_name)(msg)
                        return
                    except Exception:
                        continue
    print(msg)


def main():
    lines = []
    lines.append("=" * 60)
    lines.append("  游戏内 Python API 探测报告")
    lines.append("  生成时间: " + time.strftime("%Y-%m-%d %H:%M:%S"))
    lines.append("  Python: " + sys.version.split()[0])
    lines.append("  平台: " + sys.platform)
    lines.append("=" * 60)

    # 1) 已加载模块
    loaded = sorted(k for k in sys.modules if k and not k.startswith("_"))
    lines.append("")
    lines.append("## 1. 已加载模块（sys.modules，共 %d 个）" % len(loaded))
    # 分组显示，每行 4 个
    for i in range(0, len(loaded), 4):
        lines.append("  " + "  ".join("%-30s" % x for x in loaded[i:i + 4]))

    # 2) 候选模块探测
    lines.append("")
    lines.append("## 2. 候选 API 模块探测")
    found = []
    for name in CANDIDATES:
        r = probe(name)
        if r["ok"]:
            found.append(name)
            lines.append("")
            lines.append("### " + name + "  (file: %s, %d 成员)" % (r["file"], len(r["members"])))
            for k in sorted(r["members"]):
                lines.append("    %-34s %s" % (k, r["members"][k]))
        else:
            lines.append("### " + name.ljust(34) + " ✗ " + r["err"])

    # 3) 汇总
    lines.append("")
    lines.append("=" * 60)
    lines.append("成功导入 %d 个候选模块: %s" % (len(found), ", ".join(found) or "(无)"))
    lines.append("=" * 60)

    report = "\n".join(lines)

    # 写文件：优先脚本同目录，回退到 cwd / 固定路径
    out = None
    for d in (
        os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else "",
        os.getcwd(),
        "/storage/emulated/0/Android/data/com.netease.x19/files/pack_netease/scripts",
    ):
        if d and os.path.isdir(d):
            out = os.path.join(d, "api_probe_report.txt")
            break
    if not out:
        out = "api_probe_report.txt"
    try:
        with open(out, "w", encoding="utf-8") as f:
            f.write(report)
    except Exception as e:
        print("写报告失败:", e)

    msg = "§a[API探测] 完成：发现 %d 个模块，报告 %s" % (len(found), out)
    notify(msg)
    print(report)


try:
    main()
except Exception as e:
    try:
        traceback.print_exc()
    except Exception:
        pass
    try:
        notify("§c[API探测] 失败: %s" % e)
    except Exception:
        print("API 探测失败:", e)
