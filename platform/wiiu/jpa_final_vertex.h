#ifndef SMS_WIIU_JPA_FINAL_VERTEX_H
#define SMS_WIIU_JPA_FINAL_VERTEX_H

#include <stdint.h>
#include <string.h>

enum SmsWiiuJpaFinalVxFormatBits {
    SMS_WIIU_JPA_FINALVX_CLR0 = 1u << 2,
    SMS_WIIU_JPA_FINALVX_CLR1 = 1u << 3,
    SMS_WIIU_JPA_FINALVX_TEX0 = 1u << 4,
};

enum SmsWiiuJpaFinalVxPrimitiveClass {
    SMS_WIIU_JPA_FINALVX_PRIM_TRIS = 0,
    SMS_WIIU_JPA_FINALVX_PRIM_QUADS = 1,
};

struct SmsWiiuJpaFinalVxDesc {
    const uint8_t* vertices;
    const uint32_t* indices;
    uint32_t vertexCount, indexCount, particleCount;
    uint32_t vertexStride, packedFmt, primitiveClass, runId;
    uint32_t vertexBytes, indexBytes;
    uint32_t slot;
    uint8_t compactDefaults[32];
};

enum SmsWiiuJpaFinalVxRecordPhase {
    SMS_WIIU_JPA_FINALVX_BEGIN_CHECK = 1,
    SMS_WIIU_JPA_FINALVX_SUBMIT = 2,
};

inline bool sms_wiiu_jpa_finalvx_format_valid(uint32_t packedFmt) {
    const uint32_t supported = SMS_WIIU_JPA_FINALVX_CLR0 |
                               SMS_WIIU_JPA_FINALVX_CLR1 |
                               SMS_WIIU_JPA_FINALVX_TEX0;
    return (packedFmt & ~supported) == 0 &&
           (packedFmt & SMS_WIIU_JPA_FINALVX_TEX0) != 0 &&
           ((packedFmt & SMS_WIIU_JPA_FINALVX_CLR1) == 0 ||
            (packedFmt & SMS_WIIU_JPA_FINALVX_CLR0) != 0);
}

inline uint32_t sms_wiiu_jpa_finalvx_stride(uint32_t packedFmt) {
    if (!sms_wiiu_jpa_finalvx_format_valid(packedFmt)) return 0;
    return 12u + ((packedFmt & SMS_WIIU_JPA_FINALVX_CLR0) ? 4u : 0u) +
           ((packedFmt & SMS_WIIU_JPA_FINALVX_CLR1) ? 4u : 0u) + 8u;
}

inline bool sms_wiiu_jpa_finalvx_resolve_s8(const uint8_t* array, uint32_t stride,
                                            uint8_t index, uint8_t frac, float out[2]) {
    if (!array || !out || stride < 2 || frac >= 31) return false;
    const int8_t* entry = reinterpret_cast<const int8_t*>(array + uint32_t(index) * stride);
    const float scale = 1.0f / static_cast<float>(uint32_t(1) << frac);
    out[0] = static_cast<float>(entry[0]) * scale;
    out[1] = static_cast<float>(entry[1]) * scale;
    return true;
}

inline void sms_wiiu_jpa_finalvx_pack(uint8_t* dst, uint32_t packedFmt,
                                      const float pos[3], const uint8_t* clr0,
                                      const uint8_t* clr1, const float tex[2]) {
    if (!dst || !pos || !tex || !sms_wiiu_jpa_finalvx_format_valid(packedFmt)) return;
    uint32_t offset = 0;
    memcpy(dst + offset, pos, sizeof(float) * 3);
    offset += sizeof(float) * 3;
    if (packedFmt & SMS_WIIU_JPA_FINALVX_CLR0) {
        if (clr0) memcpy(dst + offset, clr0, 4);
        offset += 4;
    }
    if (packedFmt & SMS_WIIU_JPA_FINALVX_CLR1) {
        if (clr1) memcpy(dst + offset, clr1, 4);
        offset += 4;
    }
    memcpy(dst + offset, tex, sizeof(float) * 2);
}

inline uint32_t sms_wiiu_jpa_finalvx_quad_indices(bool nativeQuads,
                                                   uint32_t quadCount, uint32_t* out) {
    if (!out) return 0;
    uint32_t written = 0;
    for (uint32_t quad = 0; quad < quadCount; ++quad) {
        const uint32_t base = quad * 4;
        if (nativeQuads) {
            out[written++] = base;
            out[written++] = base + 1;
            out[written++] = base + 2;
            out[written++] = base + 3;
        } else {
            out[written++] = base;
            out[written++] = base + 1;
            out[written++] = base + 2;
            out[written++] = base;
            out[written++] = base + 2;
            out[written++] = base + 3;
        }
    }
    return written;
}

#endif
