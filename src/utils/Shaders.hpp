#pragma once
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/shaders/CCGLProgram.h>
#include <Geode/cocos/shaders/CCShaderCache.h>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;

// shaders comunes que usamos por todos lados
// todo junto aquí pa no copiar/pegar código

namespace Shaders {

    inline CCGLProgram* getOrCreateShader(char const* key, char const* vertexSrc, char const* fragmentSrc) {
        auto shaderCache = CCShaderCache::sharedShaderCache();
        if (auto program = shaderCache->programForKey(key)) {
            return program;
        }

        auto program = new CCGLProgram();
        program->initWithVertexShaderByteArray(vertexSrc, fragmentSrc);
        program->addAttribute("a_position", kCCVertexAttrib_Position);
        program->addAttribute("a_color", kCCVertexAttrib_Color);
        program->addAttribute("a_texCoord", kCCVertexAttrib_TexCoords);

        if (!program->link()) {
            geode::log::error("failed to link shader: {}", key);
            program->release();
            return nullptr;
        }

        program->updateUniforms();
        shaderCache->addProgram(program, key);
        program->release();
        return program;
    }

constexpr auto vertexShaderCell =
R"(
attribute vec4 a_position;
attribute vec4 a_color;
attribute vec2 a_texCoord;

#ifdef GL_ES
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

void main()
{
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
})";

constexpr auto fragmentShaderHorizontal = R"(
#ifdef GL_ES
precision mediump float;
#endif

varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_radius;

void main() {
    float scaledRadius = u_radius * u_screenSize.y * 0.5;
    vec2 texOffset = 1.0 / u_screenSize;
    vec2 direction = vec2(texOffset.x, 0.0);

    vec3 result = texture2D(u_texture, v_texCoord).rgb;
    float weightSum = 1.0;
    float weight = 1.0;

    float fastScale = u_radius * 10.0 / ((u_radius * 10.0 + 1.0) * (u_radius * 10.0 + 1.0) - 1.0);
    scaledRadius *= fastScale;

    for (int i = 1; i < 64; i++) {
        if (float(i) >= scaledRadius) break;

        weight -= 1.0 / scaledRadius;
        if (weight <= 0.0) break;

        vec2 offset = direction * float(i);
        result += texture2D(u_texture, v_texCoord + offset).rgb * weight;
        result += texture2D(u_texture, v_texCoord - offset).rgb * weight;
        weightSum += 2.0 * weight;
    }

    result /= weightSum;
    gl_FragColor = vec4(result, 1.0) * v_fragmentColor;
})";

constexpr auto fragmentShaderVertical = R"(
#ifdef GL_ES
precision mediump float;
#endif

varying vec4 v_fragmentColor;
varying vec2 v_texCoord;

uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_radius;

void main() {
    float scaledRadius = u_radius * u_screenSize.y * 0.5;
    vec2 texOffset = 1.0 / u_screenSize;
    vec2 direction = vec2(0.0, texOffset.y);

    vec3 result = texture2D(u_texture, v_texCoord).rgb;
    float weightSum = 1.0;
    float weight = 1.0;

    float fastScale = u_radius * 10.0 / ((u_radius * 10.0 + 1.0) * (u_radius * 10.0 + 1.0) - 1.0);
    scaledRadius *= fastScale;

    for (int i = 1; i < 64; i++) {
        if (float(i) >= scaledRadius) break;

        weight -= 1.0 / scaledRadius;
        if (weight <= 0.0) break;

        vec2 offset = direction * float(i);
        result += texture2D(u_texture, v_texCoord + offset).rgb * weight;
        result += texture2D(u_texture, v_texCoord - offset).rgb * weight;
        weightSum += 2.0 * weight;
    }

    result /= weightSum;
    gl_FragColor = vec4(result, 1.0) * v_fragmentColor;
})";

// shader de escala de grises
constexpr auto fragmentShaderGrayscale = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 result = mix(color.rgb, vec3(gray), u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

// shader de sepia
constexpr auto fragmentShaderSepia = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 sepia;
    sepia.r = dot(color.rgb, vec3(0.393, 0.769, 0.189));
    sepia.g = dot(color.rgb, vec3(0.349, 0.686, 0.168));
    sepia.b = dot(color.rgb, vec3(0.272, 0.534, 0.131));
    vec3 result = mix(color.rgb, sepia, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

// shader de viñeta
constexpr auto fragmentShaderVignette = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    vec2 pos = v_texCoord - 0.5;
    float dist = length(pos);
    float vignette = smoothstep(0.8, 0.3 * (1.0 - u_intensity), dist);
    gl_FragColor = vec4(color.rgb * vignette, color.a) * v_fragmentColor;
})";

// shader de scanlines
constexpr auto fragmentShaderScanlines = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform vec2 u_screenSize;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float scanline = sin(v_texCoord.y * u_screenSize.y * 3.14159 * (1.0 + u_intensity * 2.0)) * 0.5 + 0.5;
    scanline = mix(1.0, scanline, u_intensity * 0.5);
    gl_FragColor = vec4(color.rgb * scanline, color.a) * v_fragmentColor;
})";

// shader de bloom
constexpr auto fragmentShaderBloom = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform vec2 u_screenSize;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 bloom = vec3(0.0);
    vec2 texOffset = 1.0 / u_screenSize;
    float radius = u_intensity * 3.0;
    
    for (float x = -radius; x <= radius; x += 1.0) {
        for (float y = -radius; y <= radius; y += 1.0) {
            vec2 offset = vec2(x, y) * texOffset;
            vec4 sample = texture2D(u_texture, v_texCoord + offset);
            float bright = max(max(sample.r, sample.g), sample.b);
            if (bright > 0.8) {
                bloom += sample.rgb * (bright - 0.8) * 5.0;
            }
        }
    }
    
    bloom /= (radius * 2.0 + 1.0) * (radius * 2.0 + 1.0);
    vec3 result = color.rgb + bloom * u_intensity * 0.5;
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

// shader de aberración cromática
constexpr auto fragmentShaderChromatic = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec2 offset = (v_texCoord - 0.5) * u_intensity * 0.01;
    float r = texture2D(u_texture, v_texCoord + offset).r;
    float g = texture2D(u_texture, v_texCoord).g;
    float b = texture2D(u_texture, v_texCoord - offset).b;
    float a = texture2D(u_texture, v_texCoord).a;
    gl_FragColor = vec4(r, g, b, a) * v_fragmentColor;
})";

// shader de blur radial
constexpr auto fragmentShaderRadialBlur = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec2 center = vec2(0.5, 0.5);
    vec2 direction = v_texCoord - center;
    vec4 color = vec4(0.0);
    float samples = 10.0 + u_intensity * 5.0;
    
    for (float i = 0.0; i < samples; i += 1.0) {
        float scale = 1.0 - (u_intensity * 0.05 * (i / samples));
        color += texture2D(u_texture, center + direction * scale);
    }
    
    color /= samples;
    gl_FragColor = color * v_fragmentColor;
})";

// shader de glitch
constexpr auto fragmentShaderGlitch = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_texCoord;
    float glitchStrength = u_intensity * 0.1;
    
    // random per-line horizontal displacement
    float lineNoise = rand(vec2(floor(uv.y * 100.0), 0.0));
    if (lineNoise > 0.95 - u_intensity * 0.05) {
        uv.x += (rand(vec2(uv.y, 0.0)) - 0.5) * glitchStrength;
    }
    
    vec4 color = texture2D(u_texture, uv);
    
    // color channel separation
    if (rand(vec2(uv.y, 1.0)) > 0.98 - u_intensity * 0.02) {
        color.r = texture2D(u_texture, uv + vec2(0.01 * u_intensity, 0.0)).r;
        color.b = texture2D(u_texture, uv - vec2(0.01 * u_intensity, 0.0)).b;
    }
    
    gl_FragColor = color * v_fragmentColor;
})";

// shader de pixelado (para GIFs)
constexpr auto fragmentShaderPixelate = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_intensity;

void main() {
    float pixelSize = 2.0 + u_intensity * 15.0;
    vec2 coord = floor(v_texCoord * u_screenSize / pixelSize) * pixelSize / u_screenSize;
    gl_FragColor = texture2D(u_texture, coord) * v_fragmentColor;
})";

// shader de blur simple (para GIFs)
// Dual Kawase Blur, el truco típico de juegos grandes
// buen balance entre calidad y rendimiento
constexpr auto fragmentShaderBlurSinglePass = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_screenSize;
uniform float u_intensity;

void main() {
    vec2 texelSize = 1.0 / u_screenSize;
    float blurAmount = u_intensity * 3.0 + 1.0;

    vec2 halfpixel = (blurAmount * 0.5) * texelSize;
    vec2 offset = blurAmount * texelSize;

    // Dual Kawase sampling pattern - 12 samples ponderados
    vec3 color = texture2D(u_texture, v_texCoord).rgb * 4.0;

    // Diagonales cercanas
    color += texture2D(u_texture, v_texCoord - halfpixel).rgb;
    color += texture2D(u_texture, v_texCoord + halfpixel).rgb;
    color += texture2D(u_texture, v_texCoord + vec2(halfpixel.x, -halfpixel.y)).rgb;
    color += texture2D(u_texture, v_texCoord - vec2(halfpixel.x, -halfpixel.y)).rgb;

    // Cardinales con peso extra
    color += texture2D(u_texture, v_texCoord + vec2(-offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2( offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, -offset.y)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0,  offset.y)).rgb * 2.0;

    // Alpha siempre 1.0 para fondos opacos
    gl_FragColor = vec4(color / 16.0, 1.0) * v_fragmentColor;
})";

// shader de posterize
constexpr auto fragmentShaderPosterize = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 color = texture2D(u_texture, v_texCoord);
    float levels = mix(32.0, 3.0, u_intensity);
    vec3 result;
    result.r = floor(color.r * levels) / levels;
    result.g = floor(color.g * levels) / levels;
    result.b = floor(color.b * levels) / levels;
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

// shaders pensados para celdas (normalmente post-tint)

constexpr auto fragmentShaderGrayscaleCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 result = mix(color.rgb, vec3(gray), u_intensity);
    gl_FragColor = vec4(result, color.a);
})";

constexpr auto fragmentShaderInvertCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    vec3 inverted = vec3(1.0) - color.rgb;
    vec3 result = mix(color.rgb, inverted, u_intensity);
    gl_FragColor = vec4(result, color.a);
})";

constexpr auto fragmentShaderBlurCell = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

// Blur de alta calidad - 9x9 samples con pesos gaussianos
// Usa offsets fraccionarios para aprovechar linear filtering de la GPU
void main() {
    vec2 texelSize = 1.0 / u_texSize;
    float radius = u_intensity * 4.0 + 0.5;
    vec2 step = texelSize * radius;

    // Pesos gaussianos precalculados (sigma ~2.0)
    // Centro
    vec3 color = texture2D(u_texture, v_texCoord).rgb * 0.1621;

    // Cruz principal (peso alto)
    color += texture2D(u_texture, v_texCoord + vec2(step.x, 0.0)).rgb * 0.1408;
    color += texture2D(u_texture, v_texCoord - vec2(step.x, 0.0)).rgb * 0.1408;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, step.y)).rgb * 0.1408;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, step.y)).rgb * 0.1408;

    // Diagonales cercanas
    color += texture2D(u_texture, v_texCoord + vec2(step.x, step.y) * 0.7071).rgb * 0.0911;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x, step.y) * 0.7071).rgb * 0.0911;
    color += texture2D(u_texture, v_texCoord + vec2(step.x, -step.y) * 0.7071).rgb * 0.0911;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x, -step.y) * 0.7071).rgb * 0.0911;

    // Segunda capa - cruz lejana
    vec2 step2 = step * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(step2.x, 0.0)).rgb * 0.0215;
    color += texture2D(u_texture, v_texCoord - vec2(step2.x, 0.0)).rgb * 0.0215;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, step2.y)).rgb * 0.0215;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, step2.y)).rgb * 0.0215;

    // Diagonales lejanas
    color += texture2D(u_texture, v_texCoord + step2 * 0.7071).rgb * 0.0108;
    color += texture2D(u_texture, v_texCoord + vec2(-step2.x, step2.y) * 0.7071).rgb * 0.0108;
    color += texture2D(u_texture, v_texCoord + vec2(step2.x, -step2.y) * 0.7071).rgb * 0.0108;
    color += texture2D(u_texture, v_texCoord - step2 * 0.7071).rgb * 0.0108;

    // Puntos intermedios para suavizar (aprovecha linear filtering)
    vec2 step15 = step * 1.5;
    color += texture2D(u_texture, v_texCoord + vec2(step15.x, step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step15.x, step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(step15.x, -step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step15.x, -step.y * 0.5)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(step.x * 0.5, step15.y)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x * 0.5, step15.y)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(step.x * 0.5, -step15.y)).rgb * 0.0156;
    color += texture2D(u_texture, v_texCoord + vec2(-step.x * 0.5, -step15.y)).rgb * 0.0156;

    gl_FragColor = vec4(color, 1.0) * v_fragmentColor;
})";

// High quality two-pass Gaussian blur - Horizontal pass
// Usa linear sampling para mejor rendimiento (13-tap con solo 7 samples)
constexpr auto fragmentShaderBlurCellH = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 texelSize = 1.0 / u_texSize;
    float blurRadius = u_intensity * 3.0 + 0.5;

    // Pesos Gaussianos optimizados para linear sampling (sigma ~3.0)
    const float weight0 = 0.227027027;
    const float weight1 = 0.316216216;
    const float weight2 = 0.070270270;

    // Offsets calculados para linear sampling optimo
    const float offset1 = 1.384615385;
    const float offset2 = 3.230769231;

    float dx = texelSize.x * blurRadius;

    vec3 color = texture2D(u_texture, v_texCoord).rgb * weight0;

    color += texture2D(u_texture, v_texCoord + vec2(dx * offset1, 0.0)).rgb * weight1;
    color += texture2D(u_texture, v_texCoord - vec2(dx * offset1, 0.0)).rgb * weight1;
    color += texture2D(u_texture, v_texCoord + vec2(dx * offset2, 0.0)).rgb * weight2;
    color += texture2D(u_texture, v_texCoord - vec2(dx * offset2, 0.0)).rgb * weight2;

    // Alpha siempre 1.0 para fondos opacos
    gl_FragColor = vec4(color, 1.0) * v_fragmentColor;
})";

// High quality two-pass Gaussian blur - Vertical pass
// Usa linear sampling para mejor rendimiento (13-tap con solo 7 samples)
constexpr auto fragmentShaderBlurCellV = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 texelSize = 1.0 / u_texSize;
    float blurRadius = u_intensity * 3.0 + 0.5;

    // Pesos Gaussianos optimizados para linear sampling (sigma ~3.0)
    const float weight0 = 0.227027027;
    const float weight1 = 0.316216216;
    const float weight2 = 0.070270270;

    // Offsets calculados para linear sampling optimo
    const float offset1 = 1.384615385;
    const float offset2 = 3.230769231;

    float dy = texelSize.y * blurRadius;

    vec3 color = texture2D(u_texture, v_texCoord).rgb * weight0;

    color += texture2D(u_texture, v_texCoord + vec2(0.0, dy * offset1)).rgb * weight1;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, dy * offset1)).rgb * weight1;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, dy * offset2)).rgb * weight2;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, dy * offset2)).rgb * weight2;

    // Alpha siempre 1.0 para fondos opacos
    gl_FragColor = vec4(color, 1.0) * v_fragmentColor;
})";

constexpr auto fragmentShaderGlitchCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_texCoord;
    float glitchStrength = u_intensity * 0.1;
    
    float lineNoise = rand(vec2(floor(uv.y * 100.0), u_time));
    if (lineNoise > 0.95 - u_intensity * 0.05) {
        uv.x += (rand(vec2(uv.y, u_time)) - 0.5) * glitchStrength;
    }
    
    vec4 color = texture2D(u_texture, uv);
    
    if (rand(vec2(uv.y, u_time + 1.0)) > 0.98 - u_intensity * 0.02) {
        color.r = texture2D(u_texture, uv + vec2(0.01 * u_intensity, 0.0)).r;
        color.b = texture2D(u_texture, uv - vec2(0.01 * u_intensity, 0.0)).b;
    }
    
    gl_FragColor = color * v_fragmentColor;
})";

constexpr auto fragmentShaderSepiaCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 sepia = vec3(gray * 1.2, gray * 1.0, gray * 0.8);
    vec3 result = mix(color.rgb, sepia, u_intensity);
    gl_FragColor = vec4(result, color.a);
})";

constexpr auto fragmentShaderSaturationCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity; // saturation: 1.0 = normal
uniform float u_brightness; // brightness: 1.0 = normal

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    
    // saturation
    float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    vec3 grayColor = vec3(gray);
    vec3 saturated = mix(grayColor, color.rgb, u_intensity);
    
    // brightness
    vec3 finalRGB = saturated * u_brightness;
    
    gl_FragColor = vec4(finalRGB, color.a);
})";

constexpr auto fragmentShaderSharpenCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 onePixel = vec2(1.0, 1.0) / u_texSize;
    vec4 color = texture2D(u_texture, v_texCoord);
    
    vec4 sum = vec4(0.0);
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, -1.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(-1.0, 0.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, 0.0) * onePixel) * 5.0;
    sum += texture2D(u_texture, v_texCoord + vec2(1.0, 0.0) * onePixel) * -1.0;
    sum += texture2D(u_texture, v_texCoord + vec2(0.0, 1.0) * onePixel) * -1.0;
    
    vec3 result = mix(color.rgb, sum.rgb, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

constexpr auto fragmentShaderEdgeCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec2 onePixel = vec2(1.0, 1.0) / u_texSize;
    
    float kernel[9];
    kernel[0] = -1.0; kernel[1] = -1.0; kernel[2] = -1.0;
    kernel[3] = -1.0; kernel[4] = 8.0; kernel[5] = -1.0;
    kernel[6] = -1.0; kernel[7] = -1.0; kernel[8] = -1.0;
    
    vec4 sum = vec4(0.0);
    int index = 0;
    for (float y = -1.0; y <= 1.0; y++) {
        for (float x = -1.0; x <= 1.0; x++) {
            sum += texture2D(u_texture, v_texCoord + vec2(x, y) * onePixel) * kernel[index];
            index++;
        }
    }
    
    vec4 color = texture2D(u_texture, v_texCoord);
    vec3 result = mix(color.rgb, sum.rgb, u_intensity);
    gl_FragColor = vec4(result, color.a) * v_fragmentColor;
})";

constexpr auto fragmentShaderVignetteCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    vec2 uv = v_texCoord - 0.5;
    float dist = length(uv);
    float vignette = smoothstep(0.8, 0.25 * (1.0 - u_intensity * 0.5), dist);
    gl_FragColor = vec4(color.rgb * vignette, color.a);
})";

constexpr auto fragmentShaderPixelateCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    float pixelSize = 2.0 + u_intensity * 15.0;
    vec2 coord = floor(v_texCoord * u_texSize / pixelSize) * pixelSize / u_texSize;
    gl_FragColor = texture2D(u_texture, coord) * v_fragmentColor;
})";

constexpr auto fragmentShaderPosterizeCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float levels = 10.0 - (u_intensity * 8.0);
    levels = max(2.0, levels);
    vec3 result = floor(color.rgb * levels) / levels;
    gl_FragColor = vec4(result, color.a);
})";

constexpr auto fragmentShaderChromaticCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec2 uv = v_texCoord;
    float amount = u_intensity * 0.02;
    float r = texture2D(u_texture, uv + vec2(amount, 0.0)).r;
    float g = texture2D(u_texture, uv).g;
    float b = texture2D(u_texture, uv - vec2(amount, 0.0)).b;
    gl_FragColor = vec4(r, g, b, texture2D(u_texture, uv).a) * v_fragmentColor;
})";

constexpr auto fragmentShaderScanlinesCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float scanline = sin(v_texCoord.y * u_texSize.y * 0.5) * 0.1 * u_intensity;
    color.rgb -= scanline;
    gl_FragColor = color;
})";

constexpr auto fragmentShaderSolarizeCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    float threshold = 0.5;
    vec3 solarized = abs(color.rgb - vec3(threshold)) * 2.0;
    vec3 result = mix(color.rgb, solarized, u_intensity);
    gl_FragColor = vec4(result, color.a);
})";

// simple hue shift for rainbow
constexpr auto fragmentShaderRainbowCell = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_intensity;
uniform float u_time;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    vec4 texColor = texture2D(u_texture, v_texCoord);
    vec4 color = texColor * v_fragmentColor;
    vec3 hsv = rgb2hsv(color.rgb);
    // shift hue based on time and intensity
    hsv.x = fract(hsv.x + u_time * 0.5 * u_intensity); 
    vec3 rgb = hsv2rgb(hsv);
    // mix with original based on intensity (so it fades in)
    vec3 result = mix(color.rgb, rgb, u_intensity);
    gl_FragColor = vec4(result, color.a);
})";


constexpr auto fragmentShaderAtmosphere = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D u_texture;
uniform vec2 u_texSize;
uniform float u_intensity; // 0.0 to 1.0

void main() {
    // Kawase-style blur optimizado para atmosfera
    vec2 texelSize = 1.0 / u_texSize;
    float blurAmount = u_intensity * 4.0 + 1.0;

    vec2 offset = blurAmount * texelSize;
    vec2 halfOffset = offset * 0.5;

    // Centro con peso alto
    vec3 color = texture2D(u_texture, v_texCoord).rgb * 4.0;

    // Diagonales cercanas
    color += texture2D(u_texture, v_texCoord + halfOffset).rgb;
    color += texture2D(u_texture, v_texCoord - halfOffset).rgb;
    color += texture2D(u_texture, v_texCoord + vec2(halfOffset.x, -halfOffset.y)).rgb;
    color += texture2D(u_texture, v_texCoord + vec2(-halfOffset.x, halfOffset.y)).rgb;

    // Cardinales
    color += texture2D(u_texture, v_texCoord + vec2(offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord - vec2(offset.x, 0.0)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord + vec2(0.0, offset.y)).rgb * 2.0;
    color += texture2D(u_texture, v_texCoord - vec2(0.0, offset.y)).rgb * 2.0;

    // Alpha siempre 1.0 para fondos opacos
    gl_FragColor = vec4(color / 16.0, 1.0) * v_fragmentColor;
})";


    constexpr auto fragmentShaderFastBlur = R"(
    #ifdef GL_ES
    precision highp float;
    #endif
    varying vec4 v_fragmentColor;
    varying vec2 v_texCoord;
    uniform sampler2D u_texture;
    uniform vec2 u_texSize; 
    
    void main() {
        vec2 texelSize = 1.0 / u_texSize;
        float blurAmount = 3.5; // Intensidad fija para fondos

        vec2 halfpixel = (blurAmount * 0.5) * texelSize;
        vec2 offset = blurAmount * texelSize;

        // Dual Kawase Blur - metodo profesional de juegos AAA
        vec3 color = texture2D(u_texture, v_texCoord).rgb * 4.0;

        // Diagonales cercanas
        color += texture2D(u_texture, v_texCoord - halfpixel).rgb;
        color += texture2D(u_texture, v_texCoord + halfpixel).rgb;
        color += texture2D(u_texture, v_texCoord + vec2(halfpixel.x, -halfpixel.y)).rgb;
        color += texture2D(u_texture, v_texCoord - vec2(halfpixel.x, -halfpixel.y)).rgb;

        // Cardinales con peso extra para suavidad
        color += texture2D(u_texture, v_texCoord + vec2(-offset.x, 0.0)).rgb * 2.0;
        color += texture2D(u_texture, v_texCoord + vec2( offset.x, 0.0)).rgb * 2.0;
        color += texture2D(u_texture, v_texCoord + vec2(0.0, -offset.y)).rgb * 2.0;
        color += texture2D(u_texture, v_texCoord + vec2(0.0,  offset.y)).rgb * 2.0;

        // Alpha siempre 1.0 para fondos opacos
        gl_FragColor = vec4(color / 16.0, 1.0) * v_fragmentColor;
    })";

    inline void applyBlurPass(CCSprite* input, CCRenderTexture* output, CCGLProgram* program, CCSize const& size, float radius) {
        input->setShaderProgram(program);
        input->setPosition(size * 0.5f);

        program->use();
        program->setUniformsForBuiltins();
        program->setUniformLocationWith2f(
            program->getUniformLocationForName("u_screenSize"),
            size.width, size.height
        );
        program->setUniformLocationWith1f(
            program->getUniformLocationForName("u_radius"),
            radius
        );

        output->begin();
        input->visit();
        output->end();
    }

/// convierte intensidad (1–10) a radio de blur para shaders multi‑paso
    inline float intensityToBlurRadius(float intensity) {
        float normalized = std::clamp((intensity - 1.0f) / 9.0f, 0.0f, 1.0f);
        return 0.02f + (normalized * 0.20f);
    }

    /// crea un sprite con blur gaussiano multi‑paso; toda la magia del blur vive aquí
    /// @param texture textura fuente
    /// @param targetSize tamaño del área a blurear
    /// @param intensity 1‑10 (slider típico), controla qué tan fuerte pega
    /// @param useDirectRadius si true, intensity se usa como radio directo (ej. LevelSelectLayer con 4.0)
    inline CCSprite* createBlurredSprite(CCTexture2D* texture, CCSize const& targetSize, float intensity, bool useDirectRadius = false) {
        if (!texture) return nullptr;

        auto srcSprite = CCSprite::createWithTexture(texture);
        if (!srcSprite) return nullptr;

        float scaleX = targetSize.width / texture->getContentSize().width;
        float scaleY = targetSize.height / texture->getContentSize().height;
        float scale = std::max(scaleX, scaleY);

        srcSprite->setScale(scale);
        srcSprite->setAnchorPoint({0.5f, 0.5f});
        srcSprite->setPosition(targetSize * 0.5f);

        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        texture->setTexParameters(&params);

        auto blurH = getOrCreateShader("blur-horizontal"_spr, vertexShaderCell, fragmentShaderHorizontal);
        auto blurV = getOrCreateShader("blur-vertical"_spr, vertexShaderCell, fragmentShaderVertical);

        if (!blurH || !blurV) {
            return srcSprite;
        }

        auto rtA = CCRenderTexture::create(targetSize.width, targetSize.height);
        auto rtB = CCRenderTexture::create(targetSize.width, targetSize.height);

        if (!rtA || !rtB) {
            return srcSprite;
        }

        float radius = useDirectRadius ? intensity : intensityToBlurRadius(intensity);

        applyBlurPass(srcSprite, rtA, blurH, targetSize, radius);

        auto midSprite = CCSprite::createWithTexture(rtA->getSprite()->getTexture());
        midSprite->setFlipY(true);
        midSprite->setAnchorPoint({0.5f, 0.5f});
        midSprite->setPosition(targetSize * 0.5f);
        midSprite->getTexture()->setTexParameters(&params);

        applyBlurPass(midSprite, rtB, blurV, targetSize, radius);

        if (!useDirectRadius && intensity > 5.0f) {
            auto mid2 = CCSprite::createWithTexture(rtB->getSprite()->getTexture());
            mid2->setFlipY(true);
            mid2->setAnchorPoint({0.5f, 0.5f});
            mid2->setPosition(targetSize * 0.5f);
            mid2->getTexture()->setTexParameters(&params);

            applyBlurPass(mid2, rtA, blurH, targetSize, radius * 0.8f);

            auto mid3 = CCSprite::createWithTexture(rtA->getSprite()->getTexture());
            mid3->setFlipY(true);
            mid3->setAnchorPoint({0.5f, 0.5f});
            mid3->setPosition(targetSize * 0.5f);
            mid3->getTexture()->setTexParameters(&params);

            applyBlurPass(mid3, rtB, blurV, targetSize, radius * 0.8f);
        }

        auto finalSprite = CCSprite::createWithTexture(rtB->getSprite()->getTexture());
        finalSprite->setFlipY(true);
        finalSprite->setAnchorPoint({0.5f, 0.5f});
        finalSprite->getTexture()->setTexParameters(&params);

        return finalSprite;
    }

    /// shader de blur single‑pass para GIFs; solo ajusta intensidad en el sprite (m_intensity)
    inline CCGLProgram* getBlurCellShader() {
        return getOrCreateShader("paimon_cell_blur", vertexShaderCell, fragmentShaderBlurCell);
    }

    /// shader blur single‑pass para fondos GIF (LevelInfoLayer), mismo rollo: solo intensidad
    inline CCGLProgram* getBlurSinglePassShader() {
        return getOrCreateShader("blur-single"_spr, vertexShaderCell, fragmentShaderBlurSinglePass);
    }

}
