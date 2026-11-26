#pragma once

#include <stdint.h>

enum SmsWiiuPipe5EndFrameResult {
    SmsWiiuPipe5Inactive = 0,
    SmsWiiuPipe5OverlapOwned = 1,
    SmsWiiuPipe5Fallback = 2,
};

/* The pipe5-owned producer frame may still be read by Latte after its
 * present callback returns.  Resource replacement/free sites use this seam
 * before touching such memory. */
extern "C" void sms_wiiu_pipe5_wait_resource_fence(void);

/* The second-generation frame ticket protocol is deliberately separate from
 * the original pipe5 marker.  These hooks keep the ticket accounting in the
 * game-thread present shim while the producer callback remains renderer-owned.
 */
extern "C" int sms_wiiu_pipe5_overlap_enabled(void);
extern "C" void sms_wiiu_pipe5_overlap_queued(uint64_t sequence, int rendered);
extern "C" void sms_wiiu_pipe5_overlap_worker_boundary_done(uint64_t sequence);
extern "C" void sms_wiiu_pipe5_overlap_retired(uint64_t sequence);
extern "C" void sms_wiiu_pipe5_overlap_fallback(const char* reason);
