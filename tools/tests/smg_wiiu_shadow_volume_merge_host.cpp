#include "platform/wiiu/shadow_volume_merge.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

struct Reader {
    const SmsWiiuShadowVolumeVec3* positions;
    uint32_t count;

    bool read(uint32_t index, SmsWiiuShadowVolumeVec3* out) const {
        if (!out || index >= count) return false;
        *out = positions[index];
        return true;
    }
};

static bool near(float a, float b) {
    return std::fabs(a - b) < 0.000001f;
}

int main() {
    const SmsWiiuShadowVolumeVec3 positions[] = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
    };
    const uint8_t displayList[] = {
        0x98, 0x00, 0x04,
        0x00, 0x00, 0x00,
        0x01, 0x03, 0x00,
        0x02, 0x03, 0x00,
        0x03, 0x00, 0x00,
        0x00,
    };
    Reader reader = {positions, 4};
    SmsWiiuShadowVolumeTriangle triangles[2] = {};
    uint32_t written = 0;
    if (!sms_wiiu_shadow_volume_extract_triangles(displayList, sizeof(displayList), 3,
                                                   0, 1, 1, 1, reader, triangles, 2, &written) ||
        written != 2 || triangles[0].matrix[1] != 3 || triangles[1].matrix[0] != 3 ||
        !near(triangles[1].pos[0].x, 1.0f) || !near(triangles[1].pos[1].y, 0.0f)) {
        std::puts("shadow-volume-merge: FAIL (strip extraction)");
        return 1;
    }

    const float matrix[3][4] = {
        {2.0f, 0.0f, 0.0f, 4.0f},
        {0.0f, 3.0f, 0.0f, -2.0f},
        {0.0f, 0.0f, 1.0f, 7.0f},
    };
    const SmsWiiuShadowVolumeVec3 transformed =
        sms_wiiu_shadow_volume_transform_point(matrix, triangles[0].pos[2]);
    if (!near(transformed.x, 6.0f) || !near(transformed.y, 1.0f) || !near(transformed.z, 7.0f)) {
        std::puts("shadow-volume-merge: FAIL (matrix transform)");
        return 1;
    }
    std::puts("shadow-volume-merge: PASS (strip extraction, matrix transform)");
    return 0;
}
