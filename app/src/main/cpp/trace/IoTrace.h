/*
 * Copyright (C) 2026 Devout9527
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 这是 TexInject 的一部分。
 * TexInject 是自由软件：你可以依据 GNU Affero 通用公共许可证（AGPL）第 3 版
 * 或（由你选择）任何更新版本重新发布和/或修改它。
 *
 * TexInject 分发时希望它有用，但不提供任何担保，甚至不提供适销性或
 * 特定用途适用性的默示担保。详见 GNU Affero 通用公共许可证。
 */
#ifndef TEXINJECT_IO_TRACE_H
#define TEXINJECT_IO_TRACE_H

#include <cstddef>

/**
 * 文件 I/O 追踪。
 *
 * 通过 Plt::replace 把游戏对 openat / openat64 / __openat / open / open64 /
 * fopen / fopen64 / stat / lstat / access / faccessat / opendir / mkdir /
 * unlink / rename / rmdir / truncate 的调用接到我们这里，记录
 * 「调用者 RVA + 路径 + 返回值 + 打开方式」到一块静态环形缓冲，
 * 需要时一次性落盘。
 *
 * 用途（这是它的主要价值）：
 *   - 搞清楚材质到底从哪个目录加载、读的是 png 还是 .material
 *   - 看 folder_md5.json 什么时候被读、游戏是不是靠 mtime 判断包更新
 *   - 看反作弊有没有在扫 /proc/self/maps、扫了哪些文件
 *   - 3.10 的资源包到底落在 netease_client_packs 还是 resource_packs
 *   - 看谁在 unlink / rename 注入进去的文件（材质被「还原」的真凶）
 *
 * 已知边界：
 *   只能看到【通过 GOT 调用】的 libc 函数。如果游戏直接用 svc #0 走
 *   syscall，这里看不到。实测 Minecraft 的绝大多数文件操作走 libc 包装。
 */
namespace IoTrace {

/** 安装 hook。返回是否至少接上了一个函数。 */
bool start();

/**
 * 装 hook 并把所有对 vanilla.mcp 的打开重定向到 toPath。
 * 只重定向，不记录。返回是否装上。
 */
bool installRedirect(const char* toPath);

/** 把 storge/stream 的读写整体镜像到 root 下（下载捕获）。 */
bool installMirror(const char* root);

/** 摘掉 hook（缓冲里的记录保留，可以继续 dump）。 */
void stop();

/** 是否正在记录。 */
bool running();

/** 缓冲区里已有的记录条数（超过环形大小时饱和）。 */
size_t count();

/** 把缓冲写成文本。返回是否成功。 */
bool dump(const char* path);

/** 最近一次失败原因。 */
const char* lastError();

}  // namespace IoTrace

#endif  // TEXINJECT_IO_TRACE_H
