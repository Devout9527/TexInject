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
#ifndef BUILD_TOOL_FUNCTION_UTILS_H
#define BUILD_TOOL_FUNCTION_UTILS_H

#include <cstdint>
#include <main.h>

class FunctionUtils {
public:
    template<typename Result, typename... Args>
    static Result callFunc(uintptr_t address, Args... args) {
        using Function = Result (*)(Args...);
        return reinterpret_cast<Function>(address)(args...);
    }
};

#endif // BUILD_TOOL_FUNCTION_UTILS_H
