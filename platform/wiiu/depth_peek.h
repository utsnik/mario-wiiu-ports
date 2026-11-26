#pragma once

#include <string.h>
#include <stdint.h>

namespace gx {

static const uint32_t kPeekZMax = 0xffffffu;
static const uint32_t kPeekZNoSampleAge = 0xffffffffu;

// GX2's FLOAT_R32 depth snapshot is normalized [0, 1].  GXPeekZ exposes the
// same value as a 24-bit integer; invalid and far-plane samples are sky.
inline uint32_t depthToPeekZ(float depth) {
    if (!(depth == depth))
        return kPeekZMax;
    if (depth <= 0.0f)
        return 0;
    if (depth >= 1.0f)
        return kPeekZMax;
    return static_cast<uint32_t>(depth * float(kPeekZMax));
}

inline uint32_t byteSwap32(uint32_t value) {
    return ((value & 0x000000ffu) << 24) |
           ((value & 0x0000ff00u) << 8) |
           ((value & 0x00ff0000u) >> 8) |
           ((value & 0xff000000u) >> 24);
}

struct DepthPeekSample {
    bool valid;
    uint32_t frame;
    uint32_t rawBits;
};

struct DepthPeekAnswer {
    uint32_t z;
    uint32_t age;
    uint32_t rawBits;
};

// Resolve only the sample already published by the asynchronous readback.
// This deliberately has no readiness or fence operation: no sample is the far
// plane rather than a near-plane zero, and an older sample remains usable.
inline DepthPeekAnswer resolveDepthPeekNonBlocking(uint32_t frame,
                                                   const DepthPeekSample& sample) {
    if (!sample.valid)
        return {kPeekZMax, kPeekZNoSampleAge, 0};

    const uint32_t depthBits = byteSwap32(sample.rawBits);
    float depth;
    memcpy(&depth, &depthBits, sizeof depth);
    const uint32_t age = frame >= sample.frame ? frame - sample.frame : kPeekZNoSampleAge;
    return {depthToPeekZ(depth), age, sample.rawBits};
}

} // namespace gx
