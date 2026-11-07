# 第三方代码

本目录下的 native 代码包含两部分外来实现，均保留原许可证，未修改其条款。

## `python/` — CPython 2.7.13

- 来源：https://github.com/python/cpython/tree/v2.7.13
- 许可：PSF License，全文见 `python/LICENSE`。
- 用途：只做**编译**。把 `.py` 编成 code object，再按游戏那张 opcode 表重排字节，
  输出 marshal 流。这份解释器不初始化到游戏进程的 Python 运行时里，也不接收游戏对象。
- `Python3105Bytecode.h` 是本项目新增的映射表（标准 opcode → 3.10.5 游戏 opcode）。

## `common/` `scripts/` — bedrock-python-xposed

- 来源：bedrock-python-xposed-source（作者以 MIT 发布）
- 许可：MIT。
- 用途：ARM64 特征扫描（`Arm64SignatureScanner`）、签名规则与地址发布
  （`Signatures.*`、`PyFunctionsAddress.*`、`tools/signatures.json` 的产物）、
  Python ABI 见证校验（`scripts/PythonScriptAbi3105.h`）、脚本队列
  （`scripts/ScriptRuntime.cpp`）。
- 本项目改动：`FunctionsAddress` 改名为 `PyFunctionsAddress`（避免与 `tp/FunctionsAddress`
  撞名）；`scripts/PyHost.cpp` 按我们的 `hook::Install` 重写，替换原项目的 Dobby 依赖。
