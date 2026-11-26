#pragma once

#include <stdint.h>

static const uint32_t SMS_WIIU_PEEK_Z_NO_SAMPLE_AGE = 0xffffffffu;

#ifdef __cplusplus
extern "C" {
#endif

void sms_wiiu_gx2_peek_z_info(uint32_t* age, uint32_t* rawBits);

#ifdef __cplusplus
}
#endif
