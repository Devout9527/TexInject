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
import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace = "com.kael.texinject"
    compileSdk = 36
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.kael.texinject"
        minSdk = 28
        targetSdk = 36
        versionCode = 4
        versionName = "1.0.3"

        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    // 签名密码不写死在源码里：
    //   CI  -> 从 GitHub Secrets 注入环境变量
    //   本地 -> 放在 gitignore 掉的 local.properties
    val localProps = Properties()
    val localPropsFile = rootProject.file("local.properties")
    if (localPropsFile.exists()) {
        localPropsFile.inputStream().use { stream -> localProps.load(stream) }
    }
    fun secret(k: String, d: String): String {
        val e = System.getenv(k)
        if (e != null && e.isNotEmpty()) return e
        val p = localProps.getProperty(k)
        if (p != null && p.isNotEmpty()) return p
        return d
    }

    signingConfigs {
        create("release") {
            storeFile = rootProject.file(secret("KEYSTORE_PATH", "keystore/release.jks"))
            storePassword = secret("KEYSTORE_PASSWORD", "")
            keyAlias = secret("KEY_ALIAS", "texinject")
            keyPassword = secret("KEY_PASSWORD", "")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    kotlinOptions {
        jvmTarget = "11"
    }
}

dependencies {
    // 现代 LibXposed API 102（compileOnly，运行时由 LSPosed 框架提供）
    compileOnly(files("libs/libxposed-api-102.jar"))
}
