# TexInject —— R8 规则
#
# 这是 LSPosed 模块，有两个"按字符串找类"的硬依赖，改名即失效：
#   1. META-INF/xposed/java_init.list  -> com.kael.texinject.TexInjectHookInit
#   2. C++ 里 FindClass("com/kael/texinject/nativecore/NativeCore") + GetMethodID
# 所以下面必须 -keep 类名。

# ---------- 1. Xposed 入口（java_init.list 按名字加载） ----------
-keep class com.kael.texinject.TexInjectHookInit { *; }
-keepclassmembers class com.kael.texinject.TexInjectHookInit {
    public void initZygote(...);
    public void handleLoadPackage(...);
}

# ---------- 2. JNI 反射查找的类：类名 + 方法名都不能动 ----------
-keep class com.kael.texinject.nativecore.NativeCore { *; }

# 所有 native 方法所在类：保类名与 native 方法名（JNI 按名字绑定）
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}

# ---------- 3. 本模块类保名（LSPosed / 日志 / 反射字符串引用） ----------
# 只保名称，方法体仍参与压缩与优化，混淆第三方库不受影响
-keepnames class com.kael.texinject.** { *; }
-keepnames interface com.kael.texinject.** { *; }
-keepnames enum com.kael.texinject.** { *; }

# ---------- 4. 属性 ----------
-keepattributes Signature, InnerClasses, EnclosingMethod
-keepattributes *Annotation*, RuntimeVisibleAnnotations
-keepattributes SourceFile, LineNumberTable

# ---------- 5. Compose / Kotlin ----------
-dontwarn org.jetbrains.annotations.**
-dontwarn kotlinx.**
-keep class androidx.compose.runtime.** { *; }
-dontwarn androidx.compose.**

# ---------- 6. LibXposed（compileOnly，运行时由框架提供） ----------
-dontwarn io.github.libxposed.**
-keep class io.github.libxposed.** { *; }

# ---------- 7. 反射用到的 Android 内部 API ----------
-dontwarn android.view.SurfaceControl
-dontwarn android.view.SurfaceControl$Transaction

# ---------- 8. ViewTree*Owner：ComposeHost 靠反射挂到 PopupWindow 上 ----------
# 这些类用 Class.forName(...) + 方法名 "set" 反射调用，改名即失效。
# 失效后 ComposeView attach 到 PopupWindow 会抛：
#   IllegalStateException: ViewTreeLifecycleOwner not found from PopupWindow$PopupDecorView
-keep class androidx.lifecycle.ViewTreeLifecycleOwner { *; }
-keep class androidx.lifecycle.ViewTreeViewModelStoreOwner { *; }
-keep class androidx.savedstate.ViewTreeSavedStateRegistryOwner { *; }
-keepclassmembers class androidx.lifecycle.** {
    public static void set(android.view.View, **);
}
-keepnames class androidx.lifecycle.**
-keepnames class androidx.savedstate.**
