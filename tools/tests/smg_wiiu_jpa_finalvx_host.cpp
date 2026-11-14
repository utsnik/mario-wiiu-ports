#include "platform/wiiu/jpa_final_vertex.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>

static bool near(float a, float b) { return std::fabs(a - b) < 0.000001f; }

static int test_stride_and_offsets() {
    const uint32_t formats[] = {
        SMS_WIIU_JPA_FINALVX_TEX0,
        SMS_WIIU_JPA_FINALVX_CLR0 | SMS_WIIU_JPA_FINALVX_TEX0,
        SMS_WIIU_JPA_FINALVX_CLR0 | SMS_WIIU_JPA_FINALVX_CLR1 | SMS_WIIU_JPA_FINALVX_TEX0,
    };
    const uint32_t strides[] = {20, 24, 28};
    const float pos[3] = {1.25f, -2.5f, 3.75f};
    const float tex[2] = {-4.5f, 5.25f};
    const uint8_t clr0[4] = {0x11, 0x22, 0x33, 0x44};
    const uint8_t clr1[4] = {0xaa, 0xbb, 0xcc, 0xdd};
    for (unsigned i = 0; i < 3; ++i) {
        if (sms_wiiu_jpa_finalvx_stride(formats[i]) != strides[i]) return 1;
        uint8_t packed[28];
        memset(packed, 0xa5, sizeof(packed));
        sms_wiiu_jpa_finalvx_pack(packed, formats[i], pos, clr0, clr1, tex);
        uint8_t expected[28];
        memset(expected, 0xa5, sizeof(expected));
        memcpy(expected, pos, sizeof(pos));
        unsigned offset = 12;
        if (formats[i] & SMS_WIIU_JPA_FINALVX_CLR0) {
            memcpy(expected + offset, clr0, 4);
            offset += 4;
        }
        if (formats[i] & SMS_WIIU_JPA_FINALVX_CLR1) {
            memcpy(expected + offset, clr1, 4);
            offset += 4;
        }
        memcpy(expected + offset, tex, sizeof(tex));
        if (memcmp(packed, expected, strides[i]) != 0) return 1;
        float unpacked[2];
        memcpy(unpacked, packed + offset, sizeof(unpacked));
        if (!near(unpacked[0], tex[0]) || !near(unpacked[1], tex[1])) return 1;
    }
    if (sms_wiiu_jpa_finalvx_stride(SMS_WIIU_JPA_FINALVX_CLR1 |
                                    SMS_WIIU_JPA_FINALVX_TEX0) != 0 ||
        sms_wiiu_jpa_finalvx_stride(SMS_WIIU_JPA_FINALVX_TEX0 | (1u << 7)) != 0)
        return 1;
    return 0;
}

static int test_signed_fractional_texcoords() {
    const uint8_t entries[] = {0x00, 0x00, 0xff, 0x02, 0x7f, 0x80};
    float out[2];
    if (!sms_wiiu_jpa_finalvx_resolve_s8(entries, 2, 1, 0, out) ||
        !near(out[0], -1.0f) || !near(out[1], 2.0f)) return 1;
    if (!sms_wiiu_jpa_finalvx_resolve_s8(entries, 2, 1, 1, out) ||
        !near(out[0], -0.5f) || !near(out[1], 1.0f)) return 1;
    const uint8_t strided[] = {0x11, 0x12, 0x13, 0x14, 0xf8, 0x04, 0xee, 0xef};
    if (!sms_wiiu_jpa_finalvx_resolve_s8(strided, 4, 1, 0, out) ||
        !near(out[0], -8.0f) || !near(out[1], 4.0f)) return 1;
    return 0;
}

static int test_triangle_quad_indices() {
    uint32_t indices[16] = {};
    if (sms_wiiu_jpa_finalvx_quad_indices(false, 2, indices) != 12) return 1;
    const uint32_t triangles[] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    if (memcmp(indices, triangles, sizeof(triangles)) != 0) return 1;
    if (sms_wiiu_jpa_finalvx_quad_indices(true, 2, indices) != 8) return 1;
    const uint32_t quads[] = {0, 1, 2, 3, 4, 5, 6, 7};
    return memcmp(indices, quads, sizeof(quads)) == 0 ? 0 : 1;
}

int main() {
    if (test_stride_and_offsets() || test_signed_fractional_texcoords() || test_triangle_quad_indices())
        return 1;
    std::puts("jpa-finalvx: PASS (stride, signed S8 UV, quad indices)");
    return 0;
}
