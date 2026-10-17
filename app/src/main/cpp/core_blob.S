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
/* 内置 core.so 字节（材质注入核心，来自 KuSug UI，原作者 bi匕匕bi）。
 * 用 .incbin 把二进制直接编进 texbridge.so，运行时释放到文件再 dlopen。 */
    .section .rodata
    .global texinject_core_so_start
    .global texinject_core_so_end
    .align 4
texinject_core_so_start:
    .incbin "core_embedded.bin"
texinject_core_so_end:
