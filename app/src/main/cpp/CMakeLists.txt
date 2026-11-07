cmake_minimum_required(VERSION 3.22.1)
project("texinject" LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections -Os -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections -Os -fPIC")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -fPIC")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections -Wl,--as-needed -Wl,-z,max-page-size=4096 -Wl,-z,noexecstack")
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

find_library(log-lib log)
find_library(dl-lib dl)

# ---------------------------------------------------------------------------
# 私有 CPython 2.7.13：只用来把 .py 编成游戏那个 opcode 表的字节码，
# 和游戏自己的解释器完全隔离，只有字节流跨过去。
# ---------------------------------------------------------------------------
add_subdirectory(python)

# ---------------------------------------------------------------------------
# 主库 libtexinject.so：只保留 Python 执行桥
# ---------------------------------------------------------------------------
add_library(texinject SHARED
        native-lib.cpp
        tp/FunctionsAddress.cpp
        plt/PltHook.cpp
        trace/IoTrace.cpp
        mount/ResourceSourceHook.cpp
        mem/MemRw.cpp
        hook/InlineHook.cpp
        game/LocalPlayerHook.cpp
        game/GameApi.cpp
        common/Arm64SignatureScanner.cpp
        common/Signatures.cpp
        common/PyFunctionsAddress.cpp
        scripts/PyHost.cpp
        scripts/ScriptRuntime.cpp)

target_include_directories(texinject PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(texinject ${log-lib} ${dl-lib} pyloader_python27_compiler)
