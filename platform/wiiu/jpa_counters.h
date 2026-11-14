#pragma once

#ifdef __cplusplus
extern "C" {
#endif
void sms_wiiu_jpa_particle(void);
void sms_wiiu_jpa_draw(void);
void sms_wiiu_jpa_split(void);
void sms_wiiu_jpa_batched(void);
void sms_wiiu_jpa_reject_type(void);
void sms_wiiu_jpa_reject_callback(void);
void sms_wiiu_jpa_reject_prj(void);
void sms_wiiu_jpa_reject_ineligible(void);
void sms_wiiu_jpa_reject_reason(unsigned reason);
void sms_wiiu_jpa_type3(void);
void sms_wiiu_jpa_type4(void);
void sms_wiiu_jpa_type2(void);
void sms_wiiu_jpa_histogram(int child, unsigned type, const char* func, int callback,
                            const void* emitter);
void sms_wiiu_jpa_finalvx_run(unsigned particles, unsigned vertices, unsigned bytes);
void sms_wiiu_jpa_finalvx_fallback(unsigned reason);
void sms_wiiu_jpa_finalvx_check_run(void);
void sms_wiiu_jpa_finalvx_check_mismatch(const char* field);
void sms_wiiu_jpa_finalvx_report(void);
#ifdef __cplusplus
}

enum SmsWiiuJpaRejectReason {
	SMS_WIIU_JPA_REJECT_TEXTURE = 0,
	SMS_WIIU_JPA_REJECT_COLOR,
	SMS_WIIU_JPA_REJECT_SCALE,
	SMS_WIIU_JPA_REJECT_PROJECTION,
	SMS_WIIU_JPA_REJECT_BLEND_ZMODE,
	SMS_WIIU_JPA_REJECT_CHILD,
	SMS_WIIU_JPA_REJECT_OTHER,
	SMS_WIIU_JPA_REJECT_COLOR_RGB,
	SMS_WIIU_JPA_REJECT_COLOR_ALPHA,
};

enum SmsWiiuJpaFinalVxFallbackReason {
	SMS_WIIU_JPA_FINALVX_FALLBACK_VAT = 0,
	SMS_WIIU_JPA_FINALVX_FALLBACK_ALLOC,
	SMS_WIIU_JPA_FINALVX_FALLBACK_SLOT_FULL,
	SMS_WIIU_JPA_FINALVX_FALLBACK_SLOT_BUSY,
	SMS_WIIU_JPA_FINALVX_FALLBACK_RECORD_FULL,
	SMS_WIIU_JPA_FINALVX_FALLBACK_DL_CACHE,
	SMS_WIIU_JPA_FINALVX_FALLBACK_CHECK_SPLIT,
	SMS_WIIU_JPA_FINALVX_FALLBACK_UNSUPPORTED,
	SMS_WIIU_JPA_FINALVX_FALLBACK_WORKER_BADDESC,
	SMS_WIIU_JPA_FINALVX_FALLBACK_WORKER_COLD,
	SMS_WIIU_JPA_FINALVX_FALLBACK_WORKER_INACTIVE,
	SMS_WIIU_JPA_FINALVX_FALLBACK_WORKER_DLOPEN,
	SMS_WIIU_JPA_FINALVX_FALLBACK_WORKER_DLPENDING,
	SMS_WIIU_JPA_FINALVX_FALLBACK_REASON_COUNT,
};
#endif
