-keep class com.kael.texinject.TexInjectHookInit { *; }
-keepclassmembers class com.kael.texinject.TexInjectHookInit {
    public void initZygote(...);
    public void handleLoadPackage(...);
}

-keep class com.kael.texinject.nativecore.NativeCore { *; }

-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}

-keepnames class com.kael.texinject.** { *; }
-keepnames interface com.kael.texinject.** { *; }
-keepnames enum com.kael.texinject.** { *; }

-keepattributes Signature, InnerClasses, EnclosingMethod
-keepattributes *Annotation*, RuntimeVisibleAnnotations
-keepattributes SourceFile, LineNumberTable

-dontwarn org.jetbrains.annotations.**
-dontwarn kotlinx.**
-keep class androidx.compose.runtime.** { *; }
-dontwarn androidx.compose.**

-dontwarn io.github.libxposed.**
-keep class io.github.libxposed.** { *; }

-dontwarn android.view.SurfaceControl
-dontwarn android.view.SurfaceControl$Transaction

-keep class androidx.lifecycle.ViewTreeLifecycleOwner { *; }
-keep class androidx.lifecycle.ViewTreeViewModelStoreOwner { *; }
-keep class androidx.savedstate.ViewTreeSavedStateRegistryOwner { *; }
-keepclassmembers class androidx.lifecycle.** {
    public static void set(android.view.View, **);
}
-keepnames class androidx.lifecycle.**
-keepnames class androidx.savedstate.**
