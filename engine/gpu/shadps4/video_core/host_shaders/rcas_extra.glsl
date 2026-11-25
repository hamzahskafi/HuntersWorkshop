// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop: the port's sharpening, one pass for every upscaler (0.4.6 beta 16, from 0.4.4
// beta 20's). Shared by taa_sharpen.comp (a pass of its own) and upscale_merge.comp (in the merge,
// with no copy of the image and no pass of its own), so both compute the same. The part above
// RCAS_SOURCE is also C++: rcas-test (tests/test_rcas.cpp) checks it on the CPU. Before including
// it in a shader: RCAS_SOURCE, the image the colour is read from (imageLoad).
//
// AMD's RCAS (FidelityFX FSR 1, fsr/ffx_fsr1.h FsrRcasF with FSR_RCAS_DENOISE): the same noise
// detection, ring limits and lobe, at AMD's own strength (FSR 2 and 3's mapping of their sharpness
// s: 2 - 2s stops below the maximum), applied in full (beta 20's blended it by the strength again,
// so its 0.2 was about a fifth of FSR's 0.2). It differs from AMD's code where that code meets an
// input it was not written for:
//  - It sharpens a display-like picture, as RCAS is made for (AMD: perceptual, gamma 2.0).
//    `encoding` says what the image holds:
//      0  the HDR scene (linear light, any range): AMD's reversible tone mapper (FsrSrtmF,
//         c / (max(rgb) + 1)), then gamma 2.0; undone after (squared, then FsrSrtmInvF);
//      1  linear light in 0..1 (a frame in an sRGB format, its view decoding it): gamma 2.0;
//      2  display-encoded already (the game's tonemapped frame): as it is.
//    Beta 20's sharpened linear light (about half AMD's sharpening on strong edges) and nothing
//    beside a value above 1 (it divided by the local peak, which AMD's guard then holds at 1).
//  - The HDR scene's result is held at twice the brightest of the five pixels plus one, in linear
//    light (halfway, in the tone mapper's terms, from that brightest to its 1), scaled down whole
//    to keep its colour. AMD's ceiling, 1, is white on a display picture but infinite light
//    here: where one channel stands out of its ring (the guard works per channel, the noise
//    detection on luma), RCAS lifts it toward 1 as it would toward white, and the inverse, which
//    divides by the brightest channel, multiplies the whole pixel (a blue of 0.47 came out at 12;
//    in a search of random neighbourhoods, 359 at 14386). Grey and tinted edges never reach it.
//  - Exact reciprocals: the approximate one in AMD's resolve (APrxMedRcpF1) is 0.32% low even with
//    nothing to sharpen, invisible on a display picture, but through the HDR inverse a pixel at
//    100 came back at 61. And no division by zero (a channel at 0 across the ring, a ring at the
//    ceiling).
//  - Where RCAS changes nothing (its lobe 0), the pixel is returned exactly as read.

float RcasMax3(vec3 c) {
    return max(c.r, max(c.g, c.b));
}

// To the picture RCAS sharpens (`encoding` as above), and back.
vec3 RcasEncode(vec3 c, uint encoding) {
    if (encoding == 0u) {
        c = c / (RcasMax3(c) + 1.0); // FsrSrtmF
    }
    return encoding == 2u ? c : sqrt(c);
}

vec3 RcasDecode(vec3 c, uint encoding) {
    if (encoding != 2u) {
        c = c * c;
    }
    if (encoding == 0u) {
        c = c / max(1.0 / 32768.0, 1.0 - RcasMax3(c)); // FsrSrtmInvF
    }
    return c;
}

// AMD's limiters for one channel: the most negative lobe that keeps the result at or above 0, and
// the one that keeps it at or below 1; the larger of the two.
float RcasChannelLobe(float mn4, float mx4, float e) {
    // A ring at 0: the result cannot go below 0 here (AMD's 0 / 0, no limit on most GPUs).
    const float hit_min = mx4 > 0.0 ? min(mn4, e) / (4.0 * mx4) : 1.0;
    // A ring at 1 or above (a display-encoded frame only): nothing to sharpen (AMD's 0 / 0, or a
    // lobe above 0).
    const float hit_max = mn4 < 1.0 ? (1.0 - max(mx4, e)) / (4.0 * mn4 - 4.0) : 0.0;
    return max(-hit_min, hit_max);
}

// The centre sharpened against its ring (the pixels above, left, right and below), all as read;
// `strength` 0..1 (0 or less: the centre as read).
vec3 RcasSharpen(vec3 above, vec3 left, vec3 centre, vec3 right, vec3 below, float strength,
                 uint encoding) {
    const float s = min(strength, 1.0);
    if (!(s > 0.0)) {
        return centre;
    }
    //    b
    //  d e f
    //    h
    const vec3 b = RcasEncode(above, encoding), d = RcasEncode(left, encoding),
               e = RcasEncode(centre, encoding), f = RcasEncode(right, encoding),
               h = RcasEncode(below, encoding);
    // Luma times 2, and AMD's noise detection: less sharpening where the centre stands out of its
    // ring alone.
    const float bl = b.b * 0.5 + (b.r * 0.5 + b.g), dl = d.b * 0.5 + (d.r * 0.5 + d.g),
                el = e.b * 0.5 + (e.r * 0.5 + e.g), fl = f.b * 0.5 + (f.r * 0.5 + f.g),
                hl = h.b * 0.5 + (h.r * 0.5 + h.g);
    const float range = max(max(max(bl, dl), max(el, fl)), hl) - min(min(min(bl, dl), min(el, fl)), hl);
    const float noise = range > 0.0 ? min(abs(0.25 * (bl + dl + fl + hl) - el) / range, 1.0) : 0.0;
    const vec3 mn4 = min(min(b, d), min(f, h)), mx4 = max(max(b, d), max(f, h));
    const float lobes = max(max(RcasChannelLobe(mn4.r, mx4.r, e.r), RcasChannelLobe(mn4.g, mx4.g, e.g)),
                            RcasChannelLobe(mn4.b, mx4.b, e.b));
    // AMD's FSR_RCAS_LIMIT, its strength (FsrRcasCon: 2^-stops) and its noise removal.
    const float lobe = max(-(0.25 - 1.0 / 16.0), min(lobes, 0.0)) * exp2(2.0 * s - 2.0) *
                       (1.0 - 0.5 * noise);
    if (lobe == 0.0) {
        return centre;
    }
    vec3 result = RcasDecode(max((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0), vec3(0.0)), encoding);
    if (encoding == 0u) {
        const float most = RcasMax3(result);
        const float cap = 2.0 * RcasMax3(max(max(max(above, left), max(right, below)), centre)) + 1.0;
        if (most > cap) {
            result = (cap / most) * result;
        }
    }
    return result;
}

#ifdef RCAS_SOURCE
// Negative input read as 0 (as RCAS reads it), and an infinity as fp16's largest, so that it does
// not spread to its neighbours.
vec3 RcasLoad(ivec2 p) {
    const vec3 c = imageLoad(RCAS_SOURCE, clamp(p, ivec2(0), imageSize(RCAS_SOURCE) - 1)).rgb;
    return clamp(c, vec3(0.0), vec3(65504.0));
}

// The colour at p sharpened by `strength` (0..1; 0: as read), the image holding `encoding`.
vec3 ExtraSharpen(ivec2 p, float strength, uint encoding) {
    const vec3 centre = RcasLoad(p);
    if (!(strength > 0.0)) {
        return centre;
    }
    return RcasSharpen(RcasLoad(p + ivec2(0, -1)), RcasLoad(p + ivec2(-1, 0)), centre,
                       RcasLoad(p + ivec2(1, 0)), RcasLoad(p + ivec2(0, 1)), strength, encoding);
}
#endif
