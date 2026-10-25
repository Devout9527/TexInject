package com.kael.texinject.paths
import com.kael.texinject.GamePaths

import android.content.Context
import java.io.File

object BuildPaths {
    private fun base(context: Context): File =
        context.getExternalFilesDir(null)
            ?: GamePaths.extStorage()

    fun mcpDir(context: Context): File {
        val dir = File(base(context), "pack_netease/mcp").apply { mkdirs() }
        return dir
    }

    fun readmeFile(context: Context): File =
        File(base(context), "pack_netease/TexInject 说明.txt")

    fun scriptsDir(context: Context): File =
        File(base(context), "pack_netease/scripts").apply { mkdirs() }

    fun iconDir(context: Context): File {
        val dir = File(base(context), "pack_netease/icon").apply { mkdirs() }
        try {
            File(dir, "说明.txt").writeText(ICON_README)
        } catch (e: Throwable) {
        }
        return dir
    }

    private val ICON_README = """【自定义 图标 / 背景 说明】

目录：pack_netease/icon/
把对应文件放进本目录即可，进面板/进游戏时读取。

支持格式：
  图片： .png / .jpg
  视频： .mp4 （背景视频另支持 .webm / .mkv）
找不到对应文件时用默认（内置图标 / 纯色卡片）。

==================================================
1) 悬浮球 / 面板图标（图片）
==================================================
文件名（任一）：
  Icon.png    icon.png
  Logo.png    logo.png
  Cover.png   cover.png

★ 悬浮球也支持视频：放一个 .mp4 就用视频代替图标（循环、静音、居中裁剪铺满）。
  悬浮球视频名（任一）：
  Ball.mp4   ball.mp4   球.mp4   球视频.mp4

★ 左上角面板图标也支持视频：**和悬浮球同一份**（悬浮球放什么，面板图标就用什么）。
  也支持专用名：Icon.mp4 / icon.mp4 / Logo.mp4 / logo.mp4 / Cover.mp4 / cover.mp4 / 图标.mp4 / 面板.mp4

注：图标/视频只在 icon/ 目录找，不再扫 resource_packs（避免和材质注入冲突）。

==================================================
2) 分类图标（图片）
==================================================
两种命名任选其一：

  方式A 按分类名：
    材质替换.png  音乐.png  脚本.png  快捷键.png  设置.png

  方式B 按序号（icon1 = 第1个分类）：
    icon1.png  icon2.png  icon3.png  icon4.png
    icon5.png
    （也支持 icon01.png … icon05.png）

  分类顺序：材质替换 · 音乐 · 脚本 · 快捷键 · 设置

==================================================
3) 面板背景（图片 或 视频）
==================================================
★ 视频优先于图片：同目录里若同时有视频和图片，用视频。

  图片名（任一）：
    background.png / .jpg     Background.png / .jpg
    bg.png / .jpg             Bg.png / .jpg
    panel.png                 menu.png
    cover.png                 Cover.png
    背景.png  背景.jpg  背景图.png  面板.png

  视频名（任一）：
    background.mp4   Background.mp4
    bg.mp4           Bg.mp4
    panel.mp4        menu.mp4
    背景.mp4  背景视频.mp4  面板.mp4
    background.webm  bg.webm  background.mkv

==================================================
4) 主页（登录界面）视频（视频）
==================================================
  视频名（任一）：
    loginVideoNew.mp4   LoginVideo.mp4   loginvideo.mp4
    登录视频.mp4  主界面视频.mp4  首页视频.mp4
    home.mp4   login.mp4

==================================================
5) 右上角播放条（图片 或 视频）
==================================================
★ 视频优先于图片。

  图标（图片）：
    music_icon.png   MusicIcon.png   music_icon.jpg
    音乐图标.png  播放图标.png  music.png

  背景-图片：
    music_bar.png  MusicBar.png  musicbar.png
    bar.png  播放条.png  音乐条.png

  背景-视频：
    music_bar.mp4  musicbar.mp4  bar.mp4
    播放条.mp4  音乐条.mp4
    music_bar.webm  music_bar.mkv

==================================================
小结
==================================================
· 图片：.png / .jpg
· 视频：.mp4（推荐）/ .webm / .mkv
· 文件名大小写需与上面一致（每个都给了两种写法）
· 本文件可删除，不影响功能
"""
}
