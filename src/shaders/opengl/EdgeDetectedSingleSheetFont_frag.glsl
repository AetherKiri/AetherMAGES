in vec2 uv;
in vec4 tint;
in vec2 maskUV;

out vec4 color;

uniform sampler2D Font;
uniform sampler2D Mask;
uniform bool HasMask;
uniform vec2 PixelOffset;
uniform float DifferenceFactor;
uniform float IntensityShift;
uniform float AlphaShift;

float getIntensity(float texValue, float valueShift, float difference) {
    // Constant atlas regions have no local gradient. Keep the transition
    // finite, including texValue == valueShift, which otherwise produces 0/0
    // and leaves glyph coverage dependent on the GPU's NaN handling.
    float transitionWidth = max(2.0 * difference * DifferenceFactor, 0.000001);
    float intensity = clamp(0.5 + (texValue - valueShift) / transitionWidth, 0.0, 1.0);
    return (3.0 - 2.0 * intensity) * pow(intensity, 2.0);
}

void main() {
    float fontValue = texture(Font, uv).r;
    float maskValue = HasMask ? texture(Mask, maskUV).r : 1.0;
    if (fontValue <= 0.0 || maskValue <= 0.0) {
        color = vec4(0.0);
        return;
    }

    float horizontalVal = texture(Font, uv + vec2(PixelOffset.x, 0.0)).r;
    float verticalVal = texture(Font, uv + vec2(0.0, PixelOffset.y)).r;
    float difference = abs(fontValue - horizontalVal) + abs(fontValue - verticalVal);

    color.rgb = vec3(getIntensity(fontValue, IntensityShift, difference)) * tint.rgb;
    color.a = getIntensity(fontValue, AlphaShift, difference) * tint.a * maskValue;
}
