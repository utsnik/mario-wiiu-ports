// Galaxy THP movies: two helper threads (cores 0 and 2) share the IDCT of a
// decoded frame with the game thread (decomp-patches/thp-08). The game thread
// claims rows too, so a helper that never gets the CPU only costs time, never
// correctness. Counters use coreinit's atomics (Espresso needs its stwcx.
// workaround). Each helper flushes the rows it wrote; the game thread's
// videoDecode then tells the GX layer the planes changed, as before.
#include <coreinit/atomic.h>
#include <coreinit/event.h>
#include <coreinit/thread.h>
#include <cstdint>

extern "C" {
BOOL cafe_OSCreateThread(OSThread*, OSThreadEntryPointFn, int32_t, char*, void*, uint32_t, int32_t,
                         OSThreadAttributes) __asm__("OSCreateThread");
int32_t cafe_OSResumeThread(OSThread*) __asm__("OSResumeThread");
void cafe_OSInitEvent(OSEvent*, BOOL, OSEventMode) __asm__("OSInitEvent");
void cafe_OSSignalEvent(OSEvent*) __asm__("OSSignalEvent");
void cafe_OSWaitEvent(OSEvent*) __asm__("OSWaitEvent");
int32_t cafe_OSAddAtomic(volatile int32_t*, int32_t) __asm__("OSAddAtomic");
void cafe_DCFlushRange(void*, uint32_t) __asm__("DCFlushRange");
void cafe_OSYieldThread(void) __asm__("OSYieldThread");
void port_log(const char* fmt, ...);
}

namespace {
constexpr int kHelpers = 2;
OSThread s_thread[kHelpers] __attribute__((aligned(16)));
uint8_t s_stack[kHelpers][32 * 1024] __attribute__((aligned(16)));
OSEvent s_go[kHelpers];
void (*volatile s_fn)(void*);
void* volatile s_ctx;
int s_state;  // 0 = not tried, 1 = running, -1 = failed

int helper(int index, const char**)
{
	for (;;) {
		cafe_OSWaitEvent(&s_go[index]);
		__asm__ volatile("sync" ::: "memory");
		void (*fn)(void*) = s_fn;
		if (fn)
			fn(s_ctx);
	}
	return 0;
}

bool start()
{
	if (s_state)
		return s_state > 0;
	s_state = -1;
	const OSThreadAttributes affinity[kHelpers] = {OS_THREAD_ATTRIB_AFFINITY_CPU0, OS_THREAD_ATTRIB_AFFINITY_CPU2};
	for (int i = 0; i < kHelpers; i++) {
		cafe_OSInitEvent(&s_go[i], FALSE, OS_EVENT_MODE_AUTO);
		// Priority 17: below the game, audio and GX worker threads (16).
		if (!cafe_OSCreateThread(&s_thread[i], helper, i, nullptr, s_stack[i] + sizeof s_stack[i], sizeof s_stack[i], 17,
		                         affinity[i])) {
			port_log("[thp-mt] helper %d create failed; the game thread decodes alone\n", i);
			return i > 0 ? (s_state = 1, true) : false;
		}
		cafe_OSResumeThread(&s_thread[i]);
	}
	s_state = 1;
	port_log("[thp-mt] %d IDCT helper threads on cores 0 and 2\n", kHelpers);
	return true;
}
}  // namespace

extern "C" void smg_thp_mt_kick(void (*fn)(void*), void* ctx)
{
	if (!start())
		return;
	s_ctx = ctx;
	s_fn  = fn;
	__asm__ volatile("sync" ::: "memory");
	for (int i = 0; i < kHelpers; i++)
		cafe_OSSignalEvent(&s_go[i]);
}

extern "C" int32_t smg_thp_mt_add(volatile int32_t* p, int32_t v)
{
	return cafe_OSAddAtomic(p, v);
}

extern "C" void smg_thp_mt_flush(void* p, uint32_t n)
{
	cafe_DCFlushRange(p, n);
}

extern "C" void smg_thp_mt_yield(void)
{
	cafe_OSYieldThread();
}

// Asynchronous movie decode: one decoder thread on core 2 runs a whole
// THPVideoDecode (entropy decode + its share of IDCT strips) while the game
// thread goes on with the frame; THPSimplePlayerWrapper waits before drawing.
namespace {
OSThread s_athread __attribute__((aligned(16)));
uint8_t s_astack[64 * 1024] __attribute__((aligned(16)));
OSEvent s_ago, s_adone;
void (*volatile s_afn)(void*);
void* volatile s_actx;
volatile int s_apending;
int s_astate;

int async_decoder(int, const char**)
{
	for (;;) {
		cafe_OSWaitEvent(&s_ago);
		__asm__ volatile("sync" ::: "memory");
		if (s_afn)
			s_afn(s_actx);
		__asm__ volatile("sync" ::: "memory");
		s_apending = 0;
		cafe_OSSignalEvent(&s_adone);
	}
	return 0;
}
}  // namespace

extern "C" int smg_thp_async_start(void (*fn)(void*), void* ctx)
{
	if (!s_astate) {
		s_astate = -1;
		cafe_OSInitEvent(&s_ago, FALSE, OS_EVENT_MODE_AUTO);
		cafe_OSInitEvent(&s_adone, FALSE, OS_EVENT_MODE_AUTO);
		if (cafe_OSCreateThread(&s_athread, async_decoder, 0, nullptr, s_astack + sizeof s_astack, sizeof s_astack, 15,
		                        OS_THREAD_ATTRIB_AFFINITY_CPU2)) {
			cafe_OSResumeThread(&s_athread);
			s_astate = 1;
			port_log("[thp-mt] asynchronous movie decoder on core 2\n");
		} else {
			port_log("[thp-mt] decoder thread create failed; decoding on the game thread\n");
		}
	}
	if (s_astate < 0)
		return 0;
	s_afn      = fn;
	s_actx     = ctx;
	s_apending = 1;
	__asm__ volatile("sync" ::: "memory");
	cafe_OSSignalEvent(&s_ago);
	return 1;
}

extern "C" void smg_thp_async_wait(void)
{
	while (s_apending)
		cafe_OSWaitEvent(&s_adone);
	__asm__ volatile("sync" ::: "memory");
}
