#include <sms_gx/gx_pc.h>
#include <sms_gx/gxvert_pc.h>
#include "gx_internal.h"
#include "games/smg/include/wiiu/gphase.h"
#include "platform/wiiu/gx2_producer_worker.h"

#include <dolphin/gx.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" uint32_t sms_gx_host_test_parse(const uint8_t*, uint32_t);
extern "C" uint32_t sms_gx_host_test_cp_vcd_hi();
extern "C" void sms_gx_host_test_reset();
extern "C" uint32_t sms_gx_host_test_unknown_opcodes();
extern "C" void sms_gx_host_test_redirect(uint8_t*);
extern "C" uint32_t sms_gx_host_test_redirect_len();
extern "C" void sms_gx_host_test_record(uint8_t, uint32_t);

volatile uint32_t g_smg_wprof_on = 0;
uint8_t* port_mem1_base = nullptr;
uint32_t port_mem1_size = 0;
uint8_t* sms_wiiu_mem2_base = nullptr;
uint32_t sms_wiiu_mem2_size = 0;

extern "C" void sms_port_heap_enter() {}
extern "C" void sms_port_heap_leave() {}
extern "C" int sms_wiiu_sd_marker(const char*) { return 0; }
extern "C" void port_log(const char*, ...) {}
extern "C" uint64_t OSGetTime() { return 0; }
extern "C" void sms_wiiu_jpa_finalvx_fallback(unsigned) {}

bool smg_gphase_enabled() { return false; }
uint64_t smg_gphase_begin_timer(SmgGPhaseTimer) { return 0; }
void smg_gphase_end_timer(SmgGPhaseTimer, uint64_t) {}
int smg_wprof_enter(SmgWBucket) { return -2; }
void smg_wprof_leave(int) {}
void smg_wprof_note_dlcall() {}

static uint32_t s_decodedFmt, s_decodedStride, s_decodedCount;
static uint8_t s_decoded[4096];

namespace gx {
uint32_t g_traceLastVat = 0;
double g_flushSeconds = 0;
uint32_t g_displayFrames = 0;
double g_presentWaited = 0, g_presentDrain = 0;
bool g_gxStats = false;
bool g_dlPromoteFirst = false, g_dlIgnoreExternal = false, g_dlNoHash = false;

bool rendererReady() { return true; }
void rendererStateWriterDomain(uint8_t) {}
void jpaMergeNoteBpReg(uint32_t) {}
bool rendererFastStateUsesTevRegs() { return false; }
void onStateChange(bool, uint8_t) {}
void flushBatch() {}
void executeCopy(uint32_t) {}
void markXfMemDirty() {}
void textureInvalidateAll() {}
void textureStreamWritten(const void*, uint32_t) {}
void textureStreamWrittenLinear(const void*, uint32_t, uint32_t) {}
void rendererGameDlCall() {}
void rendererGpuCatMark(uint8_t, uint32_t) {}
void jpaFinalVxBeginCheck(const SmsWiiuJpaFinalVxDesc*) {}
void jpaFinalVxSubmit(const SmsWiiuJpaFinalVxDesc*) {}
PrimClass primClass(uint8_t op) { return op >= 0xb8 ? PRIM_POINTS : op >= 0xa8 ? PRIM_LINES : PRIM_TRIS; }
uint8_t* primitiveBegin(uint8_t, uint32_t count, uint32_t fmt, uint32_t stride) {
    s_decodedFmt = fmt;
    s_decodedStride = stride;
    s_decodedCount = count;
    memset(s_decoded, 0, sizeof(s_decoded));
    return count * stride <= sizeof(s_decoded) ? s_decoded : nullptr;
}
void primitiveEnd(uint8_t, uint32_t) {}
uint32_t primitiveIndices(uint8_t, uint32_t, uint32_t, uint32_t*) { return 0; }
void noteDlImmediate(DlImmediateContext, uint32_t, uint32_t) {}
void appendDecoded(PrimClass, uint32_t, uint32_t, const uint8_t*, uint32_t, const uint32_t*, uint32_t) {}
void noteDlHitFallback(DlPerfFallback, uint32_t, uint32_t) {}
void noteDlMiss(uint32_t, uint32_t) {}
bool arenaEnabled() { return false; }
uint32_t arenaGeneration() { return 0; }
bool arenaUpload(const uint8_t*, uint32_t, uint32_t, const uint32_t*, uint32_t, ArenaRef&) { return false; }
void appendArena(PrimClass, uint32_t, uint32_t, const ArenaRef&, uint32_t, uint32_t, const uint8_t*) {}
bool batchUsesArena() { return false; }
void traceNoteBegin() {}
void traceNoteProjection(const float*) {}
void traceNoteDl() {}
FILE* traceFile() { return nullptr; }
void pixMetricClear() {}
uint32_t pixMetricRead() { return 0; }
bool colourPeekNeedsSync() { return false; }
uint32_t peekColor(int, int) { return 0; }
uint32_t peekZ(int, int) { return 0; }
}

namespace gx2prod {
void wake() {}
void wait(uint32_t) {}
bool reserve(uint32_t, uint8_t**, uint32_t*) { return false; }
bool commit(uint32_t) { return false; }
bool append(const uint8_t*, uint32_t) { return false; }
void set_flush_hook(void (*)()) {}
void drawown_worker_push(uint32_t) {}
void drawown_worker_pop() {}
uint32_t drawown_worker_current() { return 0; }
bool enqueueService(ServiceFn, void*) { return false; }
}

static constexpr uint8_t kBeginCheck = 1;
static constexpr uint8_t kSubmit = 2;
static constexpr uint8_t kCpVcdHi = 0x60;
static constexpr uint32_t kSentinel = 0xa5a5a5a5;
static uint8_t s_stream[4096];
static uint32_t s_bytes;

static void appendSentinel() {
    GXPC_Write8(0x08);
    GXPC_Write8(kCpVcdHi);
    GXPC_Write32(kSentinel);
}

static void emitCp(uint8_t reg, uint32_t value) {
    GXPC_Write8(0x08);
    GXPC_Write8(reg);
    GXPC_Write32(value);
}

static void emitProjectedRun(bool check) {
    memset(s_stream, 0, sizeof(s_stream));
    sms_gx_host_test_reset();
    GXPC_PipeBuf = s_stream;
    GXPC_PipeLen = 0;
    GXPC_PipeFast = 0;
    sms_gx_host_test_redirect(s_stream);
    sms_gx_host_test_record(check ? kBeginCheck : kSubmit, 0x04094100);

    if (check) {
        const uint32_t posF32 = 1u | (4u << 1) | (1u << 30);
        const uint32_t stqF32 = (2u << 21) | (4u << 23) | (1u << 29) | (1u << 30);
        emitCp(0x50, 1u << 9);  // CP_VCD_LO: direct position
        emitCp(0x60, gx::kHostVcdProjectedTex0 | 3u); // projected TEX0 host mode
        emitCp(0x70, posF32 | stqF32); // CP_VAT_A
        GXPC_Write8(0x80);
        GXPC_Write16(4);
        GXPosition3f32(0, 0, 0);
        GXTexCoord3f32(0, 0, 1);
        GXPosition3f32(1, 0, 0);
        GXTexCoord3f32(1, 0, 1);
        GXPosition3f32(1, 1, 0);
        GXTexCoord3f32(1, 1, 1);
        GXPosition3f32(0, 1, 0);
        GXTexCoord3f32(0, 1, 1);
    }

    sms_gx_host_test_record(kSubmit, 0x04094100);
    appendSentinel();
    s_bytes = sms_gx_host_test_redirect_len();
    sms_gx_host_test_redirect(nullptr);
}

static void emitLegacyRun(unsigned frac) {
    memset(s_stream, 0, sizeof(s_stream));
    sms_gx_host_test_reset();
    GXPC_PipeBuf = s_stream;
    GXPC_PipeLen = 0;
    GXPC_PipeFast = 0;
    sms_gx_host_test_redirect(s_stream);
    const uint32_t posF32 = 1u | (4u << 1) | (1u << 30);
    const uint32_t texF32 = (1u << 21) | (4u << 22) | (frac << 25) | (1u << 30);
    emitCp(0x50, 1u << 9);
    emitCp(0x60, 1u);
    emitCp(0x70, posF32 | texF32);
    GXPC_Write8(0x80);
    GXPC_Write16(1);
    GXPosition3f32(0, 0, 0);
    GXTexCoord2f32(0, 0);
    appendSentinel();
    s_bytes = sms_gx_host_test_redirect_len();
    sms_gx_host_test_redirect(nullptr);
}

// These are the stock GX VAT-A fields, independent of the STQ extension.
// In particular bit 29 belongs to the five-bit legacy TEX0 fraction.
static uint32_t legacyVat(unsigned count, unsigned type, unsigned frac) {
    return (1u | (4u << 1) | (1u << 30)) |
           (count << 21) | (type << 22) | (frac << 25);
}

static bool testLegacyVatMatrix() {
    const unsigned componentBytes[] = {1, 1, 2, 2, 4};
    for (unsigned count = 0; count != 2; ++count)
    for (unsigned type = 0; type != 5; ++type)
    for (unsigned frac : {0u, 15u, 16u, 31u}) {
        memset(s_stream, 0, sizeof(s_stream));
        sms_gx_host_test_reset();
        GXPC_PipeBuf = s_stream;
        GXPC_PipeLen = 0;
        GXPC_PipeFast = 0;
        sms_gx_host_test_redirect(s_stream);
        emitCp(0x50, 1u << 9);
        emitCp(0x60, 1u);
        emitCp(0x70, legacyVat(count, type, frac));
        GXPC_Write8(0x80);
        GXPC_Write16(1);
        GXPosition3f32(1, 2, 3);
        const unsigned n = componentBytes[type] * (count ? 2 : 1);
        for (unsigned i = 0; i < n; ++i) GXPC_Write8(0);
        appendSentinel();
        const uint32_t bytes = sms_gx_host_test_redirect_len();
        sms_gx_host_test_redirect(nullptr);
        const uint32_t used = sms_gx_host_test_parse(s_stream, bytes);
        const uint32_t expectedFmt = gx::VF_TEX0;
        const uint32_t expectedStride = gx::vtxFmtLayout(expectedFmt).stride;
        if (used != bytes || sms_gx_host_test_cp_vcd_hi() != kSentinel ||
            sms_gx_host_test_unknown_opcodes() || s_decodedFmt != expectedFmt ||
            s_decodedStride != expectedStride || s_decodedCount != 1) {
            std::printf("jpa-stream: FAIL legacy VAT count=%u type=%u frac=%u bytes=%u used=%u fmt=%x stride=%u unknown=%u\n",
                        count, type, frac, bytes, used, s_decodedFmt, s_decodedStride,
                        sms_gx_host_test_unknown_opcodes());
            return false;
        }
    }
    std::puts("jpa-stream: PASS legacy VAT matrix (S/ST, five types, fractions 0/15/16/31)");
    return true;
}

static bool testLegacyIndex16Vat() {
    // GX_INDEX16 is VCD mode 3, not a projected-TEX0 marker.  This legacy
    // S-format VAT shape is deliberately the one that shares the projected
    // count/type/fraction bits.
    memset(s_stream, 0, sizeof(s_stream));
    sms_gx_host_test_reset();
    GXPC_PipeBuf = s_stream;
    GXPC_PipeLen = 0;
    GXPC_PipeFast = 0;
    sms_gx_host_test_redirect(s_stream);
    emitCp(0x50, 1u << 9);
    emitCp(0x60, 3u); // GX_VA_TEX0 = GX_INDEX16
    emitCp(0x70, legacyVat(0, 1, 16));
    GXPC_Write8(0x80);
    GXPC_Write16(1);
    GXPosition3f32(1, 2, 3);
    GXPC_Write16(0x1234);
    appendSentinel();
    const uint32_t bytes = sms_gx_host_test_redirect_len();
    sms_gx_host_test_redirect(nullptr);
    const uint32_t used = sms_gx_host_test_parse(s_stream, bytes);
    const uint32_t expectedFmt = gx::VF_TEX0;
    if (used != bytes || sms_gx_host_test_cp_vcd_hi() != kSentinel ||
        sms_gx_host_test_unknown_opcodes() || s_decodedFmt != expectedFmt ||
        s_decodedStride != gx::vtxFmtLayout(expectedFmt).stride || s_decodedCount != 1) {
        std::printf("jpa-stream: FAIL legacy INDEX16 VAT bytes=%u used=%u fmt=%x stride=%u unknown=%u\n",
                    bytes, used, s_decodedFmt, s_decodedStride,
                    sms_gx_host_test_unknown_opcodes());
        return false;
    }
    std::puts("jpa-stream: PASS legacy INDEX16 VAT discriminator");
    return true;
}

static bool testLegacyVatBytes() {
    for (unsigned type = 0; type != 5; ++type)
    for (unsigned frac : {0u, 15u, 16u, 31u}) {
        memset(s_stream, 0, sizeof(s_stream));
        sms_gx_host_test_reset();
        GXBeginDisplayList(s_stream, sizeof(s_stream));
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST,
                        GXCompType(type), uint8_t(frac));
        const uint32_t bytes = GXEndDisplayList();
        const uint32_t expected = (1u << 30) | (1u << 21) | (type << 22) | (frac << 25);
        bool found = false;
        for (uint32_t at = 0; at + 6 <= bytes; ++at) {
            if (s_stream[at] == 0x08 && s_stream[at + 1] == 0x70) {
                uint32_t word = uint32_t(s_stream[at + 2]) << 24 |
                                uint32_t(s_stream[at + 3]) << 16 |
                                uint32_t(s_stream[at + 4]) << 8 | s_stream[at + 5];
                if (word != expected) {
                    std::printf("jpa-stream: FAIL legacy VAT bytes type=%u frac=%u got=%08x want=%08x\n",
                                type, frac, word, expected);
                    return false;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            std::printf("jpa-stream: FAIL legacy VAT bytes type=%u frac=%u missing CP_VAT_A\n",
                        type, frac);
            return false;
        }
    }
    std::puts("jpa-stream: PASS marker-off legacy VAT bytes (exact CP_VAT_A identity)");
    return true;
}

static bool testParticleRuns() {
    // Reproduce the two particle paths that can alternate within an emitter:
    // direct XYZ/colour/STQ and indexed texture coordinates with two colours.
    for (unsigned colors = 0; colors != 3; ++colors)
    for (unsigned indexed = 0; indexed != 2; ++indexed)
    for (unsigned child = 0; child != 2; ++child) {
        memset(s_stream, 0, sizeof(s_stream));
        sms_gx_host_test_reset();
        static const uint8_t texArray[8] = {0, 0, 1, 0, 1, 1, 0, 1};
        gx::g.arrayBase[4] = texArray;
        gx::g.arrayStride[4] = 2;
        gx::g.arrayBigEndian[4] = true;
        GXPC_PipeBuf = s_stream;
        GXPC_PipeLen = 0;
        GXPC_PipeFast = 0;
        sms_gx_host_test_redirect(s_stream);
        const uint32_t lo = (1u << 9) | (colors ? 1u << 13 : 0) |
                            (colors == 2 ? 1u << 15 : 0);
        emitCp(0x50, lo);
        emitCp(0x60, indexed ? 2u : (gx::kHostVcdProjectedTex0 | 3u));
        const uint32_t colorVat = (colors ? (1u << 13) | (5u << 14) : 0) |
                                  (colors == 2 ? (1u << 17) | (5u << 18) : 0);
        const uint32_t texVat = indexed ? (1u << 21) | (1u << 22) :
                                            (2u << 21) | (4u << 23) | (1u << 29);
        emitCp(0x70, (1u | (4u << 1) | (1u << 30)) | colorVat | texVat);
        GXPC_Write8(child ? 0x88 : 0x80);
        GXPC_Write16(4);
        for (unsigned v = 0; v != 4; ++v) {
            GXPosition3f32(float(v), 2, 3);
            if (colors) GXColor4u8(0x11, 0x22, 0x33, 0x44);
            if (colors == 2) GXColor4u8(0xaa, 0xbb, 0xcc, 0xdd);
            if (indexed) GXTexCoord1x8(uint8_t(v));
            else GXTexCoord3f32(float(v), 0.5f, 2);
        }
        appendSentinel();
        const uint32_t bytes = sms_gx_host_test_redirect_len();
        sms_gx_host_test_redirect(nullptr);
        const uint32_t used = sms_gx_host_test_parse(s_stream, bytes);
        const uint32_t expectedFmt = (indexed ? gx::VF_TEX0 : gx::VF_TEX0_STQ) |
                                     (colors ? gx::VF_CLR0 : 0) |
                                     (colors == 2 ? gx::VF_CLR0 << 1 : 0);
        if (used != bytes || sms_gx_host_test_cp_vcd_hi() != kSentinel ||
            sms_gx_host_test_unknown_opcodes() || s_decodedFmt != expectedFmt ||
            s_decodedStride != gx::vtxFmtLayout(expectedFmt).stride || s_decodedCount != 4) {
            std::printf("jpa-stream: FAIL particle indexed=%u colors=%u child=%u bytes=%u used=%u fmt=%x unknown=%u\n",
                        indexed, colors, child, bytes, used, s_decodedFmt,
                        sms_gx_host_test_unknown_opcodes());
            return false;
        }
    }
    std::puts("jpa-stream: PASS projected/indexed colour and child particle runs");
    return true;
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "legacy") == 0)
        return testLegacyVatBytes() && testLegacyVatMatrix() && testLegacyIndex16Vat() ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "particle") == 0) return testParticleRuns() ? 0 : 1;
    if (!testLegacyVatBytes() || !testLegacyVatMatrix() || !testLegacyIndex16Vat() || !testParticleRuns()) return 1;
    for (int check = 0; check != 2; ++check) {
        emitProjectedRun(check != 0);
        const uint32_t used = sms_gx_host_test_parse(s_stream, s_bytes);
        if (used != s_bytes || sms_gx_host_test_cp_vcd_hi() != kSentinel ||
            sms_gx_host_test_unknown_opcodes() != 0) {
            std::printf("jpa-stream: FAIL (check=%d bytes=%u used=%u cp-vcd-hi=%08x unknown=%u)\n", check,
                        unsigned(s_bytes), unsigned(used), unsigned(sms_gx_host_test_cp_vcd_hi()),
                        unsigned(sms_gx_host_test_unknown_opcodes()));
            return 1;
        }
    }
    for (unsigned frac : {16u, 31u}) {
        emitLegacyRun(frac);
        const uint32_t used = sms_gx_host_test_parse(s_stream, s_bytes);
        if (used != s_bytes || sms_gx_host_test_cp_vcd_hi() != kSentinel ||
            sms_gx_host_test_unknown_opcodes() != 0) {
            std::printf("jpa-stream: FAIL (legacy-frac=%u bytes=%u used=%u cp-vcd-hi=%08x unknown=%u)\n",
                        frac, unsigned(s_bytes), unsigned(used), unsigned(sms_gx_host_test_cp_vcd_hi()),
                        unsigned(sms_gx_host_test_unknown_opcodes()));
            return 1;
        }
    }
    std::puts("jpa-stream: PASS (submit-only and begin-check/submit producer records)");
    return 0;
}
