# 反推的 3.9.5 地址（lowlib/libminecraftpe_low.so）

方法：以 3.9.15 地址（minecraftcn/libminecraftpe.so）取 24 字节序言，在 3.9.5 .so 中匹配。

| 函数 | 3.9.15 | 3.9.5 (反推) | 校验 |
|---|---|---|---|
| Actor::getDimension | 0xD8E7554 | 0xd8e5854 | ✅=JSON |
| PyModule::GetDict | 0x1224ABA0 | 0x12249110 | ✅=JSON |
| PyImport::AddModule | 0x122D63F0 | 0x122d4960 | (唯一) |
| Level::_render | 0x7B73A54 | 0x7b6fc08 | (唯一) |
| ClientInstance::getCamera | 0x7463BF4 | 0x74636f0 | (唯一) |
| Minecraft::update | 0x74C79FC | ? | 多候选，待精匹配 |
| newReceivePacket | 0xA7905A4 | ? | 多候选，待精匹配 |
| PyGILState_Ensure | 0x12356D88 | ? | 序言差异，待精匹配 |
| PyGILState_Release | 0x12356E20 | ? | 同上 |
| Actor::getClientInstance | 0x74C673C | ? | 同上 |

待补：用更长/掩码特征或 JSON 锚点精匹配剩余 5 个。
