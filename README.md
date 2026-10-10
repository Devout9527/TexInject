# TexInject

Minecraft 中国版（网易版，`com.netease.x19`）的 LSPosed 增强面板模块。

**适配游戏版本：3.9.15.297907**

## 功能

- **材质**：材质包注入 / 恢复官方资源 / 主页视频替换 / 重载外观
- **音乐**：网易云音乐播放器（在线播放、扫码登录、歌词、播放条，图标与背景可自定义）
- **脚本**：Python 脚本加载器（`.py` 直接执行，`.mcp` 注册加载）
- **渲染**：圆盘雷达
- **快捷键**：MCP 模块开关，可挂悬浮窗
- **设置**：播放模式 / 播放条尺寸 / 面板布局 / 分类图标大小 / 雷达选项

## 环境要求

- LSPosed（现代 LibXposed API 102）
- 作用域：`com.netease.x19`
- 仅 arm64-v8a

## 安装

1. 安装 APK
2. LSPosed 中启用本模块，作用域勾选「我的世界（网易版）」
3. 重启游戏
4. 首次运行会在
   `/storage/emulated/0/Android/data/com.netease.x19/files/pack_netease/`
   下自动创建 `resource_packs` / `icon` / `scripts` 等目录，并生成 `README.txt` 说明用法

## 构建

```bash
./gradlew assembleRelease
```

产物：`app/build/outputs/apk/release/app-release.apk`

## 许可

本项目以 **GNU Affero General Public License v3.0** 发布，详见 [LICENSE](LICENSE)。

```
Copyright (C) 2026 Devout9527

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.
```

### 关于 AGPL 的额外义务

AGPL 第 13 条要求：**如果通过网络向用户提供本程序的服务，必须向这些用户提供获取完整源代码的途径。**
由于本模块以二进制 APK 形式分发，请同时提供源码仓库地址（即本仓库）。

## 声明

- 材质包注入 / 视频替换功能来自 **Kusug UI**（作者：**bi匕匕bi**）。
- 本项目与 Mojang、Microsoft、网易无关。
