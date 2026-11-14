#pragma once

enum SmsWiiuPipe5EndFrameResult {
    SmsWiiuPipe5Inactive = 0,
    SmsWiiuPipe5OverlapOwned = 1,
    SmsWiiuPipe5Fallback = 2,
};

/* The pipe5-owned producer frame may still be read by Latte after its
 * present callback returns.  Resource replacement/free sites use this seam
 * before touching such memory. */
extern "C" void sms_wiiu_pipe5_wait_resource_fence(void);
