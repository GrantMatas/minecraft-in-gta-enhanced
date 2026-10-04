#version 330
#extension GL_ARB_separate_shader_objects : require

// The strike's grade: drain the colour (violet energy keeps it, pushed toward the accent), darken (keeping
// bright pixels bright), add bloom, vignette, then the photo-negative (a cut, on or off), then the flash on top.

uniform sampler2D InSampler;
uniform sampler2D BloomSampler;

// Strengths = (drain, dim, invert, bloom); Extra = (flash, bloom threshold, bloom boost, unused)
layout(std140) uniform EclipseConfig {
    vec4 Strengths;
    vec4 Extra;
};

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 fragColor;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);
// StrikeColors.ENERGY (#B44DFF).
const vec3 ENERGY = vec3(180.0, 77.0, 255.0) / 255.0;
// How far kept violet pixels are pushed toward ENERGY at full drain (pale lavender over a bright sky turns purple).
const float VIOLET_PUSH = 0.5;
// At full dim (0.65 from the timeline) mid tones keep 1 - 0.65 * DIM_GAIN = 35 % of their brightness.
const float DIM_GAIN = 1.0;
// Share of the dimming that the darkest pixels get.
const float SHADOW_DIM = 0.45;
// Bloom is added as a "screen" blend (never past white) at this gain. At 1.0 with a plain add, the beam's
// shells and the corona merged into one flat white mass at noon (integration run strike_day 003).
const float BLOOM_GAIN = 0.5;
// Darkest corner at full dim: 1 - 0.65 * VIGNETTE_GAIN.
const float VIGNETTE_GAIN = 0.7;

// Same as in eclipse_bright.fsh: 1 for violet (hue about 250 to 300 degrees), keyed on hue with a low
// saturation floor so pale lavender energy still counts.
float violetMask(vec3 c) {
    float hi = max(c.r, max(c.g, c.b));
    float lo = min(c.r, min(c.g, c.b));
    float chroma = hi - lo;
    if (hi <= 0.0 || chroma <= 0.0 || c.b < hi) {
        return 0.0;
    }
    float hue = 60.0 * (4.0 + (c.r - c.g) / chroma);
    float inHue = smoothstep(240.0, 252.0, hue) * (1.0 - smoothstep(300.0, 312.0, hue));
    // The energy colours sit at hue 265-275. Bluer pixels (240-258) count only when bright (pale lavender over
    // a day sky): dim moonlit terrain has that hue too and would otherwise keep a purple cast at night.
    float core = smoothstep(256.0, 264.0, hue);
    float bright = mix(smoothstep(0.35, 0.55, hi), smoothstep(0.10, 0.22, hi), core);
    return inHue * smoothstep(0.07, 0.20, chroma / hi) * bright;
}

void main() {
    float drain = Strengths.x;
    float dim = Strengths.y;
    float invert = Strengths.z;
    float bloom = Strengths.w;
    float flash = Extra.x;
    float boost = max(Extra.z, 1.0);

    vec4 source = texture(InSampler, texCoord);
    float coverage = clamp(source.a, 0.0, 1.0);
    // The exported world is premultiplied. Colour grading must operate on
    // straight colour and restore coverage, especially for the negative/flash.
    if (coverage <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    vec3 c0 = source.rgb / max(coverage, 1.0 / 255.0);
    float hi0 = max(c0.r, max(c0.g, c0.b));
    float luma = dot(c0, LUMA);
    float violet = violetMask(c0);
    float keepColour = 1.0 - violet;

    vec3 c = mix(c0, ENERGY * hi0, violet * drain * VIOLET_PUSH);
    c = mix(c, vec3(luma), drain * keepColour);

    // Highlight-preserving darkening: mid tones scale down, bright pixels much less. A gentle curve (luma^4)
    // instead of a narrow knee near white, which turned every soft glow into a flat white shape with a hard
    // edge. Deep shadows are dimmed about half as much, so a night scene does not sink to black.
    float l2 = luma * luma;
    float highlight = l2 * l2;
    float shadow = mix(SHADOW_DIM, 1.0, smoothstep(0.03, 0.2, luma));
    c *= 1.0 - DIM_GAIN * dim * shadow * (1.0 - highlight) * keepColour;

    // Exact-black VOID pixels (beam core, eclipse disc, singularity) get no bloom: they stay holes in the world.
    vec3 glow = clamp(texture(BloomSampler, texCoord).rgb * (bloom * BLOOM_GAIN * boost), 0.0, 1.0);
    glow *= smoothstep(0.5 / 255.0, 3.0 / 255.0, hi0);
    c = 1.0 - (1.0 - clamp(c, 0.0, 1.0)) * (1.0 - glow);

    vec2 d = texCoord - 0.5;
    float edge = smoothstep(0.15, 1.0, dot(d, d) * 2.0);
    c *= 1.0 - VIGNETTE_GAIN * dim * edge;

    // Photo-negative: a cut (a half negative would be flat grey). Brightness flips, hue stays, so violet stays
    // violet instead of turning green; a contrast curve makes it read stark black and white.
    if (invert >= 0.5) {
        c = clamp(c + (1.0 - 2.0 * dot(c, LUMA)), 0.0, 1.0);
        c = smoothstep(0.04, 0.96, c);
    }

    // The flash lifts bright pixels toward white; blacks only lift when the flash is near full (flash 1 = white).
    float lumaNow = dot(c, LUMA);
    float reach = mix(smoothstep(0.15, 0.6, lumaNow), 1.0, flash * flash);
    c = clamp(c + flash * reach * (1.0 - c), 0.0, 1.0);
    // Preserve effect coverage when compositing over a transparent host world.
    fragColor = vec4(c * coverage, coverage);
}
