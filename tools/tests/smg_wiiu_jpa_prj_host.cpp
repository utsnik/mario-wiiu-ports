#include "platform/wiiu/jpa_final_vertex.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>

static bool sameBits(float a, float b) {
    uint32_t aa = 0, bb = 0;
    memcpy(&aa, &a, sizeof aa);
    memcpy(&bb, &b, sizeof bb);
    return aa == bb;
}

static int test_projected_stride_and_pack() {
    const uint32_t formats[] = {
        SMS_WIIU_JPA_FINALVX_TEX0_STQ,
        SMS_WIIU_JPA_FINALVX_CLR0 | SMS_WIIU_JPA_FINALVX_TEX0_STQ,
        SMS_WIIU_JPA_FINALVX_CLR0 | SMS_WIIU_JPA_FINALVX_CLR1 | SMS_WIIU_JPA_FINALVX_TEX0_STQ,
    };
    const uint32_t strides[] = {24, 28, 32};
    const float pos[3] = {1.25f, -2.5f, 3.75f};
    const float stq[3] = {-4.5f, 5.25f, -2.0f};
    const uint8_t clr0[4] = {0x11, 0x22, 0x33, 0x44};
    const uint8_t clr1[4] = {0xaa, 0xbb, 0xcc, 0xdd};
    for (unsigned i = 0; i < 3; ++i) {
        if (sms_wiiu_jpa_finalvx_stride(formats[i]) != strides[i] ||
            sms_wiiu_jpa_finalvx_tex_bytes(formats[i]) != 12)
            return 1;
        uint8_t packed[32];
        memset(packed, 0xa5, sizeof packed);
        sms_wiiu_jpa_finalvx_pack_stq(packed, formats[i], pos, clr0, clr1, stq);
        uint8_t expected[32];
        memset(expected, 0xa5, sizeof expected);
        memcpy(expected, pos, sizeof pos);
        unsigned offset = 12;
        if (formats[i] & SMS_WIIU_JPA_FINALVX_CLR0) {
            memcpy(expected + offset, clr0, 4);
            offset += 4;
        }
        if (formats[i] & SMS_WIIU_JPA_FINALVX_CLR1) {
            memcpy(expected + offset, clr1, 4);
            offset += 4;
        }
        memcpy(expected + offset, stq, sizeof stq);
        if (memcmp(packed, expected, strides[i]) != 0) return 1;
        float unpacked[3] = {};
        memcpy(unpacked, packed + offset, sizeof unpacked);
        for (unsigned c = 0; c < 3; ++c)
            if (!sameBits(unpacked[c], stq[c])) return 1;
    }
    if (sms_wiiu_jpa_finalvx_format_valid(SMS_WIIU_JPA_FINALVX_TEX0 |
                                          SMS_WIIU_JPA_FINALVX_TEX0_STQ) ||
        sms_wiiu_jpa_finalvx_stride(SMS_WIIU_JPA_FINALVX_TEX0 | (1u << 7)) != 0)
        return 1;
    return 0;
}

static int test_projective_helper() {
    const float zeroQ[3] = {3.0f, -5.0f, 0.0f};
    const float nonzeroQ[3] = {3.0f, -5.0f, 2.0f};
    float uv[2] = {};
    if (!sms_wiiu_jpa_finalvx_projective_uv(zeroQ, uv) ||
        !sameBits(uv[0], zeroQ[0]) || !sameBits(uv[1], zeroQ[1])) return 1;
    if (!sms_wiiu_jpa_finalvx_projective_uv(nonzeroQ, uv) ||
        !sameBits(uv[0], 1.5f) || !sameBits(uv[1], -2.5f)) return 1;
    return 0;
}

static int test_indices() {
    uint32_t indices[16] = {};
    if (sms_wiiu_jpa_finalvx_quad_indices(false, 2, indices) != 12) return 1;
    const uint32_t tris[] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    if (memcmp(indices, tris, sizeof tris) != 0) return 1;
    if (sms_wiiu_jpa_finalvx_quad_indices(true, 2, indices) != 8) return 1;
    const uint32_t quads[] = {0, 1, 2, 3, 4, 5, 6, 7};
    return memcmp(indices, quads, sizeof quads) == 0 ? 0 : 1;
}

int main() {
    if (test_projected_stride_and_pack() || test_projective_helper() || test_indices()) return 1;
    std::puts("jpa-prj: PASS (STQ stride, exact pack, zero-Q divide, indices)");
    return 0;
}
