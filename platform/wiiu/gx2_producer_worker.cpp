#include "gx2_producer_worker.h"
#include "gx2_producer_ring.h"
#include "jpa_counters.h"

#include "runtime.h"
#include "port_heap.h"

#include <coreinit/core.h>
#include <coreinit/cache.h>
#include <coreinit/time.h>

#include <cstring>

#if defined(PORT_GAME_SMG)
#include "jpa_final_vertex.h"
#endif

#if defined(PORT_GAME_SMG)
#include "../../games/smg/include/wiiu/gphase.h"
extern void smg_gphase_note_prod_wait(uint32_t kind, uint32_t us);
extern void smg_gphase_note_prod_service(uint32_t us);
#endif

#if defined(PORT_GAME_SMG)
extern "C" void smg_wiiu_gpuframe_stamp(int kind);
#endif

namespace gx2prod {
namespace {

/* Keep wut's types out of the public header and away from the decomp SDK's
 * same-named types.  These sizes and alignments match the existing Wii U
 * worker boundary in gx2_texture_pack.cpp. */
struct WutThread { unsigned char bytes[0x6a0]; } __attribute__((aligned(32)));
struct WutMessageQueue { unsigned char bytes[0x100]; } __attribute__((aligned(32)));
struct WutMessage { void* message; uint32_t args[3]; };
typedef int (*WutEntry)(int, const char**);

extern "C" {
BOOL wut_OSCreateThread(WutThread*, WutEntry, int32_t, char*, void*, uint32_t,
                        int32_t, uint8_t) __asm__("OSCreateThread");
int32_t wut_OSResumeThread(WutThread*) __asm__("OSResumeThread");
BOOL wut_OSJoinThread(WutThread*, int*) __asm__("OSJoinThread");
void wut_OSInitMessageQueue(WutMessageQueue*, WutMessage*, int32_t)
    __asm__("OSInitMessageQueue");
BOOL wut_OSSendMessage(WutMessageQueue*, WutMessage*, int32_t)
    __asm__("OSSendMessage");
BOOL wut_OSReceiveMessage(WutMessageQueue*, WutMessage*, int32_t)
    __asm__("OSReceiveMessage");
void wut_OSSleepTicks(int64_t) __asm__("OSSleepTicks");
/* coreinit's spinlock, not __sync: Espresso needs the OS's stwcx. workaround. */
struct WutSpinLock { uint32_t owner, pad0, recursion, pad1; } __attribute__((aligned(16)));
BOOL wut_OSUninterruptibleSpinLock_Acquire(WutSpinLock*) __asm__("OSUninterruptibleSpinLock_Acquire");
BOOL wut_OSUninterruptibleSpinLock_Release(WutSpinLock*) __asm__("OSUninterruptibleSpinLock_Release");
void wut_OSYieldThread(void) __asm__("OSYieldThread");
int64_t wut_OSGetSystemTime(void) __asm__("OSGetSystemTime");
int sms_wiiu_pipe2_enabled(void);
}

static const uint32_t kByteRingSize = 2u << 20;
static const uint32_t kServiceRingSize = 4096;
static const uint32_t kWakeSlots = 4;
static const uint32_t kWorkerStackSize = 1u << 20;
static const uint8_t kAffinityCpu0 = 1u << 0;
static const uint8_t kAffinityCpu2 = 1u << 2;
static const int32_t kMessageBlock = 1;

enum ServiceType { SERVICE_LIST, SERVICE_CALLBACK, SERVICE_BARRIER };
struct ServiceRecord {
    uint32_t type;
    uint32_t request;
    void* list;
    uint32_t bytes;
    ServiceFn fn;
    void* arg;
};

static WutThread s_worker;
static WutMessageQueue s_wakeQueue;
static WutMessage s_wakeSlots[kWakeSlots] __attribute__((aligned(32)));
static uint8_t s_workerStack[kWorkerStackSize] __attribute__((aligned(32)));
static uint8_t s_byteRing[kByteRingSize] __attribute__((aligned(64)));
static ServiceRecord s_serviceRing[kServiceRingSize] __attribute__((aligned(64)));

static Hooks s_hooks;
static ServiceCallback s_serviceCallback;
static volatile uint32_t s_byteRead;
static volatile uint32_t s_byteWrite;
static volatile uint32_t s_serviceRead;
static volatile uint32_t s_serviceWrite;
static volatile uint32_t s_requestId;
static volatile uint32_t s_requestTarget;
static volatile uint32_t s_requestKind;
static ServiceFn s_requestFn;
static void* s_requestArg;
static volatile uint32_t s_workerHandled;
static volatile uint32_t s_serviceDone;
static volatile uint32_t s_pendingRequest;
static volatile uint32_t s_stopRequested;
/* HOME/ProcUI release park (2026-11-18).  The ProcUI release callback must not
 * block on core-1 work (test 2c froze at the HOME press), so it only bumps
 * s_parkRequest and wakes the worker; the worker runs s_parkHook (closes the
 * display list it has open on its own core) at its next safe point and
 * acknowledges.  A list left open across the release was ended after the
 * return: DSI store to 0 in GX2 on the worker stack (3.1 test 1, 17 Nov). */
static volatile uint32_t s_parkRequest, s_parkAck;
static volatile OSTime s_parkRequestTick, s_parkAckTick;
static void (*s_parkHook)();
/* The worker is asleep in OSReceiveMessage with nothing open (the idle hook
 * closed its display list first).  A real HOME press on test 2h showed the
 * wake sent from the release callback is not delivered until the callback
 * returns (worker context = workerMain -> OSReceiveMessage, acked 502 ms
 * late); the late wake then ran GX2 while WHBGfx released the GPU.  An idle
 * worker therefore counts as parked: core 1 acks for it. */
static void (*s_idleHook)();
static volatile bool s_workerIdle;
static volatile uint32_t s_parkIdleAcks;
static void checkPark() {
    const uint32_t req = s_parkRequest;
    if (req == s_parkAck) return;
    __sync_synchronize();
    if (s_parkHook) s_parkHook();
    s_parkAckTick = OSGetTime();
    __sync_synchronize();
    s_parkAck = req;
}
static bool s_started;
static uint32_t s_wokenAt;
#if defined(PORT_GAME_SMG)
/* Marker wakefix: the wake decision used the queue depth seen when the
 * zero-copy chunk was reserved, long before it was committed; a worker that
 * drained and slept in between was not woken until 32 KiB more arrived or a
 * synchronous wait.  With the marker, after publishing, wake whenever the
 * worker has already consumed everything before this chunk. */
static bool s_wakeFix;
static uint32_t s_wakeFixWakes;
#endif
static Counters s_totals;
static Counters s_lastLog;
static uint32_t s_lastLogFrame;
static bool s_appendFailureLogged;
static bool s_serviceFailureLogged;
static bool s_wrongServiceCoreLogged;
static bool s_byteReservation;
static uint32_t s_reservedWrite;
static uint32_t s_reservedUsed;
static uint32_t s_reservedCapacity;
static void (*s_flushHook)();
static bool s_inService;

static constexpr uint32_t kDrawownMaxOwners = 1024;
static constexpr uint32_t kDrawownOtherOwner = kDrawownMaxOwners;
static constexpr uint32_t kDrawownMaxStack = 32;
static constexpr uint32_t kDrawownWindowFrames = 120;
struct DrawownWorkerOwner {
    const char* name;
    uint64_t busyTicks;
    uint64_t draws;
};
static bool s_drawownEnabled;
static DrawownWorkerOwner s_drawownOwners[kDrawownMaxOwners + 1];
static uint32_t s_drawownStack[kDrawownMaxStack];
static uint32_t s_drawownStackDepth;
static uint32_t s_drawownCurrent = kDrawownOtherOwner;
static int64_t s_drawownStart;
static bool s_drawownTiming;

static inline int64_t drawownNow() { return wut_OSGetSystemTime(); }

static void drawownAccumulate(int64_t now) {
    if (!s_drawownEnabled || !s_drawownTiming) return;
    const int64_t elapsed = now - s_drawownStart;
    if (elapsed > 0)
        s_drawownOwners[s_drawownCurrent].busyTicks += uint64_t(elapsed);
    s_drawownStart = now;
}

static void drawownPause() {
    if (!s_drawownEnabled || !s_drawownTiming) return;
    const int64_t now = drawownNow();
    drawownAccumulate(now);
    s_drawownTiming = false;
}

static void drawownResume() {
    if (!s_drawownEnabled || s_drawownTiming) return;
    s_drawownStart = drawownNow();
    s_drawownTiming = true;
}

static void drawownSwitch(uint32_t owner) {
    if (!s_drawownEnabled) return;
    if (owner > kDrawownMaxOwners) owner = kDrawownOtherOwner;
    const int64_t now = drawownNow();
    drawownAccumulate(now);
    s_drawownCurrent = owner;
    s_drawownStart = now;
    s_drawownTiming = true;
}

static inline uint32_t drawownMicros(uint64_t ticks) {
    return uint32_t(OSTicksToMicroseconds(ticks));
}

static inline void releaseBarrier() { __sync_synchronize(); }
static inline void acquireBarrier() { __sync_synchronize(); }
static inline bool reached(uint32_t value, uint32_t target) {
    return static_cast<int32_t>(value - target) >= 0;
}
static inline uint32_t microsSince(OSTime start) {
    return static_cast<uint32_t>(OSTicksToMicroseconds(OSGetTime() - start));
}
static inline void add(volatile uint32_t* value, uint32_t amount) {
    __sync_fetch_and_add(value, amount);
}
static inline uint32_t load(const volatile uint32_t* value) {
    return __sync_fetch_and_add(const_cast<volatile uint32_t*>(value), 0);
}

static void updateHighWater(uint32_t used) {
    uint32_t old = s_totals.queueHighWaterBytes;
    while (old < used && !__sync_bool_compare_and_swap(
                              &s_totals.queueHighWaterBytes, old, used))
        old = s_totals.queueHighWaterBytes;
}

static void wakeWorker() {
    WutMessage wake = {reinterpret_cast<void*>(uintptr_t(1)), {0, 0, 0}};
    /* Nonblocking sends coalesce naturally when a wake is already queued. */
    wut_OSSendMessage(&s_wakeQueue, &wake, 0);
}

/* The service ring has TWO writers: the worker (lists, barriers, copy and
 * texture services from its parse path) and the game thread on core 1
 * (colour and depth peeks, enqueueService/tryEnqueueService).  Unlocked, both
 * could claim the same slot and one record was lost: a lost barrier leaves
 * gx2prod::wait spinning forever (the GXPeekZ hang "minutes in", alpha 2,
 * 2026-11-15), a lost list drops a frame's draws.  The claim-fill-publish is
 * now under one spinlock; nothing sleeps while holding it.  pushes[] counts
 * pushes by core (logged once when core 1 first pushes). */
static WutSpinLock s_pushLock;
static volatile uint32_t s_pushesByCore[3];
static bool s_foreignPushLogged;

static bool pushService(const ServiceRecord& record, bool mustSucceed) {
    for (;;) {
        wut_OSUninterruptibleSpinLock_Acquire(&s_pushLock);
        const uint32_t write = s_serviceWrite;
        acquireBarrier();
        const uint32_t read = s_serviceRead;
        if (write - read < kServiceRingSize) {
            s_serviceRing[write % kServiceRingSize] = record;
            releaseBarrier();
            s_serviceWrite = write + 1;
            const uint32_t core = OSGetCoreId();
            if (core < 3) ++s_pushesByCore[core];
            const bool logForeign = core == 1 && !s_foreignPushLogged;
            if (logForeign) s_foreignPushLogged = true;
            wut_OSUninterruptibleSpinLock_Release(&s_pushLock);
            if (logForeign)
                port_log("SMS Wii U: [prod] service push from core 1 (type %u); the ring is locked for two writers\n",
                         unsigned(record.type));
            return true;
        }
        wut_OSUninterruptibleSpinLock_Release(&s_pushLock);
        if (!mustSucceed) {
            add(&s_totals.serviceFailures, 1);
            if (!s_serviceFailureLogged) {
                s_serviceFailureLogged = true;
                port_log("SMS Wii U: [prod] core-1 service queue overflow; probe failed\n");
            }
            return false;
        }
        /* A wait on core 1 drains this queue while its barrier is pending. */
        if (s_stopRequested) return false;
        checkPark();
        wut_OSSleepTicks(62156250 / 20000); // about 50 us
    }
}

static void consumeThrough(uint32_t target) {
    uint32_t read = s_byteRead;
    while (read != target) {
        const uint32_t offset = read % kByteRingSize;
        uint32_t bytes = target - read;
        const uint32_t contiguous = kByteRingSize - offset;
        if (bytes > contiguous) bytes = contiguous;
#if defined(PORT_GAME_SMG)
        smg_wprof_note_consume();
#endif
        checkPark();
        s_hooks.consume(s_byteRing + offset, bytes);
        read += bytes;
        releaseBarrier();
        s_byteRead = read;
    }
    checkPark();
}

static void drainWorker() {
#if defined(PORT_GAME_SMG)
    smg_wprof_worker_mark();
    smg_wprof_note_wake();
#endif
    drawownResume();
    for (;;) {
        acquireBarrier();
        const uint32_t target = s_byteWrite;
        const uint32_t request = s_requestId;
        const OSTime busyStart = OSGetTime();
        consumeThrough(target);

        if (request != s_workerHandled && reached(target, s_requestTarget)) {
#if defined(PORT_GAME_SMG)
            SmgWProfScope wprof_req(SmgWBucket_Req);
#endif
            const uint32_t kind = s_requestKind;
            const ServiceFn fn = s_requestFn;
            void* const arg = s_requestArg;
            s_hooks.boundary(kind);
            drawownPause();
            if (fn) {
                ServiceRecord operation = {SERVICE_CALLBACK, 0, nullptr, 0, fn, arg};
                if (!pushService(operation, true)) return;
            }
            ServiceRecord barrier = {SERVICE_BARRIER, request, nullptr, 0,
                                     nullptr, nullptr};
            if (!pushService(barrier, true)) return;
            releaseBarrier();
            s_workerHandled = request;
            /* A frame operation is the handoff between two renderer states.
             * Do not parse the next frame until core 1 has replayed the
             * operation and performed its resource rotation. */
            if (kind == WAIT_END_FRAME) {
                while (!reached(s_serviceDone, request) && !s_stopRequested) {
                    checkPark();
                    wut_OSSleepTicks(62156250 / 20000);
                }
            }
        }
        add(&s_totals.workerBusyMicros, microsSince(busyStart));
#if defined(PORT_GAME_SMG)
        smg_wprof_note_busy(microsSince(busyStart));
#endif

        acquireBarrier();
        if (s_byteRead == s_byteWrite && request == s_requestId) {
            drawownPause();
            return;
        }
        drawownResume();
    }
}

extern "C" void sms_wiiu_profiler_worker_start(void);
static int workerMain(int, const char**) {
    sms_port_heap_enter();
    sms_wiiu_profiler_worker_start();
    for (;;) {
        WutMessage message = {nullptr, {0, 0, 0}};
        if (s_idleHook) s_idleHook();   // close the open display list before sleeping
        __sync_synchronize();
        s_workerIdle = true;
        __sync_synchronize();
        checkPark();
        const BOOL got = wut_OSReceiveMessage(&s_wakeQueue, &message, kMessageBlock);
        s_workerIdle = false;
        __sync_synchronize();
        if (!got) continue;
        checkPark();
        if (!message.message || s_stopRequested) break;
        drainWorker();
    }
    return 0;
}

static Counters delta(const Counters& now, const Counters& before) {
    Counters d = now;
    d.producerAppendMicros -= before.producerAppendMicros;
    for (uint32_t i = 0; i != WAIT_KIND_COUNT; ++i)
        d.waitMicros[i] -= before.waitMicros[i];
    d.workerBusyMicros -= before.workerBusyMicros;
    d.core1ReplayMicros -= before.core1ReplayMicros;
    d.segments -= before.segments;
    d.listsCalled -= before.listsCalled;
    d.appendFailures -= before.appendFailures;
    d.serviceFailures -= before.serviceFailures;
    return d;
}

} // namespace

bool waitJpaFinalVxReady(const SmsWiiuJpaFinalVxDesc* descriptor) {
    if (!descriptor) return false;
    // No DCInvalidateRange: CPU caches are coherent between cores, and an
    // invalidate can discard core 1's dirty writes to a neighbouring descriptor.
#if defined(PORT_GAME_SMG)
    const OSTime start = OSGetTime();
    static bool s_loggedSlowWait;
    while (descriptor->ready != 1u) {
        if (!s_loggedSlowWait && OSTicksToMilliseconds(OSGetTime() - start) > 100) {
            s_loggedSlowWait = true;
            port_log("SMS Wii U: [jpaasync] worker waiting >100 ms for a final-vx payload\n");
        }
        wut_OSYieldThread();
    }
    __sync_synchronize();
    sms_wiiu_jpa_async_worker_wait(static_cast<unsigned>(OSTicksToMicroseconds(OSGetTime() - start)));
#else
    __sync_synchronize();
#endif
    return true;
}

bool start(const Hooks& hooks, bool core2) {
    if (s_started) return true;
    std::memset(&s_totals, 0, sizeof s_totals);
    std::memset(&s_lastLog, 0, sizeof s_lastLog);
    s_lastLogFrame = 0;
#if !defined(PORT_GAME_SMG)
    if (!sms_wiiu_sd_marker("gx2prod")) return false;
#endif
    if (!hooks.consume || !hooks.boundary) {
        port_log("SMS Wii U: [prod] missing consume/boundary hook; worker refused\n");
        return false;
    }

    core2 = core2 || sms_wiiu_sd_marker("gx2core2");
#if defined(PORT_GAME_SMG)
    s_wakeFix = sms_wiiu_sd_marker("wakefix") != 0;
    if (s_wakeFix) port_log("SMS Wii U: [prod] wakefix on\n");
#endif
    s_hooks = hooks;
    s_byteRead = s_byteWrite = 0;
    s_serviceRead = s_serviceWrite = 0;
    s_requestId = s_workerHandled = s_serviceDone = 0;
    s_pendingRequest = 0;
    s_requestTarget = s_requestKind = 0;
    s_requestFn = nullptr;
    s_requestArg = nullptr;
    s_stopRequested = 0;
    s_parkRequest = s_parkAck = 0;
    s_wokenAt = 0;
    s_appendFailureLogged = s_serviceFailureLogged = false;
    s_wrongServiceCoreLogged = false;
    s_byteReservation = false;
    s_reservedWrite = s_reservedUsed = s_reservedCapacity = 0;
    std::memset(&s_worker, 0, sizeof s_worker);
    std::memset(&s_wakeQueue, 0, sizeof s_wakeQueue);
    wut_OSInitMessageQueue(&s_wakeQueue, s_wakeSlots, kWakeSlots);

    const uint32_t core = core2 ? 2u : 0u;
    const uint8_t affinity = core2 ? kAffinityCpu2 : kAffinityCpu0;
    if (!wut_OSCreateThread(&s_worker, workerMain, 0, nullptr,
                            s_workerStack + sizeof s_workerStack,
                            sizeof s_workerStack, 16, affinity)) {
        port_log("SMS Wii U: [prod] worker creation failed on core %u\n", core);
        return false;
    }
    s_totals.mode = 1;
    s_totals.workerCore = core;
    s_started = true;
    wut_OSResumeThread(&s_worker);
    if (core2)
        port_log("SMS Wii U: [prod] worker core=2 (gx2core2; shares core with default AX and texture-pack work)\n");
    else
        port_log("SMS Wii U: [prod] worker core=0 (default; core 1 owns game/GX2 service, core 2 hosts AX and texture-pack work)\n");
    return true;
}

bool enabled() { return s_started; }

void drawown_enable(bool enabled) {
    s_drawownEnabled = enabled && s_started;
    s_drawownStackDepth = 0;
    s_drawownCurrent = kDrawownOtherOwner;
    s_drawownStart = 0;
    s_drawownTiming = false;
    if (s_drawownEnabled) {
        for (uint32_t i = 0; i <= kDrawownMaxOwners; ++i) {
            s_drawownOwners[i].name = nullptr;
            s_drawownOwners[i].busyTicks = 0;
            s_drawownOwners[i].draws = 0;
        }
        s_drawownOwners[kDrawownOtherOwner].name = "other";
    }
}

void drawown_owner_name(uint32_t owner, const char* name) {
    if (owner < kDrawownMaxOwners && name && *name)
        s_drawownOwners[owner].name = name;
}

void drawown_worker_push(uint32_t owner) {
    if (!s_drawownEnabled) return;
    if (s_drawownStackDepth < kDrawownMaxStack)
        s_drawownStack[s_drawownStackDepth++] = s_drawownCurrent;
    drawownSwitch(owner);
}

void drawown_worker_pop() {
    if (!s_drawownEnabled) return;
    const uint32_t owner = s_drawownStackDepth ? s_drawownStack[--s_drawownStackDepth]
                                              : kDrawownOtherOwner;
    drawownSwitch(owner);
}

void drawown_worker_draw() {
    if (s_drawownEnabled)
        ++s_drawownOwners[s_drawownCurrent].draws;
}

uint32_t drawown_worker_current() {
    return s_drawownEnabled ? s_drawownCurrent : kDrawownOtherOwner;
}

void drawown_report(uint32_t frame) {
    if (!s_drawownEnabled || !frame || frame % kDrawownWindowFrames) return;
    __sync_synchronize();
    uint64_t totalTicks = 0;
    for (uint32_t i = 0; i <= kDrawownMaxOwners; ++i)
        totalTicks += s_drawownOwners[i].busyTicks;

    uint32_t order[kDrawownMaxOwners + 1];
    for (uint32_t i = 0; i <= kDrawownMaxOwners; ++i) order[i] = i;
    const uint32_t limit = 16;
    uint32_t reported = 0;
    for (uint32_t rank = 0; rank < limit; ++rank) {
        uint32_t best = rank;
        for (uint32_t i = rank + 1; i <= kDrawownMaxOwners; ++i) {
            const DrawownWorkerOwner& candidate = s_drawownOwners[order[i]];
            const DrawownWorkerOwner& current = s_drawownOwners[order[best]];
            if (candidate.busyTicks > current.busyTicks)
                best = i;
        }
        const uint32_t tmp = order[rank];
        order[rank] = order[best];
        order[best] = tmp;
        const DrawownWorkerOwner& owner = s_drawownOwners[order[rank]];
        if (!owner.busyTicks && !owner.draws) break;
        ++reported;
    }

    port_log("[wown] frame=%u worker_us=%u\n", frame,
             unsigned(drawownMicros(totalTicks) / kDrawownWindowFrames));
    for (uint32_t rank = 0; rank < reported; ++rank) {
        const DrawownWorkerOwner& owner = s_drawownOwners[order[rank]];
        const char* name = owner.name ? owner.name : "other";
        port_log("[wown]  owner=%s us=%u draws=%u\n", name,
                 unsigned(drawownMicros(owner.busyTicks) / kDrawownWindowFrames),
                 unsigned(owner.draws * 10 / kDrawownWindowFrames));
    }
    for (uint32_t i = 0; i <= kDrawownMaxOwners; ++i) {
        s_drawownOwners[i].busyTicks = 0;
        s_drawownOwners[i].draws = 0;
    }
}

void set_flush_hook(void (*fn)()) { s_flushHook = fn; }
void set_park_hook(void (*fn)()) { s_parkHook = fn; }
void set_idle_hook(void (*fn)()) { s_idleHook = fn; }
uint32_t park_idle_acks() { return s_parkIdleAcks; }

static void serviceImpl(bool allowNested);

/* Where the worker is when a park times out: its saved context (valid while
 * it is blocked, stale if it is running) and the saved LRs up its stack.  A
 * real HOME press on 3.1 test 2f timed out 3 of 3 times while homesim never
 * did; this names the wait.  Offsets: OSThread starts with OSContext; gpr[1]
 * at 0x0C, lr 0x8C, srr0 0x98. */
static void logWorkerContext() {
    const uint8_t* t = reinterpret_cast<const uint8_t*>(&s_worker);
    const uint32_t sp = *reinterpret_cast<const uint32_t*>(t + 0x0C);
    const uint32_t lr = *reinterpret_cast<const uint32_t*>(t + 0x8C);
    const uint32_t pc = *reinterpret_cast<const uint32_t*>(t + 0x98);
    const uint32_t lo = uint32_t(uintptr_t(s_workerStack));
    const uint32_t hi = lo + uint32_t(sizeof s_workerStack);
    uint32_t chain[8] = {};
    uint32_t n = 0;
    for (uint32_t frame = sp; n < 8 && frame >= lo && frame + 8 <= hi && !(frame & 3);) {
        const uint32_t back = *reinterpret_cast<const uint32_t*>(uintptr_t(frame));
        if (back <= frame || back + 8 > hi) break;
        chain[n++] = *reinterpret_cast<const uint32_t*>(uintptr_t(back + 4));
        frame = back;
    }
    port_log("SMS Wii U: [home] worker context pc=%08X lr=%08X sp=%08X chain %08X %08X %08X %08X %08X %08X %08X %08X\n",
             pc, lr, sp, chain[0], chain[1], chain[2], chain[3], chain[4], chain[5], chain[6], chain[7]);
}

/* Called from the HOME release callback on core 1 while the GPU is still the
 * app's (ProcUI runs the priority-255 callback before WHBGfx's 100 on a HOME
 * press).  The worker may be blocked on work only core 1 submits (a display
 * list fence), so core 1 keeps servicing the ring while it waits; it never
 * waits on a barrier (test 2c froze doing that). */
bool request_park(uint32_t timeoutMicros) {
    if (!s_started) return true;
    const uint32_t req = s_parkRequest + 1;
    s_parkRequestTick = OSGetTime();
    s_parkAckTick = 0;
    releaseBarrier();
    s_parkRequest = req;
    wakeWorker();
    const OSTime startTime = OSGetTime();
    while (s_parkAck != req) {
        if (s_workerIdle) {
            acquireBarrier();
            s_parkAckTick = OSGetTime();
            s_parkAck = req;
            ++s_parkIdleAcks;
            break;
        }
        if (microsSince(startTime) >= timeoutMicros) {
            logWorkerContext();
            return false;
        }
        if (OSGetCoreId() == 1) serviceImpl(false);
        wut_OSSleepTicks(62156250 / 20000); // about 50 us
    }
    acquireBarrier();
    return true;
}

uint32_t park_ack_micros() {
    const OSTime req = s_parkRequestTick, ack = s_parkAckTick;
    if (!req || !ack || ack < req) return ~0u;
    return uint32_t(OSTicksToMicroseconds(ack - req));
}

void waitPending(uint32_t kind);

bool append(const uint8_t* bytes, uint32_t size) {
    if (!s_started || !bytes || !size) return false;
    const OSTime startTime = OSGetTime();
    if (size > kByteRingSize) {
        add(&s_totals.appendFailures, 1);
        if (!s_appendFailureLogged) {
            s_appendFailureLogged = true;
            port_log("SMS Wii U: [prod] append of %u bytes exceeds %u-byte queue; probe failed\n",
                     size, kByteRingSize);
        }
        add(&s_totals.producerAppendMicros, microsSince(startTime));
        return false;
    }

    uint32_t write = 0, used;
#if defined(PORT_GAME_SMG)
    uint64_t append_wait_start = 0;
    bool append_wait_timing = false;
#endif
    for (;;) {
        write = s_byteWrite;
        acquireBarrier();
        used = write - s_byteRead;
        if (size <= kByteRingSize - used) break;
#if defined(PORT_GAME_SMG)
        if (!append_wait_timing) {
            append_wait_start = smg_gphase_begin_timer(SmgGPhaseTimer_ProducerAppendWait);
            append_wait_timing = true;
        }
#endif
        /* The worker can itself be waiting for room in the ordered service
         * ring, so core 1 must pump that ring while applying byte backpressure. */
        wakeWorker();
        serviceImpl(true);
        wut_OSSleepTicks(62156250 / 20000); // about 50 us
    }
#if defined(PORT_GAME_SMG)
    if (append_wait_timing)
        smg_gphase_end_timer(SmgGPhaseTimer_ProducerAppendWait, append_wait_start);
#endif

    const uint32_t offset = write % kByteRingSize;
    uint32_t first = kByteRingSize - offset;
    if (first > size) first = size;
    std::memcpy(s_byteRing + offset, bytes, first);
    if (first != size) std::memcpy(s_byteRing, bytes + first, size - first);
    releaseBarrier();
    s_byteWrite = write + size;
    updateHighWater(used + size);
    add(&s_totals.producerAppendMicros, microsSince(startTime));

    /* Wake immediately from empty, then every 32 KiB while the producer is
     * ahead.  Synchronous boundaries always issue an unconditional wake. */
    if (!used || s_byteWrite - s_wokenAt >= (32u << 10)) {
        s_wokenAt = s_byteWrite;
        wakeWorker();
    }
#if defined(PORT_GAME_SMG)
    else if (s_wakeFix) {
        __sync_synchronize();  // publish byteWrite before reading byteRead
        if (s_byteRead == write) {
            s_wokenAt = s_byteWrite;
            ++s_wakeFixWakes;
            wakeWorker();
        }
    }
#endif
    // Keep ordered display-list services moving while the game is recording;
    // this also prevents a busy producer from filling the service ring before
    // its next synchronous boundary.
    if (s_serviceRead != s_serviceWrite)
        serviceImpl(false);
    return true;
}

bool reserve(uint32_t requested, uint8_t** out, uint32_t* capacity) {
    if (!s_started || !requested || !out || !capacity || s_byteReservation)
        return false;

    const uint32_t write = s_byteWrite;
    acquireBarrier();
    const uint32_t read = s_byteRead;
    ByteReservation reservation = {};
    if (!reserveBytes(write, read, kByteRingSize, requested, &reservation)) return false;

    s_byteReservation = true;
    s_reservedWrite = reservation.write;
    s_reservedUsed = reservation.used;
    s_reservedCapacity = reservation.capacity;
    *out = s_byteRing + reservation.offset;
    *capacity = reservation.capacity;
    return true;
}

bool commit(uint32_t size) {
    if (!s_started || !s_byteReservation || size > s_reservedCapacity)
        return false;

    const uint32_t write = s_reservedWrite;
    const uint32_t used = s_reservedUsed;
    ByteReservation reservation = {write, used, write % kByteRingSize, s_reservedCapacity};
    uint32_t nextWrite = 0;
    if (!commitBytes(reservation, size, &nextWrite)) return false;
    s_byteReservation = false;
    s_reservedWrite = s_reservedUsed = s_reservedCapacity = 0;
    if (!size)
        return true;

    /* Match append(): all command bytes become visible before byteWrite is
     * published, and the worker only consumes through that published index. */
    releaseBarrier();
    s_byteWrite = nextWrite;
    updateHighWater(used + size);
    if (!used || s_byteWrite - s_wokenAt >= (32u << 10)) {
        s_wokenAt = s_byteWrite;
        wakeWorker();
    }
#if defined(PORT_GAME_SMG)
    else if (s_wakeFix) {
        __sync_synchronize();  // publish byteWrite before reading byteRead
        if (s_byteRead == write) {
            s_wokenAt = s_byteWrite;
            ++s_wakeFixWakes;
            wakeWorker();
        }
    }
#endif
    if (s_serviceRead != s_serviceWrite)
        serviceImpl(false);
    return true;
}

void wake() {
    if (s_started) {
        if (s_flushHook) s_flushHook();
        wakeWorker();
    }
}

void wait(uint32_t kind) {
    if (!s_started) return;
    if (s_byteReservation && s_flushHook) s_flushHook();
    /* A pipe2 worker may reach GX2DrawDone/GXFlush through the worker-side
     * parser. It cannot wait for a barrier that only core 1 can service. */
    if (sms_wiiu_pipe2_enabled() && OSGetCoreId() != 1)
        return;
    if (s_pendingRequest)
        waitPending(kind);
    if (s_flushHook) s_flushHook();
    const OSTime startTime = OSGetTime();
    const uint32_t request = s_requestId + 1;
    s_requestTarget = s_byteWrite;
    s_requestKind = kind;
    releaseBarrier();
    s_requestId = request;
    wakeWorker();
    while (!reached(s_serviceDone, request)) {
        serviceImpl(true);
        if (!reached(s_serviceDone, request))
            wut_OSSleepTicks(62156250 / 20000); // about 50 us
    }
    if (kind < WAIT_KIND_COUNT)
        add(&s_totals.waitMicros[kind], microsSince(startTime));
#if defined(PORT_GAME_SMG)
    if (kind < WAIT_KIND_COUNT)
        smg_gphase_note_prod_wait(kind, microsSince(startTime));
#endif
}

bool queueBoundary(uint32_t kind, ServiceFn fn, void* arg) {
    if (!s_started || !fn || s_pendingRequest)
        return false;
    if (s_flushHook) s_flushHook();
    const uint32_t request = s_requestId + 1;
    s_requestTarget = s_byteWrite;
    s_requestKind = kind;
    s_requestFn = fn;
    s_requestArg = arg;
    releaseBarrier();
    s_requestId = request;
    s_pendingRequest = request;
    wakeWorker();
    return true;
}

void waitPending(uint32_t kind) {
    if (!s_started || !s_pendingRequest) return;
    if (s_byteReservation && s_flushHook) s_flushHook();
    const OSTime startTime = OSGetTime();
    const uint32_t request = s_pendingRequest;
    while (!reached(s_serviceDone, request)) {
        serviceImpl(true);
        if (!reached(s_serviceDone, request))
            wut_OSSleepTicks(62156250 / 20000);
    }
    s_pendingRequest = 0;
    if (kind < WAIT_KIND_COUNT)
        add(&s_totals.waitMicros[kind], microsSince(startTime));
#if defined(PORT_GAME_SMG)
    if (kind < WAIT_KIND_COUNT)
        smg_gphase_note_prod_wait(kind, microsSince(startTime));
#endif
}

bool enqueue_list(void* displayList, uint32_t bytes) {
    if (!s_started || !displayList || !bytes) return false;
    ServiceRecord record = {SERVICE_LIST, 0, displayList, bytes,
                            nullptr, nullptr};
    if (!pushService(record, true)) return false;
    add(&s_totals.segments, 1);
    return true;
}

bool enqueueService(ServiceFn fn, void* arg) {
    if (!s_started || !fn) return false;
    ServiceRecord record = {SERVICE_CALLBACK, 0, nullptr, 0, fn, arg};
    if (!pushService(record, true)) return false;
    add(&s_totals.segments, 1);
    return true;
}

bool tryEnqueueService(ServiceFn fn, void* arg) {
    if (!s_started || !fn) return false;
    ServiceRecord record = {SERVICE_CALLBACK, 0, nullptr, 0, fn, arg};
    if (!pushService(record, false)) return false;
    add(&s_totals.segments, 1);
    return true;
}

void publish_token(uint16_t token) {
    if (s_started && s_hooks.publish_token) s_hooks.publish_token(token);
}

void bind_service(ServiceCallback callback) { s_serviceCallback = callback; }

static void serviceImpl(bool allowNested) {
    if (!s_started) return;
    if (OSGetCoreId() != 1) {
        add(&s_totals.serviceFailures, 1);
        if (!s_wrongServiceCoreLogged) {
            s_wrongServiceCoreLogged = true;
            port_log("SMS Wii U: [prod] service called on core %u, expected core 1\n",
                     static_cast<unsigned>(OSGetCoreId()));
        }
        return;
    }
    if (s_inService && !allowNested) return;
    const bool outer = !s_inService;
    s_inService = true;
    const OSTime startTime = OSGetTime();
    uint32_t serviced = 0;
    for (;;) {
        /* Acquire AFTER loading the producer's write index and BEFORE the
         * record: pushService fills the slot, syncs, then publishes
         * s_serviceWrite.  With the barrier before the index load (as it was)
         * the record copy could see a half-filled slot -- a SERVICE_CALLBACK
         * with fn 0: ISI at PC 0 in serviceImpl on core 1 (Honeyhive
         * 2026-11-20, under the workerprof sampler's timing). */
        const uint32_t read = s_serviceRead;
        const uint32_t write = s_serviceWrite;
        acquireBarrier();
        if (read == write) break;
        const ServiceRecord record = s_serviceRing[read % kServiceRingSize];
        if (record.type != SERVICE_BARRIER) {
            releaseBarrier();
            s_serviceRead = read + 1;
        }
        if (record.type == SERVICE_LIST) {
            if (s_serviceCallback) {
                s_serviceCallback(record.list, record.bytes);
                add(&s_totals.listsCalled, 1);
            } else {
                add(&s_totals.serviceFailures, 1);
                if (!s_serviceFailureLogged) {
                    s_serviceFailureLogged = true;
                    port_log("SMS Wii U: [prod] display list reached core 1 without a service callback\n");
                }
            }
        } else if (record.type == SERVICE_CALLBACK) {
            record.fn(record.arg);
#if defined(PORT_GAME_SMG)
            smg_wiiu_gpuframe_stamp(4);
#endif
        }
        if (record.type == SERVICE_BARRIER) {
            /* Retire the queue slot before publishing completion.  A waiter
             * observing s_serviceDone may return immediately. */
            releaseBarrier();
            s_serviceRead = read + 1;
            releaseBarrier();
            s_serviceDone = record.request;
        }
        ++serviced;
    }
    if (outer) s_inService = false;
    if (serviced) add(&s_totals.core1ReplayMicros, microsSince(startTime));
}

bool drained() {
    acquireBarrier();
    return s_byteRead == s_byteWrite && s_workerHandled == s_requestId;
}

void service() {
    const OSTime startTime = OSGetTime();
    serviceImpl(false);
#if defined(PORT_GAME_SMG)
    smg_gphase_note_prod_service(microsSince(startTime));
#endif
}

void stop() {
    if (!s_started) return;
    wait(BOUNDARY_STOP);
    s_stopRequested = 1;
    releaseBarrier();
    WutMessage stopMessage = {nullptr, {0, 0, 0}};
    wut_OSSendMessage(&s_wakeQueue, &stopMessage, 0);
    int result = 0;
    wut_OSJoinThread(&s_worker, &result);
    service();
    s_started = false;
}

void snapshot(Counters* out) {
    if (!out) return;
    Counters value = {};
    value.mode = s_totals.mode;
    value.workerCore = s_totals.workerCore;
    value.producerAppendMicros = load(&s_totals.producerAppendMicros);
    for (uint32_t i = 0; i != WAIT_KIND_COUNT; ++i)
        value.waitMicros[i] = load(&s_totals.waitMicros[i]);
    value.workerBusyMicros = load(&s_totals.workerBusyMicros);
    value.core1ReplayMicros = load(&s_totals.core1ReplayMicros);
    value.queueHighWaterBytes = load(&s_totals.queueHighWaterBytes);
    value.segments = load(&s_totals.segments);
    value.listsCalled = load(&s_totals.listsCalled);
    value.appendFailures = load(&s_totals.appendFailures);
    value.serviceFailures = load(&s_totals.serviceFailures);
    *out = value;
}

Counters counters() {
    Counters result;
    snapshot(&result);
    return result;
}

void note_frame(uint32_t frame) {
    if (!frame || frame % 30 || frame == s_lastLogFrame) return;
    if (!s_started) {
        port_log("SMS Wii U: [prod] frame=%u mode=0 core=0 us/30 append=0 waitPeek=0 waitDrawsync=0 waitDrawdone=0 waitPresent=0 waitEnd=0 worker=0 replay=0 queueHigh=0 segments=0 lists=0 appendFail=0 serviceFail=0\n",
                 frame);
        s_lastLogFrame = frame;
        return;
    }
    const Counters now = counters();
    const Counters d = delta(now, s_lastLog);
    port_log("SMS Wii U: [prod] frame=%u mode=%u core=%u us/30 append=%u waitPeek=%u waitDrawsync=%u waitDrawdone=%u waitPresent=%u waitEnd=%u worker=%u replay=%u queueHigh=%u segments=%u lists=%u appendFail=%u serviceFail=%u\n",
             frame, now.mode, now.workerCore, d.producerAppendMicros,
             d.waitMicros[WAIT_PEEK], d.waitMicros[WAIT_DRAWSYNC],
             d.waitMicros[WAIT_DRAWDONE], d.waitMicros[WAIT_PRESENT_DRAIN],
             d.waitMicros[WAIT_END_FRAME], d.workerBusyMicros, d.core1ReplayMicros,
             now.queueHighWaterBytes, d.segments, d.listsCalled,
             d.appendFailures, d.serviceFailures);
    if (frame % 600 == 0)
        port_log("SMS Wii U: [prod] service pushes core0=%u core1=%u core2=%u\n", unsigned(s_pushesByCore[0]),
                 unsigned(s_pushesByCore[1]), unsigned(s_pushesByCore[2]));
    s_lastLog = now;
    s_lastLogFrame = frame;
    drawown_report(frame);
}

} // namespace gx2prod
