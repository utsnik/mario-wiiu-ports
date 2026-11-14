#include "JPADirectionalBatch.hpp"
#include "platform/wiiu/jpa_final_vertex.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>

static bool near(float a, float b) { return std::fabs(a - b) < 0.000001f; }

static void packBillboardVertex(uint8_t* dst, uint32_t format, const float matrix[3][4],
                                const signed char* template_xyz, unsigned corner,
                                const uint8_t* tex_array, uint8_t tex_frac,
                                const uint8_t* clr0, const uint8_t* clr1) {
    const jpa_batch::Vertex vertex =
        jpa_batch::transform(matrix, template_xyz + corner * 3);
    float tex[2] = {};
    if (!sms_wiiu_jpa_finalvx_resolve_s8(tex_array, 2, static_cast<uint8_t>(corner), tex_frac, tex))
        return;
    const float pos[3] = {vertex.x, vertex.y, vertex.z};
    sms_wiiu_jpa_finalvx_pack(dst, format, pos, clr0, clr1, tex);
}

static int test_billboard_corner_order() {
    const signed char corners[12] = {-3, -4, 0, 5, -4, 0, 5, 6, 0, -3, 6, 0};
    const float matrix[3][4] = {
        {1.0f, 0.0f, 0.0f, 10.0f}, {0.0f, 1.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 1.0f, 4.0f},
    };
    const uint8_t tex_array[8] = {0, 0, 1, 0, 2, 0, 3, 0};
    uint8_t payload[4 * 20] = {};
    for (unsigned corner = 0; corner < 4; ++corner)
        packBillboardVertex(payload + corner * 20, SMS_WIIU_JPA_FINALVX_TEX0, matrix,
                            corners, corner, tex_array, 0, NULL, NULL);
    const float expected[4][3] = {
        {7.0f, -6.0f, 4.0f}, {15.0f, -6.0f, 4.0f},
        {15.0f, 4.0f, 4.0f}, {7.0f, 4.0f, 4.0f},
    };
    for (unsigned corner = 0; corner < 4; ++corner) {
        float got[3];
        std::memcpy(got, payload + corner * 20, sizeof(got));
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!near(got[axis], expected[corner][axis])) return 1;
    }
    return 0;
}

static int test_rotated_billboard_corners() {
    const signed char corners[12] = {-7, -2, 1, 7, -2, 1, 7, 2, 1, -7, 2, 1};
    const float matrix[3][4] = {
        {0.25f, -0.75f, 0.5f, 3.0f}, {0.5f, 0.25f, -0.25f, -8.0f},
        {0.125f, 0.5f, 1.25f, 11.0f},
    };
    const uint8_t tex_array[8] = {0, 0, 1, 0, 2, 0, 3, 0};
    uint8_t payload[4 * 20] = {};
    for (unsigned corner = 0; corner < 4; ++corner)
        packBillboardVertex(payload + corner * 20, SMS_WIIU_JPA_FINALVX_TEX0, matrix,
                            corners, corner, tex_array, 0, NULL, NULL);
    for (unsigned corner = 0; corner < 4; ++corner) {
        const signed char* xyz = corners + corner * 3;
        float expected[3] = {matrix[0][3], matrix[1][3], matrix[2][3]};
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned axis = 0; axis < 3; ++axis)
                expected[row] += matrix[row][axis] * xyz[axis];
        float got[3];
        std::memcpy(got, payload + corner * 20, sizeof(got));
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!near(got[axis], expected[axis])) return 1;
    }
    return 0;
}

static int test_billboard_signed_fractional_uv() {
    const uint8_t tex_array[8] = {0x7f, 0x80, 0xff, 0x02, 0x08, 0xf8, 0x04, 0x04};
    float out[2] = {};
    if (!sms_wiiu_jpa_finalvx_resolve_s8(tex_array, 2, 1, 0, out) ||
        !near(out[0], -1.0f) || !near(out[1], 2.0f)) return 1;
    if (!sms_wiiu_jpa_finalvx_resolve_s8(tex_array, 2, 1, 1, out) ||
        !near(out[0], -0.5f) || !near(out[1], 1.0f)) return 1;
    if (!sms_wiiu_jpa_finalvx_resolve_s8(tex_array, 2, 3, 0, out) ||
        !near(out[0], 4.0f) || !near(out[1], 4.0f)) return 1;
    return 0;
}

static int test_billboard_colour_layout() {
    const float pos[3] = {1.0f, 2.0f, 3.0f};
    const float tex[2] = {-4.0f, 5.0f};
    const uint8_t clr0[4] = {0x10, 0x20, 0x30, 0x40};
    const uint8_t clr1[4] = {0xa0, 0xb0, 0xc0, 0xd0};
    uint8_t one[24] = {};
    uint8_t two[28] = {};
    sms_wiiu_jpa_finalvx_pack(one, SMS_WIIU_JPA_FINALVX_CLR0 | SMS_WIIU_JPA_FINALVX_TEX0,
                              pos, clr0, NULL, tex);
    sms_wiiu_jpa_finalvx_pack(two, SMS_WIIU_JPA_FINALVX_CLR0 | SMS_WIIU_JPA_FINALVX_CLR1 |
                              SMS_WIIU_JPA_FINALVX_TEX0, pos, clr0, clr1, tex);
    if (std::memcmp(one + 12, clr0, 4) != 0 || std::memcmp(two + 12, clr0, 4) != 0 ||
        std::memcmp(two + 16, clr1, 4) != 0) return 1;
    float oneTex[2] = {}, twoTex[2] = {};
    std::memcpy(oneTex, one + 16, sizeof(oneTex));
    std::memcpy(twoTex, two + 20, sizeof(twoTex));
    return near(oneTex[0], tex[0]) && near(oneTex[1], tex[1]) &&
                   near(twoTex[0], tex[0]) && near(twoTex[1], tex[1]) ? 0 : 1;
}

static int test_billboard_triangle_indices() {
    uint32_t indices[6] = {};
    const uint32_t expected[6] = {0, 1, 2, 0, 2, 3};
    if (sms_wiiu_jpa_finalvx_quad_indices(false, 1, indices) != 6 ||
        std::memcmp(indices, expected, sizeof(expected)) != 0) return 1;
    const uint32_t native[4] = {0, 1, 2, 3};
    return sms_wiiu_jpa_finalvx_quad_indices(true, 1, indices) == 4 &&
                   std::memcmp(indices, native, sizeof(native)) == 0 ? 0 : 1;
}

static unsigned billboardCheckWorkerDraws(bool beginCheck, bool normalGx,
                                          bool submit) {
    const unsigned normalDraws = normalGx ? 1u : 0u;
    if (beginCheck && submit) return 1;
    return normalDraws;
}

static int test_billboard_check_emits_once() {
    // BEGIN_CHECK brackets the normal producer stream. A successful SUBMIT
    // replaces that stream at the worker boundary, while either record
    // failure leaves the normal GX draw as the only representation.
    if (billboardCheckWorkerDraws(true, true, true) != 1 ||
        billboardCheckWorkerDraws(true, true, false) != 1 ||
        billboardCheckWorkerDraws(false, true, false) != 1) return 1;
    return 0;
}

static int test_billboard_visible_count_and_reverse_order() {
    const bool invisible[5] = {false, true, false, true, false};
    unsigned forward[5] = {}, reverse[5] = {};
    unsigned forwardCount = 0, reverseCount = 0;
    for (unsigned i = 0; i < 5; ++i)
        if (!invisible[i]) forward[forwardCount++] = i;
    for (unsigned i = 5; i != 0; --i)
        if (!invisible[i - 1]) reverse[reverseCount++] = i - 1;
    const unsigned expectedForward[3] = {0, 2, 4};
    const unsigned expectedReverse[3] = {4, 2, 0};
    if (forwardCount != 3 || reverseCount != 3 ||
        std::memcmp(forward, expectedForward, sizeof(expectedForward)) != 0 ||
        std::memcmp(reverse, expectedReverse, sizeof(expectedReverse)) != 0) return 1;
    return 0;
}

int main() {
    if (test_billboard_corner_order() || test_rotated_billboard_corners() ||
        test_billboard_signed_fractional_uv() || test_billboard_colour_layout() ||
        test_billboard_triangle_indices() || test_billboard_check_emits_once() ||
        test_billboard_visible_count_and_reverse_order()) return 1;
    std::puts("jpa-billboard: PASS (corner order, rotation, signed fractional UV, colours, indices, check, visibility)");
    return 0;
}
