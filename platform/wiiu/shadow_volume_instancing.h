#ifndef SMS_WIIU_SHADOW_VOLUME_INSTANCING_H
#define SMS_WIIU_SHADOW_VOLUME_INSTANCING_H

#include <stdint.h>

/* The producer stream carries only this descriptor's address.  The caller
 * owns no part of the descriptor after the port entry point accepts it; the
 * worker frees it after rendering the group. */
struct SmsWiiuShadowVolumeInstancedDesc {
    const void* modelData;
    uint32_t instanceCount;
    uint32_t matrixBytes;
    uint8_t payload[1];
};

typedef void (*SmsWiiuShadowVolumeModelCommandFn)(const void* vcd, uint32_t vcdBytes,
                                                   const void* displayList,
                                                   uint32_t displayListBytes, void* context);

#ifdef __cplusplus
extern "C" {
#endif
bool sms_wiiu_shadow_volume_model_walk(const void* modelData,
                                       SmsWiiuShadowVolumeModelCommandFn callback,
                                       void* context);
const void* sms_wiiu_shadow_volume_model_pos_array(const void* modelData);
#ifdef __cplusplus
}
#endif

static inline float* sms_wiiu_shadow_volume_instanced_matrices(
    SmsWiiuShadowVolumeInstancedDesc* desc) {
    return desc ? reinterpret_cast<float*>(desc->payload) : 0;
}

static inline const float* sms_wiiu_shadow_volume_instanced_matrices(
    const SmsWiiuShadowVolumeInstancedDesc* desc) {
    return desc ? reinterpret_cast<const float*>(desc->payload) : 0;
}

static inline uint8_t* sms_wiiu_shadow_volume_instanced_matrix_indices(
    SmsWiiuShadowVolumeInstancedDesc* desc) {
    return desc ? desc->payload + desc->matrixBytes : 0;
}

static inline const uint8_t* sms_wiiu_shadow_volume_instanced_matrix_indices(
    const SmsWiiuShadowVolumeInstancedDesc* desc) {
    return desc ? desc->payload + desc->matrixBytes : 0;
}

#endif
