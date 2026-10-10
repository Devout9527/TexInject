# 独立的签名扫描器（不依赖任何 hook 框架）
add_library(texsig STATIC Signature.cpp)
target_include_directories(texsig PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(texsig PROPERTIES POSITION_INDEPENDENT_CODE ON)
