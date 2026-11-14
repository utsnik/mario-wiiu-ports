#include "wiiu/gphase.h"

#include "runtime.h"

#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <cstdio>
#include <cstring>
#include "j3d_state_delta.h"

extern "C" int sms_wiiu_pipe5_enabled();
extern "C" void smg_vi_take_boundary_counts(unsigned* flush, unsigned* wait, unsigned* fields);
extern "C" void sms_wiiu_j3d_state_delta_take(uint32_t* values, uint32_t valueCount);
extern "C" bool sms_wiiu_j3d_state_delta_enabled();

namespace {

const unsigned kCategoryCount = 256;
const unsigned kTopCount = 12;
const unsigned kTexInvCallerCount = 6;
const OSTime kReportInterval = OSSecondsToTicks(2);
const unsigned kProdPhaseCount = 4;
const unsigned kProdWaitKindCount = 5;
const unsigned kPipe5PresentCallerCount = 3;

int s_enabled = -1;
uint64_t s_frames;
uint64_t s_phase_us[SmgGPhase_Count];
uint64_t s_category_us[SmgGPhaseCategoryKindCount][kCategoryCount];
uint64_t s_timer_us[SmgGPhaseTimer_Count];
uint64_t s_prod_wait_us[kProdPhaseCount][kProdWaitKindCount];
uint64_t s_prod_service_us;
uint64_t s_pipe5_drain_us;
uint64_t s_pipe5_bad_frames;
uint64_t s_pipe5_game_frames;
uint64_t s_pipe5_rendered_frames;
uint64_t s_pipe5_presents;
uint64_t s_pipe5_present_callers[kPipe5PresentCallerCount];
uint64_t s_pipe5_owned;
uint64_t s_pipe5_fallback;
uint64_t s_pipe5_skipped_wait;
uint64_t s_pipe5_gate_blocks;
uint64_t s_pipe5_resource_fence_waits;
SmgGPhase s_current_phase = SmgGPhase_Count;
OSTime s_report_start;
struct TexInvCaller {
    uintptr_t address;
    uint64_t count;
    uint64_t us;
};
TexInvCaller s_texinv_callers[kTexInvCallerCount];

// Per-frame view ([gframe]): frames are paced to vsync, so one that runs past
// 16.7 ms costs a whole field.  Frame time = begin_frame to begin_frame;
// phase and producer-wait times of each frame go to the slow (> 17.5 ms) or
// the fast bucket, so the report shows what a missed frame spent extra.
const unsigned kHistBins = 6;  // <17.5, <20, <25, <34.5, <51, >=51 ms
uint32_t s_hist[kHistBins];
OSTime s_frame_start;
uint64_t s_cur_phase_us[SmgGPhase_Count], s_cur_wait_us[kProdWaitKindCount];
uint64_t s_slow_phase_us[SmgGPhase_Count], s_fast_phase_us[SmgGPhase_Count];
uint64_t s_slow_wait_us[kProdWaitKindCount], s_fast_wait_us[kProdWaitKindCount];
uint64_t s_slow_total_us, s_fast_total_us;
uint32_t s_slow_frames, s_fast_frames;

// Worker buckets: written only by the worker thread, read by the core-1 report.
uint32_t s_wprof_us[SmgWBucket_Count];
uint32_t s_wprof_busy_us;
uint32_t s_wprof_draws;
uint32_t s_wprof_verts;
uint32_t s_wprof_dlcalls;
uint32_t s_wprof_wakes;
uint32_t s_wprof_consumes;
OSThread* s_wprof_thread;
int s_wprof_current = -1;
OSTime s_wprof_last;

bool enabled()
{
    if (s_enabled < 0) {
        s_enabled = sms_wiiu_sd_marker("gphase") != 0 ? 1 : 0;
        if (s_enabled != 0)
            s_report_start = OSGetTime();
    }
    return s_enabled != 0;
}

uint64_t average_us(uint64_t total)
{
    return s_frames == 0 ? 0 : total / s_frames;
}

void append_top(char* line, size_t line_size, SmgGPhaseCategoryKind kind)
{
    unsigned selected[kTopCount];
    std::memset(selected, 0xff, sizeof(selected));
    size_t used = std::strlen(line);
    const char* separator = "";
    for (unsigned rank = 0; rank < kTopCount; ++rank) {
        unsigned best = kCategoryCount;
        uint64_t best_time = 0;
        for (unsigned category = 0; category < kCategoryCount; ++category) {
            bool already_selected = false;
            for (unsigned i = 0; i < rank; ++i)
                already_selected = already_selected || selected[i] == category;
            if (!already_selected && s_category_us[kind][category] > best_time) {
                best = category;
                best_time = s_category_us[kind][category];
            }
        }
        if (best == kCategoryCount || best_time == 0)
            break;
        selected[rank] = best;
        int written = std::snprintf(line + used, line_size - used, "%s%d:%llu",
                                    separator, (int)best,
                                    (unsigned long long)average_us(best_time));
        if (written < 0 || static_cast<size_t>(written) >= line_size - used)
            break;
        used += static_cast<size_t>(written);
        separator = ",";
    }
}

void append_timer(char* line, size_t line_size, const char* name, SmgGPhaseTimer timer)
{
    size_t used = std::strlen(line);
    std::snprintf(line + used, line_size - used, " %s=%llu", name,
                  (unsigned long long)average_us(s_timer_us[timer]));
}

void append_pipe5_diag(char* line, size_t line_size)
{
    size_t used = std::strlen(line);
    const char* names[kProdPhaseCount] = {"draw", "update", "calc", "end"};
    int written = std::snprintf(line + used, line_size - used, " p5wait=");
    if (written < 0 || static_cast<size_t>(written) >= line_size - used) return;
    used += static_cast<size_t>(written);
    for (unsigned phase = 0; phase < kProdPhaseCount; ++phase) {
        written = std::snprintf(line + used, line_size - used, "%s%s:%llu,%llu,%llu,%llu,%llu",
                                phase ? ";" : "", names[phase],
                                (unsigned long long)average_us(s_prod_wait_us[phase][0]),
                                (unsigned long long)average_us(s_prod_wait_us[phase][1]),
                                (unsigned long long)average_us(s_prod_wait_us[phase][2]),
                                (unsigned long long)average_us(s_prod_wait_us[phase][3]),
                                (unsigned long long)average_us(s_prod_wait_us[phase][4]));
        if (written < 0 || static_cast<size_t>(written) >= line_size - used) return;
        used += static_cast<size_t>(written);
    }
    std::snprintf(line + used, line_size - used, " p5drain=%llu p5bad=%llu p5service=%llu p5game=%llu p5render=%llu p5present=%llu(end=%llu,gfx=%llu,service=%llu) p5owned=%llu p5fallback=%llu p5skip=%llu p5gate=%llu p5resourceFence=%llu",
                  (unsigned long long)average_us(s_pipe5_drain_us),
                  (unsigned long long)s_pipe5_bad_frames,
                  (unsigned long long)average_us(s_prod_service_us),
                  (unsigned long long)s_pipe5_game_frames,
                  (unsigned long long)s_pipe5_rendered_frames,
                  (unsigned long long)s_pipe5_presents,
                  (unsigned long long)s_pipe5_present_callers[0],
                  (unsigned long long)s_pipe5_present_callers[1],
                  (unsigned long long)s_pipe5_present_callers[2],
                  (unsigned long long)s_pipe5_owned,
                  (unsigned long long)s_pipe5_fallback,
                  (unsigned long long)s_pipe5_skipped_wait,
                  (unsigned long long)s_pipe5_gate_blocks,
                  (unsigned long long)s_pipe5_resource_fence_waits);
}

void append_texinv_callers(char* line, size_t line_size)
{
    unsigned selected[kTexInvCallerCount];
    std::memset(selected, 0xff, sizeof(selected));
    size_t used = std::strlen(line);
    const char* separator = "";
    for (unsigned rank = 0; rank < kTexInvCallerCount; ++rank) {
        unsigned best = kTexInvCallerCount;
        for (unsigned i = 0; i < kTexInvCallerCount; ++i) {
            if (s_texinv_callers[i].address == 0) continue;
            bool already = false;
            for (unsigned j = 0; j < rank; ++j) already = already || selected[j] == i;
            if (!already && (best == kTexInvCallerCount ||
                             s_texinv_callers[i].us > s_texinv_callers[best].us))
                best = i;
        }
        if (best == kTexInvCallerCount) break;
        selected[rank] = best;
        int written = std::snprintf(line + used, line_size - used, "%s%p:%llu/%llu",
                                    separator, reinterpret_cast<void*>(s_texinv_callers[best].address),
                                    (unsigned long long)s_texinv_callers[best].count,
                                    (unsigned long long)s_texinv_callers[best].us);
        if (written < 0 || static_cast<size_t>(written) >= line_size - used) break;
        used += static_cast<size_t>(written);
        separator = ",";
    }
}

void append_worker(char* line, size_t line_size)
{
    static const char* const names[SmgWBucket_Count] = {
        "parse", "fetch", "flush", "shader", "tex", "dlcall", "dlrec", "efb",
        "hook", "req", "wk"};
    uint64_t sum = 0;
    for (unsigned i = 0; i < SmgWBucket_Count; ++i) {
        sum += s_wprof_us[i];
        // hook/req/wk (always ~0) stay in the rest sum but off the line: it is
        // cut at ~1024 bytes in Beach and lost worker.draws/busy.
        if (i >= SmgWBucket_Hook) continue;
        size_t used = std::strlen(line);
        std::snprintf(line + used, line_size - used, " worker.%s=%llu", names[i],
                      (unsigned long long)average_us(s_wprof_us[i]));
    }
    size_t used = std::strlen(line);
    std::snprintf(line + used, line_size - used,
                  " worker.rest=%llu worker.busy=%llu worker.draws=%llu worker.verts=%llu worker.dlcalls=%llu",
                  (unsigned long long)average_us(s_wprof_busy_us > sum ? s_wprof_busy_us - sum : 0),
                  (unsigned long long)average_us(s_wprof_busy_us),
                  (unsigned long long)(s_frames ? s_wprof_draws / s_frames : 0),
                  (unsigned long long)(s_frames ? s_wprof_verts / s_frames : 0),
                  (unsigned long long)(s_frames ? s_wprof_dlcalls / s_frames : 0));
}

void report()
{
    char line[2048];
    std::snprintf(line, sizeof(line),
                  "[gphase] frames=%llu draw=%llu update=%llu calcAnim=%llu beginRender=%llu endRender=%llu endFrame=%llu movement=",
                  (unsigned long long)s_frames,
                  (unsigned long long)average_us(s_phase_us[SmgGPhase_Draw]),
                  (unsigned long long)average_us(s_phase_us[SmgGPhase_Update]),
                  (unsigned long long)average_us(s_phase_us[SmgGPhase_CalcAnim]),
                  (unsigned long long)average_us(s_phase_us[SmgGPhase_BeginRender]),
                  (unsigned long long)average_us(s_phase_us[SmgGPhase_EndRender]),
                  (unsigned long long)average_us(s_phase_us[SmgGPhase_EndFrame]));
    append_top(line, sizeof(line), SmgGPhaseMovement);
    size_t used = std::strlen(line);
    std::snprintf(line + used, sizeof(line) - used, " calcAnim=");
    append_top(line, sizeof(line), SmgGPhaseCategoryCalcAnim);
    used = std::strlen(line);
    std::snprintf(line + used, sizeof(line) - used, " draw=");
    append_top(line, sizeof(line), SmgGPhaseDraw);
    append_timer(line, sizeof(line), "shadow.calcView", SmgGPhaseTimer_MarioShadowCalcView);
    append_timer(line, sizeof(line), "shadow.directDraw1", SmgGPhaseTimer_MarioShadowDirectDraw1);
    append_timer(line, sizeof(line), "shadow.directDraw2", SmgGPhaseTimer_MarioShadowDirectDraw2);
    append_timer(line, sizeof(line), "shadow.fillCircle", SmgGPhaseTimer_MarioShadowFillCircle);
    append_timer(line, sizeof(line), "shadow.captureDolTexture", SmgGPhaseTimer_MarioShadowCaptureDolTexture);
    append_timer(line, sizeof(line), "shadow.GXInvalidateTexAll", SmgGPhaseTimer_MarioShadowGXInvalidateTexAll);
    append_timer(line, sizeof(line), "shadow.resetVcdVatCache", SmgGPhaseTimer_MarioShadowResetVcdVatCache);
    append_timer(line, sizeof(line), "shadow.setDefaultViewportAndScissor", SmgGPhaseTimer_MarioShadowDefaultViewportAndScissor);
    append_timer(line, sizeof(line), "shadow.cameraInit3D", SmgGPhaseTimer_MarioShadowCameraInit3D);
    append_timer(line, sizeof(line), "shadow.copyService", SmgGPhaseTimer_MarioShadowCopyService);
    append_timer(line, sizeof(line), "shadow.copyWorkerWait", SmgGPhaseTimer_MarioShadowCopyWorkerWait);
    append_timer(line, sizeof(line), "pipe5.vertexFenceWait", SmgGPhaseTimer_Pipe5VertexFenceWait);
    append_timer(line, sizeof(line), "endRender.consoleDraw", SmgGPhaseTimer_EndRenderConsoleDraw);
    append_timer(line, sizeof(line), "endRender.GXFlush", SmgGPhaseTimer_EndRenderGXFlush);
    append_timer(line, sizeof(line), "endRender.drawendXfb", SmgGPhaseTimer_EndRenderDrawendXfb);
    append_timer(line, sizeof(line), "endRender.copyXfb", SmgGPhaseTimer_EndRenderCopyXfb);
    append_timer(line, sizeof(line), "endRender.calcCombinationRatio", SmgGPhaseTimer_EndRenderCalcCombinationRatio);
    append_timer(line, sizeof(line), "producer.appendWait", SmgGPhaseTimer_ProducerAppendWait);
    append_timer(line, sizeof(line), "producer.flushHook", SmgGPhaseTimer_ProducerFlushHook);
    append_worker(line, sizeof(line));
    size_t texinv_used = std::strlen(line);
    std::snprintf(line + texinv_used, sizeof(line) - texinv_used, " texinvwait=");
    append_texinv_callers(line, sizeof(line));
    if (sms_wiiu_pipe5_enabled())
        append_pipe5_diag(line, sizeof(line));
    port_log("%s", line);
    if (sms_wiiu_j3d_state_delta_enabled()) {
        uint32_t d[J3D_STATE_DELTA_REPORT_COUNT];
        sms_wiiu_j3d_state_delta_take(d, J3D_STATE_DELTA_REPORT_COUNT);
        port_log("[j3dstatedelta] materialLists=%u stateOps=%u cpSame=%u cpDiff=%u bpSame=%u bpDiff=%u xfSame=%u xfDiff=%u stateFlush=%u draws=%u skippedOps=%u dirtyDomains=%u checkRuns=%u checkMismatches=%u\n",
                 d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11], d[12], d[13]);
    }
}

} // namespace

bool smg_gphase_enabled() { return enabled(); }

volatile uint32_t g_smg_wprof_on;

void smg_wprof_worker_mark()
{
    s_wprof_thread = OSGetCurrentThread();
    s_wprof_current = -1;
}

int smg_wprof_enter(SmgWBucket bucket)
{
    if (OSGetCurrentThread() != s_wprof_thread) return -2;
    const OSTime now = OSGetTime();
    const int previous = s_wprof_current;
    if (previous >= 0)
        s_wprof_us[previous] += static_cast<uint32_t>(OSTicksToMicroseconds(now - s_wprof_last));
    s_wprof_current = static_cast<int>(bucket);
    s_wprof_last = now;
    return previous;
}

void smg_wprof_leave(int previous)
{
    const OSTime now = OSGetTime();
    if (s_wprof_current >= 0)
        s_wprof_us[s_wprof_current] += static_cast<uint32_t>(OSTicksToMicroseconds(now - s_wprof_last));
    s_wprof_current = previous;
    s_wprof_last = now;
}

void smg_wprof_note_busy(uint32_t us)
{
    if (g_smg_wprof_on && OSGetCurrentThread() == s_wprof_thread)
        s_wprof_busy_us += us;
}

void smg_wprof_note_draw(uint32_t vertices)
{
    if (g_smg_wprof_on && OSGetCurrentThread() == s_wprof_thread) {
        ++s_wprof_draws;
        s_wprof_verts += vertices;
    }
}

void smg_wprof_note_dlcall()
{
    if (g_smg_wprof_on && OSGetCurrentThread() == s_wprof_thread)
        ++s_wprof_dlcalls;
}

void smg_wprof_note_wake()
{
    if (g_smg_wprof_on && OSGetCurrentThread() == s_wprof_thread)
        ++s_wprof_wakes;
}

void smg_wprof_note_consume()
{
    if (g_smg_wprof_on && OSGetCurrentThread() == s_wprof_thread)
        ++s_wprof_consumes;
}

void smg_gphase_begin_frame()
{
    if (enabled()) {
        g_smg_wprof_on = 1;
        ++s_frames;
        const OSTime now = OSGetTime();
        if (s_frame_start) {
            const uint64_t us = static_cast<uint64_t>(OSTicksToMicroseconds(now - s_frame_start));
            const unsigned bin = us < 17500 ? 0 : us < 20000 ? 1 : us < 25000 ? 2 : us < 34500 ? 3 : us < 51000 ? 4 : 5;
            ++s_hist[bin];
            const bool slow = bin != 0;
            uint64_t* phase = slow ? s_slow_phase_us : s_fast_phase_us;
            uint64_t* wait = slow ? s_slow_wait_us : s_fast_wait_us;
            for (unsigned i = 0; i < SmgGPhase_Count; ++i) phase[i] += s_cur_phase_us[i];
            for (unsigned i = 0; i < kProdWaitKindCount; ++i) wait[i] += s_cur_wait_us[i];
            if (slow) { ++s_slow_frames; s_slow_total_us += us; }
            else { ++s_fast_frames; s_fast_total_us += us; }
        }
        std::memset(s_cur_phase_us, 0, sizeof(s_cur_phase_us));
        std::memset(s_cur_wait_us, 0, sizeof(s_cur_wait_us));
        s_frame_start = now;
    }
}

uint64_t smg_gphase_begin(SmgGPhase phase)
{
    if (enabled()) {
        s_current_phase = phase;
        return static_cast<uint64_t>(OSGetTime());
    }
    return 0;
}

void smg_gphase_end(SmgGPhase phase, uint64_t start)
{
    if (enabled()) {
        const uint64_t us = static_cast<uint64_t>(OSTicksToMicroseconds(OSGetTime() - static_cast<OSTime>(start)));
        s_phase_us[phase] += us;
        s_cur_phase_us[phase] += us;
        if (s_current_phase == phase)
            s_current_phase = SmgGPhase_Count;
    }
}

uint64_t smg_gphase_begin_category(SmgGPhaseCategoryKind kind, int category)
{
    return enabled() && category >= 0 && category < static_cast<int>(kCategoryCount)
               ? static_cast<uint64_t>(OSGetTime())
               : 0;
}

void smg_gphase_end_category(SmgGPhaseCategoryKind kind, int category, uint64_t start)
{
    if (enabled() && category >= 0 && category < static_cast<int>(kCategoryCount))
        s_category_us[kind][category] += static_cast<uint64_t>(OSTicksToMicroseconds(OSGetTime() - static_cast<OSTime>(start)));
}

uint64_t smg_gphase_begin_timer(SmgGPhaseTimer timer)
{
    return enabled() && timer >= 0 && timer < SmgGPhaseTimer_Count
               ? static_cast<uint64_t>(OSGetTime())
               : 0;
}

void smg_gphase_end_timer(SmgGPhaseTimer timer, uint64_t start)
{
    if (enabled() && timer >= 0 && timer < SmgGPhaseTimer_Count)
        s_timer_us[timer] += static_cast<uint64_t>(OSTicksToMicroseconds(OSGetTime() - static_cast<OSTime>(start)));
}

void smg_gphase_note_texinv_wait(uintptr_t caller, uint64_t us)
{
    if (!enabled()) return;
    for (unsigned i = 0; i < kTexInvCallerCount; ++i) {
        if (s_texinv_callers[i].address == caller || s_texinv_callers[i].address == 0) {
            s_texinv_callers[i].address = caller;
            ++s_texinv_callers[i].count;
            s_texinv_callers[i].us += us;
            return;
        }
    }
    unsigned least = 0;
    for (unsigned i = 1; i < kTexInvCallerCount; ++i)
        if (s_texinv_callers[i].us < s_texinv_callers[least].us) least = i;
    s_texinv_callers[least] = {caller, 1, us};
}

void smg_gphase_note_prod_wait(uint32_t kind, uint32_t us)
{
    if (enabled() && kind < kProdWaitKindCount) s_cur_wait_us[kind] += us;
    if (!enabled() || !sms_wiiu_pipe5_enabled() || kind >= kProdWaitKindCount) return;
    unsigned phase;
    switch (s_current_phase) {
    case SmgGPhase_Draw: phase = 0; break;
    case SmgGPhase_Update: phase = 1; break;
    case SmgGPhase_CalcAnim: phase = 2; break;
    case SmgGPhase_EndFrame: phase = 3; break;
    default: return;
    }
    s_prod_wait_us[phase][kind] += us;
}

void smg_gphase_note_prod_service(uint32_t us)
{
    if (enabled() && sms_wiiu_pipe5_enabled())
        s_prod_service_us += us;
}

void smg_gphase_note_pipe5_drain(uint32_t us)
{
    if (enabled() && sms_wiiu_pipe5_enabled())
        s_pipe5_drain_us += us;
}

void smg_gphase_note_pipe5_bad_frame()
{
    if (enabled() && sms_wiiu_pipe5_enabled())
        ++s_pipe5_bad_frames;
}

void smg_gphase_note_pipe5_game_frame(bool rendered)
{
    if (!enabled() || !sms_wiiu_pipe5_enabled()) return;
    ++s_pipe5_game_frames;
    if (rendered)
        ++s_pipe5_rendered_frames;
}

void smg_gphase_note_pipe5_present(const char* caller)
{
    if (!enabled() || !sms_wiiu_pipe5_enabled()) return;
    ++s_pipe5_presents;
    unsigned index = kPipe5PresentCallerCount - 1;
    if (caller && std::strcmp(caller, "endFrame") == 0) index = 0;
    else if (caller && std::strcmp(caller, "gfxFrame") == 0) index = 1;
    ++s_pipe5_present_callers[index];
}

void smg_gphase_note_pipe5_owned()
{
    if (enabled() && sms_wiiu_pipe5_enabled())
        ++s_pipe5_owned;
}

void smg_gphase_note_pipe5_fallback()
{
    if (enabled() && sms_wiiu_pipe5_enabled())
        ++s_pipe5_fallback;
}

void smg_gphase_note_pipe5_skipped_wait()
{
    if (enabled() && sms_wiiu_pipe5_enabled())
        ++s_pipe5_skipped_wait;
}

void smg_gphase_note_pipe5_gate_block()
{
    if (enabled() && sms_wiiu_pipe5_enabled())
        ++s_pipe5_gate_blocks;
}

void smg_gphase_note_pipe5_resource_fence()
{
    if (enabled() && sms_wiiu_pipe5_enabled())
        ++s_pipe5_resource_fence_waits;
}

void smg_gphase_report_if_due()
{
    if (!enabled() || s_frames == 0 || OSGetTime() - s_report_start < kReportInterval)
        return;
    report();
    {
        // Phases: draw/update/calcAnim/beginRender/endRender/endFrame; waits by
        // producer kind 0..4 (peek, drawsync, drawdone, present, end), us per frame.
        char g[600];
        auto avg = [](uint64_t v, uint32_t n) { return (unsigned long long)(n ? v / n : 0); };
        int n = std::snprintf(g, sizeof g, "[gframe] hist(<17.5/<20/<25/<34.5/<51/more)=%u/%u/%u/%u/%u/%u slow=%u avg=%llu ph=",
                              s_hist[0], s_hist[1], s_hist[2], s_hist[3], s_hist[4], s_hist[5], s_slow_frames,
                              avg(s_slow_total_us, s_slow_frames));
        for (unsigned i = 0; i < SmgGPhase_Count && n > 0 && n < int(sizeof g) - 16; ++i)
            n += std::snprintf(g + n, sizeof g - n, "%s%llu", i ? "," : "", avg(s_slow_phase_us[i], s_slow_frames));
        n += std::snprintf(g + n, sizeof g - n, " w=");
        for (unsigned i = 0; i < kProdWaitKindCount && n > 0 && n < int(sizeof g) - 16; ++i)
            n += std::snprintf(g + n, sizeof g - n, "%s%llu", i ? "," : "", avg(s_slow_wait_us[i], s_slow_frames));
        n += std::snprintf(g + n, sizeof g - n, " | fast=%u avg=%llu ph=", s_fast_frames, avg(s_fast_total_us, s_fast_frames));
        for (unsigned i = 0; i < SmgGPhase_Count && n > 0 && n < int(sizeof g) - 16; ++i)
            n += std::snprintf(g + n, sizeof g - n, "%s%llu", i ? "," : "", avg(s_fast_phase_us[i], s_fast_frames));
        n += std::snprintf(g + n, sizeof g - n, " w=");
        for (unsigned i = 0; i < kProdWaitKindCount && n > 0 && n < int(sizeof g) - 16; ++i)
            n += std::snprintf(g + n, sizeof g - n, "%s%llu", i ? "," : "", avg(s_fast_wait_us[i], s_fast_frames));
        unsigned bf = 0, bw = 0, fl = 0;
        smg_vi_take_boundary_counts(&bf, &bw, &fl);
        port_log("%s bnd(flush/wait/fields)=%u/%u/%u\n", g, bf, bw, fl);
        std::memset(s_hist, 0, sizeof(s_hist));
        std::memset(s_slow_phase_us, 0, sizeof(s_slow_phase_us));
        std::memset(s_fast_phase_us, 0, sizeof(s_fast_phase_us));
        std::memset(s_slow_wait_us, 0, sizeof(s_slow_wait_us));
        std::memset(s_fast_wait_us, 0, sizeof(s_fast_wait_us));
        s_slow_total_us = s_fast_total_us = 0;
        s_slow_frames = s_fast_frames = 0;
    }
    std::memset(s_phase_us, 0, sizeof(s_phase_us));
    std::memset(s_category_us, 0, sizeof(s_category_us));
    std::memset(s_timer_us, 0, sizeof(s_timer_us));
    std::memset(s_prod_wait_us, 0, sizeof(s_prod_wait_us));
    s_prod_service_us = s_pipe5_drain_us = s_pipe5_bad_frames = 0;
    s_pipe5_game_frames = s_pipe5_rendered_frames = s_pipe5_presents = 0;
    std::memset(s_pipe5_present_callers, 0, sizeof(s_pipe5_present_callers));
    s_pipe5_owned = s_pipe5_fallback = s_pipe5_skipped_wait = 0;
    s_pipe5_gate_blocks = s_pipe5_resource_fence_waits = 0;
    std::memset(s_texinv_callers, 0, sizeof(s_texinv_callers));
    std::memset(s_wprof_us, 0, sizeof(s_wprof_us));
    s_wprof_busy_us = s_wprof_draws = s_wprof_verts = s_wprof_dlcalls = s_wprof_wakes = s_wprof_consumes = 0;
    s_frames = 0;
    s_report_start = OSGetTime();
}
