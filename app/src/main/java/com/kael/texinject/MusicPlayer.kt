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

import android.app.Activity
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.os.Handler
import android.os.Looper
import android.util.Base64
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import org.json.JSONObject
import com.kael.texinject.ui.NowPlayingBar
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

/**
 * 网易云音乐播放器（调用 NeteaseCloudMusicApi 服务），内联嵌入面板右侧。
 *
 * 默认连接：http://musicapi.infinitex.icu/（可用素材目录 music_api.txt 覆盖）。
 *   搜索    /cloudsearch?keywords=xxx&type=1&limit=100
 *   推荐    /personalized/newsong?limit=50
 *   播放地址 /song/url?id=xxx （需登录才返回真实地址）
 *   扫码登录 /login/qr/key → /login/qr/create → /login/qr/check → cookie
 *
 * 登录 cookie 会持久化到素材目录 music_cookie.txt，之后所有请求自动带上。
 */
object MusicPlayer {
    /** 「重载外观」时调用：重新读素材目录里的自定义资源。 */
    fun reloadCustomizations() {
        try {
            com.kael.texinject.ui.NowPlayingBar.clearCache()
            com.kael.texinject.ui.CategoryIcons.clearCache()
        } catch (e: Throwable) {
        }
    }


    private var apiBase = "http://musicapi.infinitex.icu"
    /** 拿来做加密存储（SecretStore）用的上下文。 */
    private var appCtx: android.content.Context? = null
    private const val DEFAULT_BASE = "http://musicapi.infinitex.icu"

    private var cookie: String? = null
    private var player: MediaPlayer? = null
    private var currentSong: Song? = null
    private val main = Handler(Looper.getMainLooper())

    // 当前面板上的控件引用（切分类后重建）
    private var statusView: TextView? = null
    private var listHost: LinearLayout? = null
    private var qrBox: LinearLayout? = null
    private var qrImage: ImageView? = null
    private var qrStatus: TextView? = null
    private var loginBtn: Button? = null

    // ---------------- 最近播放 ----------------
    private const val PREF_RECENT = "texinject_music_recent"
    private val recents = mutableListOf<Song>()
    @Volatile private var curTab = 0          // 0=推荐  1=最近播放

    private fun recentStore(c: android.content.Context) =
        c.getSharedPreferences(PREF_RECENT, android.content.Context.MODE_PRIVATE)

    private fun loadRecents(c: android.content.Context) {
        recents.clear()
        try {
            val arr = org.json.JSONArray(recentStore(c).getString("list", "[]"))
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                recents.add(Song(o.optLong("id"), o.optString("name"), o.optString("artist")))
            }
        } catch (e: Throwable) {
        }
    }

    private fun saveRecents(c: android.content.Context) {
        try {
            val arr = org.json.JSONArray()
            for (sg in recents.take(100)) {
                arr.put(org.json.JSONObject().apply {
                    put("id", sg.id); put("name", sg.name); put("artist", sg.artist)
                })
            }
            recentStore(c).edit().putString("list", arr.toString()).apply()
        } catch (e: Throwable) {
        }
    }

    /** 播放时记一笔（按 id 去重，最新的排最前）。 */
    private fun addRecent(c: android.content.Context, song: Song) {
        recents.removeAll { it.id == song.id }
        recents.add(0, song)
        while (recents.size > 100) recents.removeAt(recents.size - 1)
        saveRecents(c)
    }

    private var lyricsView: TextView? = null
    private var lyricLines: List<Pair<Long, String>> = emptyList()
    private var lyricRunnable: Runnable? = null
    private var phoneBox: LinearLayout? = null
    private var qrUrlView: TextView? = null

    // 当前播放列表（用于播完自动循环下一首）与宿主 Activity
    @Volatile private var playlist: List<Song> = emptyList()
    @Volatile private var uiActivity: Activity? = null

    // 播放模式：0=顺序 1=随机 2=单曲循环
    private const val MODE_ORDER = 0
    private const val MODE_SHUFFLE = 1
    private const val MODE_SINGLE = 2
    private const val PREF_MODE = "music_play_mode"
    @Volatile private var playMode = MODE_ORDER

    fun getPlayMode(context: android.content.Context): Int =
        context.getSharedPreferences("texinject_music", android.content.Context.MODE_PRIVATE)
            .getInt(PREF_MODE, MODE_ORDER).coerceIn(MODE_ORDER, MODE_SINGLE)

    fun setPlayMode(context: android.content.Context, mode: Int) {
        val normalized = mode.coerceIn(MODE_ORDER, MODE_SINGLE)
        playMode = normalized
        context.getSharedPreferences("texinject_music", android.content.Context.MODE_PRIVATE)
            .edit().putInt(PREF_MODE, normalized).apply()
    }

    private val pollHandler = Handler(Looper.getMainLooper())
    private var polling = false
    private var pollKey: String? = null

    private data class Song(val id: Long, val name: String, val artist: String)

    private fun apiRoot(): File = GamePaths.sourceDir

    /** 覆盖 API 地址 + 读取已保存的登录 cookie。 */
    fun configure(context: android.content.Context) {
        apiBase = DEFAULT_BASE
        try {
            val f = File(apiRoot(), "music_api.txt")
            if (f.isFile) {
                val line = f.readLines().firstOrNull { it.isNotBlank() }?.trim()
                if (line != null && (line.startsWith("http://") || line.startsWith("https://"))) {
                    apiBase = line.trimEnd('/')
                }
            }
        } catch (e: Throwable) {
        }
        cookie = try {
            // 新：AES-256 加密存储（/data/user/0/com.netease.x19/files/texinject/）
            // 取不到就回退读旧的明文文件（读到后会被 saveCookie 迁走并删掉）
            appCtx?.let { c -> com.kael.texinject.SecretStore.load(c, "music_cookie") }
                ?.takeIf { it.isNotBlank() }
                ?: run {
                    val cf = File(apiRoot(), "music_cookie.txt")
                    if (cf.isFile) cf.readText().trim().ifEmpty { null } else null
                }
        } catch (e: Throwable) {
            null
        }
    }

    private fun saveCookie(c: String) {
        cookie = c
        // AES-256 加密存储；旧的明文文件顺手清掉
        try { appCtx?.let { com.kael.texinject.SecretStore.save(it, "music_cookie", c) } } catch (e: Throwable) {}
        try { File(apiRoot(), "music_cookie.txt").delete() } catch (e: Throwable) {}
    }

    /** 构建音乐播放器 View（由面板加到右侧固定容器）。 */
    fun buildView(activity: Activity): View {
        appCtx = activity.applicationContext
        loadRecents(activity.applicationContext)
        configure(activity)
        uiActivity = activity
        playMode = getPlayMode(activity)
        val dp = activity.resources.displayMetrics.density
        fun d(v: Float) = (v * dp + .5f).toInt()

        val root = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(d(2f), 0, d(2f), 0)
        }

        // 搜索行
        val searchRow = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        val input = EditText(activity).apply {
            hint = "搜索歌名 / 歌手"
            setTextColor(Color.WHITE)
            setHintTextColor(Color.parseColor("#8899A6"))
            textSize = 14f
        }
        searchRow.addView(input, LinearLayout.LayoutParams(0, d(44f), 1f))
        searchRow.addView(Button(activity).apply {
            text = "搜索"
            setOnClickListener {
                val kw = input.text.toString().trim()
                if (kw.isEmpty()) Toast.makeText(activity, "请输入关键词", Toast.LENGTH_SHORT).show()
                else doSearch(activity, kw)
            }
        }, LinearLayout.LayoutParams(d(72f), d(44f)))
        root.addView(searchRow, lp())

        // 登录行
        val loginRow = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        // 扫码登录 / 手机号登录已移到「设置 → 网易云登录」
        qrStatus = TextView(activity).apply {
            text = if (cookie != null) "已登录，可播放 VIP 歌" else "未登录（多数歌无播放地址）"
            textSize = 11f
            setTextColor(Color.parseColor("#A9A9A9"))
            setPadding(d(8f), 0, 0, 0)
        }
        loginRow.addView(qrStatus, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        root.addView(loginRow, lp())

        // 手机号登录区（默认隐藏）
        phoneBox = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
        }
        val phoneRow = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        val phoneInput = EditText(activity).apply {
            hint = "手机号"
            setTextColor(Color.WHITE); setHintTextColor(Color.parseColor("#8899A6")); textSize = 13f
            inputType = android.text.InputType.TYPE_CLASS_PHONE
        }
        val pwdInput = EditText(activity).apply {
            hint = "密码"
            setTextColor(Color.WHITE); setHintTextColor(Color.parseColor("#8899A6")); textSize = 13f
            inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        phoneRow.addView(phoneInput, LinearLayout.LayoutParams(0, d(42f), 1f))
        phoneRow.addView(pwdInput, LinearLayout.LayoutParams(0, d(42f), 1f).apply { leftMargin = d(6f) })
        phoneBox!!.addView(phoneRow, lp())
        phoneBox!!.addView(Button(activity).apply {
            text = "登录"
            setOnClickListener {
                val p = phoneInput.text.toString().trim()
                val pwd = pwdInput.text.toString()
                if (p.isEmpty() || pwd.isEmpty()) {
                    Toast.makeText(activity, "请输入手机号和密码", Toast.LENGTH_SHORT).show()
                } else doPhoneLogin(activity, p, pwd)
            }
        }, LinearLayout.LayoutParams(d(96f), d(40f)).apply { gravity = Gravity.END })
        root.addView(phoneBox, lp())

        // 二维码区（默认隐藏）
        qrBox = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            visibility = View.GONE
        }
        qrImage = ImageView(activity).apply {
            layoutParams = LinearLayout.LayoutParams(d(180f), d(180f))
            scaleType = ImageView.ScaleType.FIT_CENTER
        }
        qrBox!!.addView(qrImage)
        qrUrlView = TextView(activity).apply {
            textSize = 10f
            setTextColor(Color.parseColor("#8899A6"))
            gravity = Gravity.CENTER
            setPadding(0, d(4f), 0, 0)
        }
        qrBox!!.addView(qrUrlView, lp())
        root.addView(qrBox, lp())

        // 分类切换：左「推荐」 / 右「最近播放」
        var refreshTabs: () -> Unit = {}
        val tabRow = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, d(6f), 0, d(2f))
        }
        fun mkTab(label: String, index: Int): TextView = TextView(activity).apply {
            text = label
            gravity = Gravity.CENTER
            textSize = 12.5f
            typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.BOLD)
            setTextColor(Color.WHITE)
            setPadding(d(8f), d(15f), d(8f), d(15f))
            setOnClickListener {
                curTab = index
                if (index == 0) {
                    loadRecommend(activity)
                } else {
                    setStatus("最近播放 ${recents.size} 首")
                    renderList(activity, recents.toList())
                }
                refreshTabs()
            }
        }
        val tabRec = mkTab("推荐", 0)
        val tabRecent = mkTab("最近播放", 1)
        tabRow.addView(tabRec, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        tabRow.addView(tabRecent, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply { leftMargin = d(6f) })
        root.addView(tabRow, lp())

        refreshTabs = {
            val pairs = listOf(tabRec to 0, tabRecent to 1)
            for (pair in pairs) {
                val v = pair.first
                val on = pair.second == curTab
                v.background = GradientDrawable().apply {
                    setColor(Color.parseColor(if (on) "#550A84FF" else "#22FFFFFF"))
                    cornerRadius = d(9f).toFloat()
                }
            }
        }
        refreshTabs()

        // 状态
        statusView = TextView(activity).apply {
            text = currentSong?.let { "播放中：${it.name}" } ?: "输入关键词搜索，或稍候加载推荐"
            textSize = 12f
            setTextColor(Color.parseColor("#A9A9A9"))
            setPadding(0, d(4f), 0, d(6f))
        }
        root.addView(statusView, lp())

        // 歌曲列表（独立滚动，高度随屏幕自适应）
        listHost = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        val listScroll = ScrollView(activity).apply {
            isVerticalScrollBarEnabled = true
            addView(listHost, LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
            ))
        }
        val listH = minOf(d(300f), (activity.resources.displayMetrics.heightPixels * 0.30f).toInt())
        root.addView(listScroll, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, listH
        ))

        // 歌词（当前行，居中）
        lyricsView = TextView(activity).apply {
            text = ""
            textSize = 13f
            setTextColor(Color.parseColor("#CCFFFFFF"))
            gravity = Gravity.CENTER
            setSingleLine(true)
            ellipsize = android.text.TextUtils.TruncateAt.MARQUEE
            marqueeRepeatLimit = -1
            isSelected = true
            setPadding(0, d(6f), 0, d(2f))
        }
        root.addView(lyricsView, lp())

        // 播放控制：固定底栏（在歌单滚动区外面）
        root.addView(View(activity).apply {
            setBackgroundColor(Color.parseColor("#33FFFFFF"))
        }, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, d(1f)).apply {
            topMargin = d(8f); bottomMargin = d(8f)
        })
        val ctl = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        root.addView(ctl, lp())

        loadRecommend(activity)
        return root
    }

    private fun lp() = LinearLayout.LayoutParams(
        LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
    )

    private fun rounded(color: Int, radius: Float) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = radius
    }

    private fun setStatus(text: String) {
        main.post { if (statusView?.parent != null) statusView?.text = text }
    }

    // ---------- 搜索 / 推荐 ----------

    private fun doSearch(activity: Activity, keywords: String) {
        setStatus("搜索中：$keywords …")
        Thread({
            try {
                val songs = searchSongs(keywords)
                main.post {
                    if (songs.isEmpty()) setStatus("没有搜到「$keywords」")
                    else { setStatus("找到 ${songs.size} 首"); renderList(activity, songs) }
                }
            } catch (e: Throwable) { main.post { setStatus("搜索失败：${e.message}") } }
        }, "MusicSearch").start()
    }

    private fun loadRecommend(activity: Activity) {
        setStatus("加载推荐 …")
        Thread({
            try {
                val songs = recommendSongs()
                main.post {
                    if (songs.isEmpty()) setStatus("推荐加载失败")
                    else { setStatus("推荐 ${songs.size} 首"); renderList(activity, songs) }
                }
            } catch (e: Throwable) { main.post { setStatus("推荐加载失败：${e.message}") } }
        }, "MusicRec").start()
    }

    // ---------- 手机号登录 ----------

    /**
     * 设置页专用：弹一个登录对话框（二维码 + 手机号），
     * 内部复用音乐页那套登录流程，只是把承载控件临时指向对话框里的 View。
     */
    fun showLoginDialog(activity: Activity) {
        val d = activity.resources.displayMetrics.density
        fun dp(v: Float) = (v * d + .5f).toInt()

        val box = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16f), dp(12f), dp(16f), dp(12f))
        }
        val qrImg = ImageView(activity).apply {
            layoutParams = LinearLayout.LayoutParams(dp(180f), dp(180f)).apply {
                gravity = Gravity.CENTER_HORIZONTAL
            }
            scaleType = ImageView.ScaleType.FIT_CENTER
        }
        val qrUrlTv = TextView(activity).apply {
            textSize = 10f
            setTextColor(Color.parseColor("#8899A6"))
        }
        val st = TextView(activity).apply {
            text = if (cookie != null) "已登录，可播放 VIP 歌" else "点下面按钮获取二维码"
            textSize = 12f
            setTextColor(Color.parseColor("#CCCCCC"))
            setPadding(0, dp(6f), 0, dp(6f))
        }
        val phBox = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            visibility = View.GONE
        }
        val phRow = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        val phoneInput = EditText(activity).apply {
            hint = "手机号"
            inputType = android.text.InputType.TYPE_CLASS_PHONE
            setTextColor(Color.WHITE)
            setSingleLine(true)
        }
        val pwdInput = EditText(activity).apply {
            hint = "密码"
            inputType = android.text.InputType.TYPE_CLASS_TEXT or
                android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
            setTextColor(Color.WHITE)
            setSingleLine(true)
        }
        phRow.addView(phoneInput, LinearLayout.LayoutParams(0, dp(40f), 1f))
        phRow.addView(pwdInput, LinearLayout.LayoutParams(0, dp(40f), 1f))
        phBox.addView(phRow)
        phBox.addView(TextView(activity).apply {
            text = "手机号登录"
            textSize = 12f
            gravity = Gravity.CENTER
            setTextColor(Color.WHITE)
            setPadding(0, dp(8f), 0, dp(8f))
            isClickable = true
            setOnClickListener {
                val ph = phoneInput.text.toString().trim()
                val pw = pwdInput.text.toString()
                if (ph.isEmpty() || pw.isEmpty()) {
                    Toast.makeText(activity, "请输入手机号和密码", Toast.LENGTH_SHORT).show()
                } else {
                    doPhoneLogin(activity, ph, pw)
                }
            }
        })

        box.addView(qrImg)
        box.addView(qrUrlTv)
        box.addView(st)
        box.addView(phBox)

        val dlg = android.app.AlertDialog.Builder(activity)
            .setTitle("网易云登录")
            .setView(box)
            .setNeutralButton("手机号登录") { _, _ ->
                phBox.visibility = if (phBox.visibility == View.VISIBLE) View.GONE else View.VISIBLE
            }
            .setNegativeButton("关闭", null)
            .create()
        dlg.setOnDismissListener {
            // 关掉后把承载控件还原空，避免指向已销毁的 View
            qrImage = null; qrUrlView = null; qrStatus = null; phoneBox = null
        }
        dlg.show()

        // 复用原登录流程：把内部 View 临时指到对话框里的控件
        qrImage = qrImg
        qrUrlView = qrUrlTv
        qrStatus = st
        phoneBox = phBox
        if (cookie == null) startQrLogin(activity)
    }

    private fun togglePhoneLogin(activity: Activity, box: LinearLayout?) {
        box ?: return
        box.visibility = if (box.visibility == View.GONE) View.VISIBLE else View.GONE
    }

    private fun doPhoneLogin(activity: Activity, phone: String, password: String) {
        qrStatus?.text = "登录中…"
        Thread({
            try {
                val path = "/login/cellphone?phone=${URLEncoder.encode(phone, "UTF-8")}" +
                        "&password=${URLEncoder.encode(password, "UTF-8")}&md5_password="
                val j = JSONObject(httpGet(path, withCookie = false))
                val code = j.optInt("code", 0)
                val ck = j.optString("cookie", "")
                main.post {
                    if (code == 200 && ck.isNotEmpty()) {
                        saveCookie(ck)
                        loginBtn?.text = "已登录"
                        qrStatus?.text = "登录成功，可播放 VIP 歌"
                        phoneBox?.visibility = View.GONE
                        Toast.makeText(activity, "网易云登录成功", Toast.LENGTH_SHORT).show()
                        loadRecommend(activity)
                    } else {
                        qrStatus?.text = "登录失败：code=$code ${j.optString("message", j.optString("msg", ""))}"
                    }
                }
            } catch (e: Throwable) {
                main.post { qrStatus?.text = "登录失败：${e.message}" }
            }
        }, "MusicPhoneLogin").start()
    }

    // ---------- 扫码登录 ----------

    private fun startQrLogin(activity: Activity) {
        if (cookie != null) {
            Toast.makeText(activity, "已登录；如需换号请删除素材目录 music_cookie.txt", Toast.LENGTH_LONG).show()
            return
        }
        qrBox?.visibility = View.VISIBLE
        qrStatus?.text = "获取二维码…"
        Thread({
            try {
                val keyJson = JSONObject(httpGet("/login/qr/key", withCookie = false))
                val key = keyJson.optJSONObject("data")?.optString("unikey", "")
                if (key.isNullOrEmpty()) throw IllegalStateException("获取 key 失败")
                pollKey = key
                val create = JSONObject(httpGet("/login/qr/create?key=$key&qrimg=true", withCookie = false))
                val data = create.optJSONObject("data")
                val qrimg = data?.optString("qrimg", "") ?: ""
                val qrurl = data?.optString("qrurl", "") ?: ""
                val bmp = decodeDataUrl(qrimg)
                main.post {
                    qrImage?.setImageBitmap(bmp)
                    qrUrlView?.text = if (qrurl.isNotEmpty()) "扫码打不开？用网易云App扫；链接：$qrurl" else ""
                    qrStatus?.text = "请用网易云 App「扫一扫」"
                }
                startPolling(activity, key)
            } catch (e: Throwable) {
                main.post { qrStatus?.text = "二维码获取失败：${e.message}" }
            }
        }, "MusicQr").start()
    }

    private fun startPolling(activity: Activity, key: String) {
        polling = true
        val tick = object : Runnable {
            override fun run() {
                if (!polling) return
                Thread({
                    try {
                        val j = JSONObject(httpGet("/login/qr/check?key=$key", withCookie = false))
                        val code = j.optInt("code", 0)
                        val ck = j.optString("cookie", "")
                        main.post {
                            when (code) {
                                800 -> { qrStatus?.text = "二维码已过期，请重试" ; stopPolling() }
                                801 -> qrStatus?.text = "等待扫码…"
                                802 -> qrStatus?.text = "已扫码，请在手机上确认"
                                803 -> {
                                    stopPolling()
                                    if (ck.isNotEmpty()) {
                                        saveCookie(ck)
                                        loginBtn?.text = "已登录"
                                        qrStatus?.text = "登录成功，可播放 VIP 歌"
                                        qrBox?.visibility = View.GONE
                                        Toast.makeText(activity, "网易云登录成功", Toast.LENGTH_SHORT).show()
                                        loadRecommend(activity)
                                    } else {
                                        qrStatus?.text = "登录成功但未取到 cookie"
                                    }
                                }
                                else -> qrStatus?.text = "扫码状态：$code"
                            }
                        }
                    } catch (e: Throwable) {
                        main.post { qrStatus?.text = "查询失败：${e.message}" }
                    }
                }, "MusicQrPoll").start()
                if (polling) pollHandler.postDelayed(this, 2500)
            }
        }
        pollHandler.postDelayed(tick, 1500)
    }

    private fun stopPolling() { polling = false; pollHandler.removeCallbacksAndMessages(null) }

    private fun decodeDataUrl(dataUrl: String): android.graphics.Bitmap? {
        return try {
            val b64 = if (dataUrl.contains(",")) dataUrl.substringAfter(",") else dataUrl
            val bytes = Base64.decode(b64, Base64.DEFAULT)
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
        } catch (e: Throwable) { null }
    }

    // ---------- 网络 ----------

    private fun httpGet(path: String, withCookie: Boolean = true): String {
        var urlStr = apiBase + path
        if (withCookie) {
            cookie?.let {
                if (it.isNotEmpty()) {
                    urlStr += (if (path.contains("?")) "&" else "?") + "cookie=" + URLEncoder.encode(it, "UTF-8")
                }
            }
        }
        val conn = URL(urlStr).openConnection() as HttpURLConnection
        conn.connectTimeout = 15000
        conn.readTimeout = 20000
        conn.requestMethod = "GET"
        try {
            if (conn.responseCode != 200) throw IllegalStateException("HTTP ${conn.responseCode}")
            return conn.inputStream.bufferedReader().readText()
        } finally {
            conn.disconnect()
        }
    }

    private fun searchSongs(keywords: String): List<Song> {
        val kw = URLEncoder.encode(keywords, "UTF-8")
        val json = httpGet("/cloudsearch?keywords=$kw&type=1&limit=100")
        val arr = JSONObject(json).optJSONObject("result")?.optJSONArray("songs") ?: return emptyList()
        return parseSongs(arr, "artists")
    }

    private fun recommendSongs(): List<Song> {
        val json = httpGet("/personalized/newsong?limit=50")
        val arr = JSONObject(json).optJSONArray("result") ?: return emptyList()
        val out = mutableListOf<Song>()
        for (i in 0 until arr.length()) {
            val s = arr.optJSONObject(i)?.optJSONObject("song") ?: continue
            if (!keepFee(s.optInt("fee", 0))) continue
            val id = s.optLong("id", 0L)
            val name = s.optString("name", "")
            val artists = s.optJSONArray("artists")
            val artist = (0 until (artists?.length() ?: 0))
                .joinToString("/") { artists!!.optJSONObject(it)?.optString("name", "") ?: "" }
            if (id > 0 && name.isNotEmpty()) out.add(Song(id, name, artist))
        }
        return out
    }

    private fun parseSongs(arr: org.json.JSONArray, artistKey: String): List<Song> {
        val out = mutableListOf<Song>()
        for (i in 0 until arr.length()) {
            val s = arr.optJSONObject(i) ?: continue
            if (!keepFee(s.optInt("fee", 0))) continue
            val id = s.optLong("id", 0L)
            val name = s.optString("name", "")
            val artists = s.optJSONArray(artistKey)
            val artist = (0 until (artists?.length() ?: 0))
                .joinToString("/") { artists!!.optJSONObject(it)?.optString("name", "") ?: "" }
            if (id > 0 && name.isNotEmpty()) out.add(Song(id, name, artist))
        }
        return out
    }

    /** 未登录时隐藏 VIP/付费歌；登录后保留（VIP 账号可播放）。 */
    private fun keepFee(fee: Int): Boolean = if (cookie != null) true else fee == 0

    private fun songUrl(id: Long): String? {
        val data = JSONObject(httpGet("/song/url?id=$id")).optJSONArray("data") ?: return null
        if (data.length() == 0) return null
        val url = data.optJSONObject(0)?.optString("url", "")?.trim() ?: ""
        return if (url.startsWith("http://") || url.startsWith("https://")) url else null
    }

    // ---------- UI ----------

    private fun renderList(activity: Activity, songs: List<Song>) {
        playlist = songs
        main.post {
            val host = listHost ?: return@post
            host.removeAllViews()
            val dp = activity.resources.displayMetrics.density
            fun d(v: Float) = (v * dp + .5f).toInt()
            for (song in songs) {
                val row = TextView(activity).apply {
                    text = "${song.name}\n${song.artist}"
                    textSize = 14f
                    setTextColor(Color.WHITE)
                    setPadding(d(6f), d(7f), d(6f), d(7f))
                    background = GradientDrawable().apply {
                        setColor(Color.parseColor("#22FFFFFF"))
                        cornerRadius = d(8f).toFloat()
                    }
                    isClickable = true
                    setOnClickListener {
                        Toast.makeText(activity, "播放：${song.name}", Toast.LENGTH_SHORT).show()
                        play(activity, song)
                    }
                }
                host.addView(row, LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
                ).apply { bottomMargin = d(6f) })
            }
        }
    }

    private fun play(activity: Activity, song: Song) {
        try { addRecent(activity.applicationContext, song) } catch (e: Throwable) {}
        setStatus("获取播放地址：${song.name} …")
        Thread({
            try {
                val url = songUrl(song.id)
                if (url == null) {
                    main.post { setStatus("无播放地址（需登录 或 VIP 歌）：${song.name}") }
                    return@Thread
                }
                startPlay(song, url)
            } catch (e: Throwable) {
                main.post { setStatus("播放失败：${e.message}") }
            }
        }, "MusicPlay").start()
    }

    private fun startPlay(song: Song, url: String) {
        NowPlayingBar.refreshIcon()
        main.post {
            try {
                player?.release()
                val mp = MediaPlayer()
                mp.setAudioAttributes(
                    AudioAttributes.Builder()
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .build()
                )
                mp.setDataSource(url)
                mp.setOnPreparedListener {
                    it.start()
                    currentSong = song
                    NowPlayingBar.update("${song.name} - ${song.artist}", "")
                    setStatus("播放中：${song.name} - ${song.artist}")
                    fetchLyrics(song.id)
                    startLyricLoop()
                }
                mp.setOnErrorListener { _, what, extra ->
                    setStatus("播放失败($what/$extra)：${song.name}")
                    true
                }
                mp.setOnCompletionListener {
                    val activity = uiActivity
                    if (activity == null) {
                        setStatus("播放完成：${song.name}")
                        return@setOnCompletionListener
                    }
                    val list = playlist
                    val current = currentSong
                    when (playMode) {
                        MODE_SINGLE -> {
                            setStatus("单曲循环：${song.name} - ${song.artist}")
                            play(activity, song)
                        }
                        MODE_SHUFFLE -> {
                            val others = list.filter { it.id != song.id }
                            if (others.isEmpty()) {
                                setStatus("随机播放：${song.name}")
                                play(activity, song)
                            } else {
                                val next = others.random()
                                setStatus("随机播放：${next.name} - ${next.artist}")
                                play(activity, next)
                            }
                        }
                        else -> {
                            if (list.isEmpty()) {
                                setStatus("顺序播放：${song.name}")
                                play(activity, song)
                            } else {
                                val index = list.indexOfFirst { it.id == (current?.id ?: song.id) }
                                val next = if (index >= 0) list[(index + 1) % list.size] else list.first()
                                setStatus("顺序播放：${next.name} - ${next.artist}")
                                play(activity, next)
                            }
                        }
                    }
                }
                mp.prepareAsync()
                player = mp
            } catch (e: Throwable) {
                setStatus("播放失败：${e.message}")
            }
        }
    }

    private fun togglePause() {
        val mp = player ?: return
        try {
            if (mp.isPlaying) { mp.pause(); setStatus("已暂停") }
            else { mp.start(); setStatus("播放中") }
            NowPlayingBar.refreshIcon()
        } catch (e: Throwable) { setStatus("操作失败：${e.message}") }
    }

    // ---------- 歌词 ----------

    private fun fetchLyrics(id: Long) {
        lyricLines = emptyList()
        Thread({
            try {
                val j = JSONObject(httpGet("/lyric?id=$id"))
                val lrc = j.optJSONObject("lrc")?.optString("lyric", "") ?: ""
                val lines = parseLrc(lrc)
                main.post { lyricLines = lines }
            } catch (e: Throwable) {}
        }, "MusicLyric").start()
    }

    private fun parseLrc(lrc: String): List<Pair<Long, String>> {
        val out = mutableListOf<Pair<Long, String>>()
        val re = Regex("\\[(\\d+):(\\d+(?:\\.\\d+)?)\\](.*)")
        for (line in lrc.split("\n")) {
            val m = re.find(line) ?: continue
            val min = m.groupValues[1].toLongOrNull() ?: continue
            val sec = m.groupValues[2].toDoubleOrNull() ?: continue
            val text = m.groupValues[3].trim()
            if (text.isEmpty()) continue
            out.add((min * 60000L + (sec * 1000).toLong()) to text)
        }
        return out.sortedBy { it.first }
    }

    private fun startLyricLoop() {
        stopLyricLoop()
        val r = object : Runnable {
            override fun run() {
                val mp = player
                val lines = lyricLines
                if (mp != null && lines.isNotEmpty()) {
                    val pos = try { mp.currentPosition.toLong() } catch (e: Throwable) { 0L }
                    val cur = lines.lastOrNull { it.first <= pos }?.second ?: lines.first().second
                    lyricsView?.text = cur
                    lyricsView?.isSelected = true
                    currentSong?.let { NowPlayingBar.update("${it.name} - ${it.artist}", cur) }
                }
                main.postDelayed(this, 500)
            }
        }
        lyricRunnable = r
        main.postDelayed(r, 500)
    }

    private fun stopLyricLoop() {
        lyricRunnable?.let { main.removeCallbacks(it) }
        lyricRunnable = null
    }

    /** 切走分类：只解绑界面引用，不停止播放（打开 UI 不打断音乐）。 */
    fun detachUi() {
        statusView = null
        listHost = null
        qrBox = null
        qrImage = null
        qrStatus = null
        loginBtn = null
        lyricsView = null
        phoneBox = null
        qrUrlView = null
    }

    /** 手动停止：释放播放器并隐藏右上角正在播放条。 */
    /** 播放条单击用：暂停/继续。 */
    fun togglePlayPause() = togglePause()

    /** 播放条双击用：停止。 */
    fun isPlaying(): Boolean = try { player?.isPlaying == true } catch (e: Throwable) { false }

    fun stop() {
        stopPolling()
        NowPlayingBar.refreshIcon()
        stopLyricLoop()
        lyricLines = emptyList()
        try { player?.release() } catch (e: Throwable) {}
        player = null
        currentSong = null
        NowPlayingBar.hide()
        detachUi()
    }
}
