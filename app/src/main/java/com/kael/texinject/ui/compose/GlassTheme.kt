package com.kael.texinject.ui.compose

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val Accent = Color(0xFF0A84FF)
private val AccentDark = Color(0xFF0066CC)

private val DarkScheme = darkColorScheme(
    primary = Accent,
    onPrimary = Color.White,
    secondary = AccentDark,
    background = Color(0xF2121214),
    surface = Color(0xB31E1E22),
    onBackground = Color.White,
    onSurface = Color.White,
)

private val LightScheme = lightColorScheme(
    primary = Accent,
    onPrimary = Color.White,
    secondary = AccentDark,
    background = Color(0xE6F9FAFC),
    surface = Color(0xB3F9FAFC),
    onBackground = Color(0xFF1C1C1E),
    onSurface = Color(0xFF1C1C1E),
)

@Composable
fun TexInjectTheme(
    dark: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = if (dark) DarkScheme else LightScheme,
        content = content
    )
}
