#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)

#define SMG_WIIU_JPA_ASYNC_JOB_TYPES 1
#include "JSystem/JParticle/JPAEmitter.hpp"
#include "wiiu/jpa_async.h"
#include "wiiu/jpa_counters.h"
#include "wiiu/jpa_final_vertex.h"

#include <revolution/os.h>
#include <stdint.h>

extern "C" {
typedef int (*JpaAsyncThreadEntry)(int, const char**);
int cafe_OSCreateThread(void*, JpaAsyncThreadEntry, int32_t, char*, void*, uint32_t, int32_t,
                        uint8_t) __asm__("OSCreateThread");
int32_t cafe_OSResumeThread(void*) __asm__("OSResumeThread");
void cafe_OSInitEvent(void*, int, int) __asm__("OSInitEvent");
void cafe_OSSignalEvent(void*) __asm__("OSSignalEvent");
void cafe_OSWaitEvent(void*) __asm__("OSWaitEvent");
void cafe_OSYieldThread(void) __asm__("OSYieldThread");
void cafe_DCFlushRange(void*, uint32_t) __asm__("DCFlushRange");
uint32_t OSGetCoreId(void);
int sms_wiiu_jpa_async_enabled(void);
void port_log(const char*, ...);
}

namespace {

static const uint32_t kMaxJobs = 256;
static const uint32_t kStackBytes = 32 * 1024;

struct Thread { uint8_t bytes[0x6a0]; } __attribute__((aligned(32)));
struct Event { uint8_t bytes[0x100]; } __attribute__((aligned(32)));

using smg_jpa_async::Job;

Thread s_thread;
Event s_event;
uint8_t s_stack[kStackBytes] __attribute__((aligned(32)));
Job s_jobs[kMaxJobs] __attribute__((aligned(32)));
// Monotonic counters, never reset: jobs live in a ring indexed by count %
// kMaxJobs. Only core 1 publishes, only the helper claims and completes, so a
// join from another thread (the gx2prod worker retiring a frame) can wait on
// them without racing a reset.
volatile uint32_t s_claim;      // helper only
volatile uint32_t s_published;  // core 1 only
volatile uint32_t s_completed;  // helper only
volatile int s_state;
bool s_loggedStart;
int s_loggedSlowJoin;

static inline uint32_t loadAcquire(const volatile uint32_t& value) {
    return __atomic_load_n(&value, __ATOMIC_ACQUIRE);
}

static inline void storeRelease(volatile uint32_t& value, uint32_t next) {
    __atomic_store_n(&value, next, __ATOMIC_RELEASE);
}

void runJob(uint32_t index) {
    Job& job = s_jobs[index % kMaxJobs];
    SmsWiiuJpaFinalVxDesc* const descriptor = job.descriptor;
    const OSTime start = OSGetTime();
    job.fill(&job);
    // For the GPU: the worker draws straight from these bytes.
    cafe_DCFlushRange(const_cast<uint8_t*>(descriptor->vertices), descriptor->vertexBytes);
    cafe_DCFlushRange(const_cast<uint32_t*>(descriptor->indices), descriptor->indexBytes);
    cafe_DCFlushRange(descriptor, sizeof(*descriptor));
    sms_wiiu_jpa_finalvx_publish_ready(&descriptor->ready);
    // This interface accumulates atomically against concurrent profiler resets.
    sms_wiiu_jpa_async_helper_busy(
        static_cast<unsigned>(OSTicksToMicroseconds(OSGetTime() - start)));
    // Last access to this slot: the producer may reuse it after this release.
    storeRelease(s_completed, index + 1);
}

void runAvailableJobs() {
    for (;;) {
        const uint32_t index = loadAcquire(s_claim);
        const uint32_t published = loadAcquire(s_published);
        if (index == published) return;
        runJob(index);
        storeRelease(s_claim, index + 1);
    }
}

int helper(int, const char**) {
    for (;;) {
        cafe_OSWaitEvent(&s_event);
        runAvailableJobs();
    }
    return 0;
}

bool startHelper() {
    const int state = __atomic_load_n(&s_state, __ATOMIC_ACQUIRE);
    if (state != 0) return state > 0;
    if (!sms_wiiu_jpa_async_enabled()) return false;
    __atomic_store_n(&s_state, -1, __ATOMIC_RELEASE);
    cafe_OSInitEvent(&s_event, 0, 1);
    if (!cafe_OSCreateThread(&s_thread, helper, 0, nullptr, s_stack + sizeof(s_stack),
                             sizeof(s_stack), 17, 1 << 2)) {
        sms_wiiu_jpa_async_fallback(SMS_WIIU_JPA_ASYNC_FALLBACK_HELPER_START);
        port_log("SMS Wii U: [jpaasync] core-2 helper create failed; synchronous path retained\n");
        return false;
    }
    cafe_OSResumeThread(&s_thread);
    __atomic_store_n(&s_state, 1, __ATOMIC_RELEASE);
    if (!s_loggedStart) {
        s_loggedStart = true;
        port_log("SMS Wii U: [jpaasync] core-2 helper started\n");
    }
    return true;
}

} // namespace

extern "C" bool sms_wiiu_jpa_async_start(void) {
    return OSGetCoreId() == 1 && startHelper();
}

extern "C" bool sms_wiiu_jpa_async_enqueue(const smg_jpa_async::Job& supplied) {
    if (OSGetCoreId() != 1) {
        sms_wiiu_jpa_async_fallback(SMS_WIIU_JPA_ASYNC_FALLBACK_CORE);
        return false;
    }
    if (!startHelper()) return false;
    if (!supplied.descriptor || !supplied.fill || !supplied.first ||
        supplied.first == supplied.end || supplied.particleCount == 0 ||
        supplied.runKind > smg_jpa_async::RunDirectional) {
        // Retain the existing admission-failure counter ABI.
        sms_wiiu_jpa_async_fallback(SMS_WIIU_JPA_ASYNC_FALLBACK_PAYLOAD);
        return false;
    }
    const uint32_t index = loadAcquire(s_published);
    if (index - loadAcquire(s_completed) >= kMaxJobs) {
        sms_wiiu_jpa_async_fallback(SMS_WIIU_JPA_ASYNC_FALLBACK_QUEUE_FULL);
        return false;
    }
    s_jobs[index % kMaxJobs] = supplied;
    supplied.descriptor->ready = SMS_WIIU_JPA_FINALVX_PENDING;
    sms_wiiu_jpa_async_job(supplied.particleCount);
    // CPU caches are coherent; publish the copied work and borrowed bounds
    // with release ordering. Only the completed output needs a GPU flush.
    storeRelease(s_published, index + 1);
    cafe_OSSignalEvent(&s_event);
    return true;
}

extern "C" void sms_wiiu_jpa_async_join(void) {
    if (!sms_wiiu_jpa_async_enabled() || __atomic_load_n(&s_state, __ATOMIC_ACQUIRE) <= 0) return;
    const uint32_t target = loadAcquire(s_published);
    // At most kMaxJobs are outstanding; signed differences handle wraparound.
    if (static_cast<int32_t>(loadAcquire(s_completed) - target) >= 0) return;
    cafe_OSSignalEvent(&s_event);
    const OSTime start = OSGetTime();
    while (static_cast<int32_t>(loadAcquire(s_completed) - target) < 0) {
        if (OSTicksToMilliseconds(OSGetTime() - start) > 100 &&
            !__atomic_exchange_n(&s_loggedSlowJoin, 1, __ATOMIC_RELAXED)) {
            port_log("SMS Wii U: [jpaasync] join waiting >100 ms on core %u: published=%u claim=%u completed=%u\n",
                     (unsigned)OSGetCoreId(), (unsigned)target,
                     (unsigned)loadAcquire(s_claim), (unsigned)loadAcquire(s_completed));
        }
        cafe_OSYieldThread();
    }
}

extern "C" bool sms_wiiu_jpa_async_wait_ready(const volatile uint32_t* ready) {
    if (!ready) return false;
    while (loadAcquire(*ready) != SMS_WIIU_JPA_FINALVX_READY) cafe_OSYieldThread();
    return true;
}

#endif
