// Fragment shader for 2D rendering — deko3d / uam
// Compiled by: uam -s frag -o basic_fsh.dksh basic_fsh.glsl
#version 460

layout (location = 0) in vec2 fragUV;
layout (location = 1) in vec4 fragColor;
layout (location = 2) in vec2 fragShapePos;
layout (location = 3) in vec2 fragShapeHalf;
layout (location = 4) in vec2 fragShapeRound;

layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 1) uniform FsUniforms {
    int useTexture;   // 0 = color only, 1 = texture × color
};

layout (location = 0) out vec4 outColor;

float sdRoundedBox(vec2 p, vec2 halfExtent, float radius) {
    float r = min(radius, min(halfExtent.x, halfExtent.y));
    vec2 q = abs(p) - (halfExtent - vec2(r));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// 4x4 ordered dither plus white noise. Large soft gradients (the background
// wash, scrims, the corner vignette) band visibly in 8-bit output; one LSB of
// screen-space noise breaks the stair-steps without reading as grain.
float bayer4(vec2 p) {
    int x = int(mod(p.x, 4.0));
    int y = int(mod(p.y, 4.0));
    const float m[16] = float[16](
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    return m[y * 4 + x] / 16.0;
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec4 c = (useTexture != 0) ? texture(tex, fragUV) * fragColor : fragColor;
    float radius = fragShapeRound.x;
    if (radius > 0.0) {
        float distanceToEdge = sdRoundedBox(fragShapePos, fragShapeHalf, radius);
        // Floor the AA band at most of a pixel: a derivative that collapses on
        // an axis-aligned edge would otherwise cut a hard, crawling line.
        float ramp = max(length(vec2(dFdx(distanceToEdge), dFdy(distanceToEdge))), 0.75);
        float thickness = fragShapeRound.y;
        if (thickness > 0.0) {
            float halfStroke = max(thickness, 1.0) * 0.5;
            c.a *= 1.0 - smoothstep(-ramp, ramp,
                                    abs(distanceToEdge + halfStroke) - halfStroke);
        } else {
            c.a *= 1.0 - smoothstep(-ramp, ramp, distanceToEdge);
        }
    }
    if (c.a <= 0.0) discard;

    float d = (bayer4(gl_FragCoord.xy) - 0.5
             + hash12(gl_FragCoord.xy) - 0.5) * (0.6 / 255.0);
    c.a = clamp(c.a + d, 0.0, 1.0);
    c.rgb = clamp(c.rgb + d * c.a, 0.0, 1.0);
    outColor = c;
}
