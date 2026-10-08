# 保留 Xposed 入口与被反射/动态查找的类名，避免 R8 改名导致 LSPosed 无法发现
-keepnames class com.kael.texinject.TexInjectHookInit { *; }
-keepnames class com.kael.texinject.** { *; }
