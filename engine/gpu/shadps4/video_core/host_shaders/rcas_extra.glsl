// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.4 beta 20: the port's extra sharpening (RCAS, strength 0..2), shared by
// taa_sharpen.comp (its own pass) and upscale_merge.comp (in the merge, with no copy of the image
// and no pass of its own: the engine map's upscale-1), so both compute exactly the same. Before
// including: RCAS_SOURCE, the image the colour is read from (imageLoad).
#define A_GPU 1
#define A_GLSL 1
#include "fsr/ffx_a.h"
#define FSR_RCAS_F 1
#define FSR_RCAS_DENOISE 1
float color_scale;
vec3 loadColor(ivec2 p) {
    return max(imageLoad(RCAS_SOURCE, clamp(p, ivec2(0), imageSize(RCAS_SOURCE)-1)).rgb, vec3(0));
}
AF4 FsrRcasLoadF(ASU2 p) { return vec4(loadColor(p) / color_scale, 1); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#include "fsr/ffx_fsr1.h"

// The colour at p sharpened by `strength` (0..2: above 1 the RCAS change is extrapolated,
// stronger than AMD's maximum); negative input clamped to 0, as RCAS reads it.
vec3 ExtraSharpen(ivec2 p, float strength) {
    float amount = clamp(strength, 0.0, 2.0);
    vec3 center = loadColor(p);
    if (amount == 0.0) {
        return center;
    }
    // RCAS limits assume normalized color. A shared local scale also handles HDR input.
    vec3 left = loadColor(p+ivec2(-1,0)), right = loadColor(p+ivec2(1,0));
    vec3 top = loadColor(p+ivec2(0,-1)), bottom = loadColor(p+ivec2(0,1));
    vec3 peak = max(center, max(max(left, right), max(top, bottom)));
    color_scale = max(1.0, max(peak.r, max(peak.g, peak.b)));
    vec3 low = min(center, min(min(left, right), min(top, bottom)));
    // RCAS's approximate reciprocal can shift constant color, especially in HDR.
    if (all(lessThanEqual(peak-low, vec3(1e-6*color_scale)))) {
        return center;
    }
    uvec4 constants = uvec4(floatBitsToUint(exp2(-2.0*(1.0-min(amount, 1.0)))), 0, 0, 0);
    vec3 sharpened;
    FsrRcasF(sharpened.r, sharpened.g, sharpened.b, uvec2(p), constants);
    return clamp(mix(center, sharpened*color_scale, amount), vec3(0), vec3(color_scale));
}
