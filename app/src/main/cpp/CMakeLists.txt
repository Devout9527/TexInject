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
# 主库 libtexinject.so：只保留 Python 执行桥
# （建筑/地图/投影/箱子/OpenSSL/Dobby 全移除）
# ---------------------------------------------------------------------------
add_library(texinject SHARED
        native-lib.cpp
        tp/FunctionsAddress.cpp)

target_include_directories(texinject PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(texinject ${log-lib} ${dl-lib})

# ---------------------------------------------------------------------------
# libtexbridge.so：驱动内置 core.so（材质注入核心）
# ---------------------------------------------------------------------------
add_library(texbridge SHARED bridge.cpp core_blob.S)
target_include_directories(texbridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(texbridge ${log-lib} ${dl-lib})
