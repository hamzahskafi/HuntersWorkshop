// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.4 beta 19: a colour image's texels written as depth into a depth image of
// its size (Runtime::CopyColorToDepth), by a full-screen draw into the depth image as an
// attachment, instead of a transfer copy into it (a whole-image decompression where the driver
// keeps depth compressed) or a round trip through a buffer. FLOAT: R32F into 32-bit depth, R16
// unorm into 16-bit depth (exact). BITS32: R32_UINT holding a float's bits. U16: R16_UINT holding
// a 16-bit depth's bits.
#version 450
#extension GL_EXT_samplerless_texture_functions : require

#if defined(BITS32) || defined(U16)
layout(binding = 0) uniform utexture2D color;
#else
layout(binding = 0) uniform texture2D color;
#endif

void main() {
    const ivec2 coord = ivec2(gl_FragCoord.xy);
#if defined(BITS32)
    gl_FragDepth = uintBitsToFloat(texelFetch(color, coord, 0).r);
#elif defined(U16)
    gl_FragDepth = float(texelFetch(color, coord, 0).r) / 65535.0;
#else
    gl_FragDepth = texelFetch(color, coord, 0).r;
#endif
}
