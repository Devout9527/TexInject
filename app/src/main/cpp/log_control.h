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
#ifndef LOG_CONTROL_H
#define LOG_CONTROL_H

#include <android/log.h>

// ==================== 日志开关 ====================
// 开启日志：注释掉下面这行
// 关闭日志：取消注释下面这行（发布时用这个）

 #define DISABLE_LOG

// ==================== 不要修改以下内容 ====================
#ifdef DISABLE_LOG
    #define LOGI(...) ((void)0)
    #define LOGE(...) ((void)0)
#else
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif

#endif // LOG_CONTROL_H
