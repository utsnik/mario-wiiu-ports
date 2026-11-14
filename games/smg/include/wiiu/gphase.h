#pragma once

#include <stdint.h>

enum SmgGPhase {
    SmgGPhase_Draw,
    SmgGPhase_Update,
    SmgGPhase_CalcAnim,
    SmgGPhase_BeginRender,
    SmgGPhase_EndRender,
    SmgGPhase_EndFrame,
    SmgGPhase_WaitRetrace,  // MainLoopFramework::waitForRetrace at the end of frameLoop
    SmgGPhase_Count
};

enum SmgGPhaseCategoryKind {
    SmgGPhaseMovement,
    SmgGPhaseCategoryCalcAnim,
    SmgGPhaseDraw,
    SmgGPhaseCategoryKindCount
};

enum SmgGPhaseTimer {
    SmgGPhaseTimer_MarioShadowCalcView,
    SmgGPhaseTimer_MarioShadowDirectDraw1,
    SmgGPhaseTimer_MarioShadowDirectDraw2,
    SmgGPhaseTimer_MarioShadowFillCircle,
    SmgGPhaseTimer_MarioShadowCaptureDolTexture,
    SmgGPhaseTimer_MarioShadowGXInvalidateTexAll,
    SmgGPhaseTimer_MarioShadowResetVcdVatCache,
    SmgGPhaseTimer_MarioShadowDefaultViewportAndScissor,
    SmgGPhaseTimer_MarioShadowCameraInit3D,
    SmgGPhaseTimer_MarioShadowCopyService,
    SmgGPhaseTimer_MarioShadowCopyWorkerWait,
    SmgGPhaseTimer_Pipe5VertexFenceWait,
    SmgGPhaseTimer_EndRenderConsoleDraw,
    SmgGPhaseTimer_EndRenderGXFlush,
    SmgGPhaseTimer_EndRenderDrawendXfb,
    SmgGPhaseTimer_EndRenderCopyXfb,
    SmgGPhaseTimer_EndRenderCalcCombinationRatio,
    SmgGPhaseTimer_ProducerAppendWait,
    SmgGPhaseTimer_ProducerFlushHook,
    SmgGPhaseTimer_Count
};

bool smg_gphase_enabled();
void smg_gphase_begin_frame();
uint64_t smg_gphase_begin(SmgGPhase phase);
void smg_gphase_end(SmgGPhase phase, uint64_t start);
uint64_t smg_gphase_begin_category(SmgGPhaseCategoryKind kind, int category);
void smg_gphase_end_category(SmgGPhaseCategoryKind kind, int category, uint64_t start);
uint64_t smg_gphase_begin_timer(SmgGPhaseTimer timer);
void smg_gphase_end_timer(SmgGPhaseTimer timer, uint64_t start);
void smg_gphase_note_texinv_wait(uintptr_t caller, uint64_t us);
void smg_gphase_note_prod_wait(uint32_t kind, uint32_t us);
void smg_gphase_note_prod_service(uint32_t us);
void smg_gphase_note_pipe5_drain(uint32_t us);
void smg_gphase_note_pipe5_bad_frame();
void smg_gphase_note_pipe5_game_frame(bool rendered);
void smg_gphase_note_pipe5_present(const char* caller);
void smg_gphase_note_pipe5_owned();
void smg_gphase_note_pipe5_fallback();
void smg_gphase_note_pipe5_skipped_wait();
void smg_gphase_note_pipe5_gate_block();
void smg_gphase_note_pipe5_resource_fence();
void smg_gphase_report_if_due();

// Worker-thread cost buckets (diagnostic, active only under the gphase marker).
// Buckets are exclusive: entering a nested bucket pauses the enclosing one.
enum SmgWBucket {
    SmgWBucket_Parse,
    SmgWBucket_Fetch,
    SmgWBucket_Flush,
    SmgWBucket_Shader,
    SmgWBucket_Tex,
    SmgWBucket_DlCall,
    SmgWBucket_DlRec,
    SmgWBucket_Efb,
    SmgWBucket_Hook,
    SmgWBucket_Req,
    SmgWBucket_Wake,
    SmgWBucket_Count
};

extern volatile uint32_t g_smg_wprof_on;
void smg_wprof_worker_mark();
int smg_wprof_enter(SmgWBucket bucket);
void smg_wprof_leave(int previous);
void smg_wprof_note_busy(uint32_t us);
void smg_wprof_note_draw(uint32_t vertices);
void smg_wprof_note_dlcall();
void smg_wprof_note_wake();
void smg_wprof_note_consume();

struct SmgWProfScope {
    int previous;
    explicit SmgWProfScope(SmgWBucket bucket)
        : previous(g_smg_wprof_on ? smg_wprof_enter(bucket) : -2) {}
    ~SmgWProfScope() { if (previous != -2) smg_wprof_leave(previous); }
};
