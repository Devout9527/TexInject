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
package com.kael.texinject

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.io.File
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * 敏感信息的加密存储（AES-256-GCM，密钥由 AndroidKeyStore 保管）。
 *
 * 存放目录：/data/user/0/com.netease.x19/files/texinject/
 * （应用私有目录，不再放在 pack_netease 资源包里）
 *
 * 目前用于：网易云登录 cookie。
 */
object SecretStore {
    private const val DIR = "/data/user/0/com.netease.x19/files/texinject"
    private const val KEY_ALIAS = "texinject_secret_v1"
    private const val ANDROID_KEYSTORE = "AndroidKeyStore"
    private const val TRANSFORM = "AES/GCM/NoPadding"
    private const val IV_LEN = 12
    private const val TAG_BITS = 128

    private fun dir(context: Context): File {
        val candidates = listOf(
            File(DIR),
            File(context.filesDir, "texinject"),
            File(context.cacheDir, "texinject")
        )
        for (d in candidates) {
            if (d.exists() || d.mkdirs() || d.isDirectory) return d
        }
        return File(context.filesDir, "texinject").apply { mkdirs() }
    }

    private fun key(): SecretKey? = try {
        val ks = KeyStore.getInstance(ANDROID_KEYSTORE).apply { load(null) }
        (ks.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.secretKey ?: run {
            val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE)
            gen.init(
                KeyGenParameterSpec.Builder(
                    KEY_ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT
                )
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(256)
                    .build()
            )
            gen.generateKey()
        }
    } catch (e: Throwable) {
        null
    }

    fun save(context: Context, name: String, value: String) {
        try {
            val k = key() ?: return
            val cipher = Cipher.getInstance(TRANSFORM)
            cipher.init(Cipher.ENCRYPT_MODE, k)
            val iv = cipher.iv
            val enc = cipher.doFinal(value.toByteArray(Charsets.UTF_8))
            val out = ByteArray(1 + iv.size + enc.size)
            out[0] = iv.size.toByte()
            System.arraycopy(iv, 0, out, 1, iv.size)
            System.arraycopy(enc, 0, out, 1 + iv.size, enc.size)
            File(dir(context), "$name.enc").writeBytes(out)
        } catch (e: Throwable) {
        }
    }

    fun load(context: Context, name: String): String? {
        try {
            val f = File(dir(context), "$name.enc")
            if (!f.isFile || f.length() <= 1) return null
            val k = key() ?: return null
            val data = f.readBytes()
            val ivLen = data[0].toInt()
            if (ivLen <= 0 || ivLen > 32 || data.size <= 1 + ivLen) return null
            val iv = data.copyOfRange(1, 1 + ivLen)
            val enc = data.copyOfRange(1 + ivLen, data.size)
            val cipher = Cipher.getInstance(TRANSFORM)
            cipher.init(Cipher.DECRYPT_MODE, k, GCMParameterSpec(TAG_BITS, iv))
            return String(cipher.doFinal(enc), Charsets.UTF_8)
        } catch (e: Throwable) {
            return null
        }
    }

    fun clear(context: Context, name: String) {
        try {
            File(dir(context), "$name.enc").delete()
        } catch (e: Throwable) {
        }
    }
}
