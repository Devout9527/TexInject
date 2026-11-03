/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 游戏侧的少数几个开关。
 *
 * 现在只剩「相机穿墙」—— 自动工具 / 背包整理 / 设置耐久 / 物品编辑器 /
 * 全局夜视那一批实测都不好用，代码已经删掉，别在这里加回来。
 *
 * 这个模块只碰一种东西：inline hook 一个游戏函数。没有 tick 线程、没有
 * 周期调用，所以开不开都不会持续吃 CPU。
 */
#pragma once

#include <cstdint>
#include <string>

namespace texinject::game
{
    /** 后台 20Hz 线程；第一次开需要 tick 的功能时自动起。 */
    void StartTicker();

    /**
     * 告诉这里 so 的基址和游戏版本。只在第一次生效（幂等）。
     * 版本不是 3.10 就整个关掉，SetFeature 一律返回 false。
     */
    void SetBase(uintptr_t Base, const char* Version);

    /**
     * 开/关一个功能。目前只有：
     *   cameraClip  相机穿墙（把相机避让系统 hook 成空函数，镜头不再被方块顶回来）
     * @return false 表示 id 不认识 / 版本不支持 / hook 装不上。
     */
    bool SetFeature(const char* Id, bool On);

    bool Feature(const char* Id);

    /**
     * 手动装 LocalPlayer 抓取 hook（点「设置耐久」之类会用到）。
     * 幂等；返回是否已就绪。
     */
    bool Attach();

    /** 手上物品的 damage（+32 那个 int）。读不到返回 INT32_MIN。 */
    int HandDamage();

    /**
     * 改手上物品的 damage。
     * 注意：这个值可以是负的 —— 游戏里没损耗的物品默认就是 -32767，
     * 别拿它当非法输入挡掉。
     */
    bool SetHandDamage(int Value);

    /**
     * 诊断：手上 ItemStack 的地址和头 64 字节，读不到时返回原因。
     * 用来确认 +16 到底是不是 user data —— 「套到手上没反应」的时候先看这个。
     */
    std::string HandInfo();

    /**
     * 把一段 SNBT 挂到手上物品的 user data 上（只写客户端，服务器不认）。
     *
     * Ops 是扁平化的构造指令，每行 `类型\t键[\t值]`：
     *   c  进入一个新 compound（值是不用的）
     *   e  退出当前 compound，挂到上一层
     *   s  字符串  b byte  h short  i int  l long  f float  d double
     * 由 Kotlin 侧把 Snbt 树展平后传进来 —— 解析器只有一份，别写两套。
     *
     * 物品已经有 tag 时不覆盖（旧 unique_ptr 会泄漏，更怕它其实是 shared_ptr）。
     */
    bool ApplyHandNbt(const char* Ops);
}
