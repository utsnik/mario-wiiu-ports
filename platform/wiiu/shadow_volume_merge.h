#ifndef SMS_WIIU_SHADOW_VOLUME_MERGE_H
#define SMS_WIIU_SHADOW_VOLUME_MERGE_H

#include <stdint.h>

struct SmsWiiuShadowVolumeVec3 {
    float x, y, z;
};

struct SmsWiiuShadowVolumeTriangle {
    SmsWiiuShadowVolumeVec3 pos[3];
    uint8_t matrix[3];
};

static inline uint16_t sms_wiiu_shadow_volume_read_be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static inline uint32_t sms_wiiu_shadow_volume_attr_bytes(uint32_t type) {
    // GX_NONE, GX_DIRECT, GX_INDEX8, GX_INDEX16.
    return type == 2 ? 1u : type == 3 ? 2u : 0u;
}

static inline bool sms_wiiu_shadow_volume_read_index(const uint8_t* p, uint32_t bytes,
                                                      uint32_t* out) {
    if (!p || !out) return false;
    if (bytes == 1) {
        *out = p[0];
        return true;
    }
    if (bytes == 2) {
        *out = sms_wiiu_shadow_volume_read_be16(p);
        return true;
    }
    return false;
}

// PositionReader must provide bool read(uint32_t index, Vec3* out) const.
// The helper expands GX triangle, strip, and fan commands into cull-preserving
// triangles. It intentionally rejects all other primitives and malformed data.
template <typename PositionReader>
static inline bool sms_wiiu_shadow_volume_extract_triangles(
    const uint8_t* displayList, uint32_t displayListBytes, uint32_t vertexStride,
    uint32_t positionOffset, uint32_t positionBytes, uint32_t matrixOffset,
    uint32_t matrixBytes, PositionReader reader, SmsWiiuShadowVolumeTriangle* out,
    uint32_t capacity, uint32_t* written) {
    if (!displayList || !vertexStride || !written || positionBytes == 0) return false;
    *written = 0;
    const uint32_t noMatrix = UINT32_MAX;
    uint32_t offset = 0;
    while (offset < displayListBytes) {
        const uint8_t command = displayList[offset++];
        if (command == 0) break;  // GX_NOP padding terminates J3D display lists.
        if (offset + 2 > displayListBytes) return false;
        const uint32_t vertexCount = sms_wiiu_shadow_volume_read_be16(displayList + offset);
        offset += 2;
        if (vertexCount == 0) return false;
        if (vertexCount > (displayListBytes - offset) / vertexStride) return false;

        const uint32_t triangleCount = command == 0x90 ? vertexCount / 3 :
            command == 0x98 || command == 0xa0 ? (vertexCount >= 3 ? vertexCount - 2 : 0) :
            UINT32_MAX;
        if (triangleCount == UINT32_MAX || (command == 0x90 && vertexCount % 3 != 0)) return false;
        if (out && (*written > capacity || triangleCount > (capacity - *written))) return false;

        for (uint32_t triangle = 0; triangle < triangleCount; ++triangle) {
            uint32_t vertex[3];
            if (command == 0x90) {
                vertex[0] = triangle * 3;
                vertex[1] = vertex[0] + 1;
                vertex[2] = vertex[0] + 2;
            } else if (command == 0xa0) {
                vertex[0] = 0;
                vertex[1] = triangle + 1;
                vertex[2] = triangle + 2;
            } else if ((triangle & 1) == 0) {
                vertex[0] = triangle;
                vertex[1] = triangle + 1;
                vertex[2] = triangle + 2;
            } else {
                vertex[0] = triangle + 1;
                vertex[1] = triangle;
                vertex[2] = triangle + 2;
            }

            SmsWiiuShadowVolumeTriangle triangleOut = {};
            for (uint32_t corner = 0; corner < 3; ++corner) {
                const uint8_t* vertexData = displayList + offset + vertex[corner] * vertexStride;
                uint32_t positionIndex;
                if (!sms_wiiu_shadow_volume_read_index(vertexData + positionOffset,
                                                        positionBytes, &positionIndex) ||
                    !reader.read(positionIndex, &triangleOut.pos[corner])) return false;
                triangleOut.matrix[corner] = 0;
                if (matrixOffset != noMatrix) {
                    uint32_t matrixIndex;
                    if (!sms_wiiu_shadow_volume_read_index(vertexData + matrixOffset,
                                                            matrixBytes, &matrixIndex) ||
                        matrixIndex > 255) return false;
                    triangleOut.matrix[corner] = static_cast<uint8_t>(matrixIndex);
                }
            }
            if (out) out[*written] = triangleOut;
            ++*written;
        }
        offset += vertexCount * vertexStride;
    }
    return offset <= displayListBytes;
}

static inline SmsWiiuShadowVolumeVec3 sms_wiiu_shadow_volume_transform_point(
    const float matrix[3][4], const SmsWiiuShadowVolumeVec3& point) {
    SmsWiiuShadowVolumeVec3 result;
    result.x = matrix[0][0] * point.x + matrix[0][1] * point.y + matrix[0][2] * point.z + matrix[0][3];
    result.y = matrix[1][0] * point.x + matrix[1][1] * point.y + matrix[1][2] * point.z + matrix[1][3];
    result.z = matrix[2][0] * point.x + matrix[2][1] * point.y + matrix[2][2] * point.z + matrix[2][3];
    return result;
}

#endif
