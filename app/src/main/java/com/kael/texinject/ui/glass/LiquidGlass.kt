/*
 * 液态玻璃效果（AGSL RuntimeShader 实现）。
 *
 * 着色器算法移植自 Kyant0/AndroidLiquidGlass (backdrop)：
 *   https://github.com/Kyant0/AndroidLiquidGlass
 *   Copyright 2025 Kyant
 *   Licensed under the Apache License, Version 2.0
 *   原文件: backdrop/src/commonMain/kotlin/com/kyant/backdrop/internal/Shaders.kt
 *
 * 本文件仅保留 Android(AGSL) 部分并做了裁剪，以适配本模块的传统 View / Compose 场景。
 */
package com.kael.texinject.ui.glass

import android.graphics.RenderEffect
import android.graphics.RuntimeShader

/**
 * 圆角矩形折射 + 色散（chromatic dispersion）。
 *
 * `content` 是输入着色器名 —— 通过
 * `RenderEffect.createRuntimeShaderEffect(shader, "content")` 把目标内容喂进来，
 * 着色器在边缘按 SDF 梯度做偏移采样，形成"液态玻璃"的折射与色散。
 */
private const val ROUNDED_RECT_SDF = """
float radiusAt(float2 coord, float4 radii) {
    if (coord.x >= 0.0) {
        if (coord.y <= 0.0) return radii.y;
        else return radii.z;
    } else {
        if (coord.y <= 0.0) return radii.x;
        else return radii.w;
    }
}

float sdRoundedRect(float2 coord, float2 halfSize, float radius) {
    float2 cornerCoord = abs(coord) - (halfSize - float2(radius));
    float outside = length(max(cornerCoord, 0.0)) - radius;
    float inside = min(max(cornerCoord.x, cornerCoord.y), 0.0);
    return outside + inside;
}

float2 gradSdRoundedRect(float2 coord, float2 halfSize, float radius) {
    float2 cornerCoord = abs(coord) - (halfSize - float2(radius));
    if (cornerCoord.x >= 0.0 || cornerCoord.y >= 0.0) {
        return sign(coord) * normalize(max(cornerCoord, 0.0));
    } else {
        float gradX = step(cornerCoord.y, cornerCoord.x);
        return sign(coord) * float2(gradX, 1.0 - gradX);
    }
}
"""

private const val REFRACTION_WITH_DISPERSION = """
uniform shader content;

uniform float2 size;
uniform float2 offset;
uniform float4 cornerRadii;
uniform float refractionHeight;
uniform float refractionAmount;
uniform float depthEffect;
uniform float chromaticAberration;

$ROUNDED_RECT_SDF

float circleMap(float x) {
    return 1.0 - sqrt(1.0 - x * x);
}

half4 main(float2 coord) {
    float2 halfSize = size * 0.5;
    float2 centeredCoord = (coord + offset) - halfSize;
    float radius = radiusAt(coord, cornerRadii);

    float sd = sdRoundedRect(centeredCoord, halfSize, radius);
    if (-sd >= refractionHeight) {
        return content.eval(coord);
    }
    sd = min(sd, 0.0);

    float d = circleMap(1.0 - -sd / refractionHeight) * refractionAmount;
    float gradRadius = min(radius * 1.5, min(halfSize.x, halfSize.y));
    float2 grad = normalize(gradSdRoundedRect(centeredCoord, halfSize, gradRadius) + depthEffect * normalize(centeredCoord));

    float2 refractedCoord = coord + d * grad;
    float dispersionIntensity = chromaticAberration * ((centeredCoord.x * centeredCoord.y) / (halfSize.x * halfSize.y));
    float2 dispersedCoord = d * grad * dispersionIntensity;

    half4 color = half4(0.0);

    half4 red = content.eval(refractedCoord + dispersedCoord);
    color.r += red.r / 3.5;
    color.a += red.a / 7.0;

    half4 orange = content.eval(refractedCoord + dispersedCoord * (2.0 / 3.0));
    color.r += orange.r / 3.5;
    color.g += orange.g / 7.0;
    color.a += orange.a / 7.0;

    half4 yellow = content.eval(refractedCoord + dispersedCoord * (1.0 / 3.0));
    color.r += yellow.r / 3.5;
    color.g += yellow.g / 3.5;
    color.a += yellow.a / 7.0;

    half4 green = content.eval(refractedCoord);
    color.g += green.g / 3.5;
    color.a += green.a / 7.0;

    half4 cyan = content.eval(refractedCoord - dispersedCoord * (1.0 / 3.0));
    color.g += cyan.g / 3.5;
    color.b += cyan.b / 3.0;
    color.a += cyan.a / 7.0;

    half4 blue = content.eval(refractedCoord - dispersedCoord * (2.0 / 3.0));
    color.b += blue.b / 3.0;
    color.a += blue.a / 7.0;

    half4 purple = content.eval(refractedCoord - dispersedCoord);
    color.r += purple.r / 7.0;
    color.b += purple.b / 3.0;
    color.a += purple.a / 7.0;

    return color;
}
"""

/** 边缘高光（DefaultHighlightStyle，BlendMode.Plus）—— 玻璃质感的关键。 */
private const val DEFAULT_HIGHLIGHT = """
// 声明但不用：RenderEffect.createRuntimeShaderEffect(shader, "content")
// 要求着色器里必须存在这个 uniform，否则启动就抛 IllegalArgumentException。
uniform shader content;

uniform float2 size;
uniform float4 cornerRadii;
layout(color) uniform half4 color;
uniform float angle;
uniform float falloff;

$ROUNDED_RECT_SDF

half4 main(float2 coord) {
    float2 halfSize = size * 0.5;
    float2 centeredCoord = coord - halfSize;
    float radius = radiusAt(coord, cornerRadii);

    float gradRadius = min(radius * 1.5, min(halfSize.x, halfSize.y));
    float2 grad = gradSdRoundedRect(centeredCoord, halfSize, gradRadius);
    float2 normal = float2(cos(angle), sin(angle));
    float d = dot(grad, normal);
    float intensity = pow(abs(d), falloff);
    return color * intensity;
}
"""

/**
 * 液态玻璃效果构建器。
 *
 * 用法：`view.setRenderEffect(LiquidGlass.effect(w, h, cornerPx))`
 * 或 Compose：`Modifier.graphicsLayer { renderEffect = ...asComposeRenderEffect() }`
 */
object LiquidGlass {

    @Volatile
    private var shader: RuntimeShader? = null

    private fun shader(): RuntimeShader =
        shader ?: synchronized(this) {
            shader ?: RuntimeShader(REFRACTION_WITH_DISPERSION).also { shader = it }
        }

    /**
     * @param widthPx        目标宽度
     * @param heightPx       目标高度
     * @param cornerRadiusPx 圆角半径
     * @param refractionPx   折射带宽度（越大边缘越"厚"）
     * @param amountPx       折射位移量（越大越扭曲）
     * @param dispersion     色散强度（0 = 关闭）
     */
    fun effect(
        widthPx: Float,
        heightPx: Float,
        cornerRadiusPx: Float,
        paddingPx: Float = 0f,
        refractionPx: Float = 24f,
        amountPx: Float = 18f,
        depthEffect: Boolean = true,
        dispersion: Float = 0.35f,
    ): RenderEffect {
        val s = shader()
        s.setFloatUniform("size", widthPx, heightPx)
        // 上游约定：offset 取 -padding
        s.setFloatUniform("offset", -paddingPx, -paddingPx)
        s.setFloatUniform("cornerRadii", cornerRadiusPx, cornerRadiusPx, cornerRadiusPx, cornerRadiusPx)
        s.setFloatUniform("refractionHeight", refractionPx)
        // 上游约定：refractionAmount 取负
        s.setFloatUniform("refractionAmount", -amountPx)
        // 上游约定：depthEffect 是 0/1 布尔
        s.setFloatUniform("depthEffect", if (depthEffect) 1f else 0f)
        s.setFloatUniform("chromaticAberration", dispersion)
        return RenderEffect.createRuntimeShaderEffect(s, "content")
    }

    @Volatile
    private var hlShader: RuntimeShader? = null

    /**
     * 边缘高光。angleDeg 默认 -45°，与上游 DefaultHighlightStyle 一致。
     * 用法：叠在玻璃层之上，BlendMode.Plus。
     */
    fun highlight(
        widthPx: Float,
        heightPx: Float,
        cornerRadiusPx: Float,
        color: Int,
        angleDeg: Float = -45f,
        falloff: Float = 1f,
    ): RenderEffect {
        val s = hlShader ?: synchronized(this) {
            hlShader ?: RuntimeShader(DEFAULT_HIGHLIGHT).also { hlShader = it }
        }
        s.setFloatUniform("size", widthPx, heightPx)
        s.setFloatUniform("cornerRadii", cornerRadiusPx, cornerRadiusPx, cornerRadiusPx, cornerRadiusPx)
        s.setColorUniform("color", color)
        s.setFloatUniform("angle", angleDeg * (Math.PI / 180.0).toFloat())
        s.setFloatUniform("falloff", falloff)
        return RenderEffect.createRuntimeShaderEffect(s, "content")
    }
}
