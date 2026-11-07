package com.kael.texinject

import android.content.pm.ActivityInfo
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import com.kael.texinject.ui.compose.PanelOverlay
import com.kael.texinject.ui.compose.TexInjectTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        setContent {
            TexInjectTheme {
                PreviewScreen()
            }
        }
    }
}

@Composable
private fun PreviewScreen() {
    Box(
        Modifier
            .fillMaxSize()
            .background(Color(0xFF101012))
    ) {
        PanelOverlay(LocalContext.current as ComponentActivity, previewMode = true)
    }
}
