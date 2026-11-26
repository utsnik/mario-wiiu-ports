#ifndef SMG_WIIU_JPA_ASYNC_H
#define SMG_WIIU_JPA_ASYNC_H

// Math and publication helpers remain usable by host contract tests. Only
// target builds expose jobs, which borrow the live particle graph until join.

#include <stdint.h>
#include <math.h>

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG) && defined(SMG_WIIU_JPA_ASYNC_JOB_TYPES)
#include "../../decomp/libs/JSystem/include/JSystem/JParticle/JPAEmitter.hpp"
#endif

struct SmsWiiuJpaFinalVxDesc;

namespace smg_jpa_async {

struct Vec3 {
    float x, y, z;
};

struct Matrix34 {
    float m[3][4];
};

struct Color {
    uint8_t r, g, b, a;
};

struct BillboardMode {
    uint8_t prmRgb;
    uint8_t envRgb;
    uint8_t alpha;
    uint8_t envAlpha;
    uint8_t alphaAnimated;
    uint8_t alphaFromParticlePrm;
    uint8_t rasterColorPrm;
    uint8_t rasterColorEnv;
    uint8_t rasterColorAlphaPrm;
    uint8_t rasterColorAlphaEnv;
    uint8_t rasterAlphaPrm;
    uint8_t rasterAlphaEnv;
    uint8_t twoColor;
    uint8_t textureAnimated;
};

enum RunKind {
    RunBillboard = 0,
    RunDirectional = 1,
};

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG) && defined(SMG_WIIU_JPA_ASYNC_JOB_TYPES)
struct Job;
typedef void (*FillCallback)(Job*);

// Enqueue copies this fixed-size job, including work, into the ring. first is
// inclusive and end is exclusive, following prev links when reverse is true.
// No particles are copied. The caller must keep particles, list links, emitter,
// resources and template/texture arrays alive and stable until join returns;
// descriptor and its output buffers must also survive their renderer use.
// The callback runs on core 2 and may update its private work and output buffers.
// It must not allocate, issue graphics calls, or mutate the borrowed inputs.
struct Job {
    JPAEmitterWorkData work;
    JPANode<JPABaseParticle>* first;
    JPANode<JPABaseParticle>* end;
    SmsWiiuJpaFinalVxDesc* descriptor;
    FillCallback fill;
    const signed char* templateXYZ;
    const void* texArray;
    uint32_t runKind;
    uint32_t shapeType;
    uint32_t particleCount;
    uint32_t texFraction;
    uint32_t colorCount;
    BillboardMode billboard;
    bool reverse;
    bool rotated;
    bool nativeQuads;
    bool projected;
};
#endif

struct ReadyProtocol {
    volatile uint32_t ready;
    volatile uint32_t payloadReads;
};

// Host-side accounting model; target callbacks use the atomic-safe interfaces
// in jpa_counters.h instead of sharing this model across cores.
struct Counters {
    uint32_t jobs;
    uint32_t particles;
    uint32_t helperBusyUs;
    uint32_t workerWaitUs;

    Counters() : jobs(0), particles(0), helperBusyUs(0), workerWaitUs(0) {}
    void add(uint32_t jobCount, uint32_t particleCount, uint32_t busyUs, uint32_t waitUs) {
        jobs += jobCount;
        particles += particleCount;
        helperBusyUs += busyUs;
        workerWaitUs += waitUs;
    }
    void reset() { jobs = particles = helperBusyUs = workerWaitUs = 0; }
};

inline uint8_t colorMulti(uint8_t a, uint8_t b) {
    return static_cast<uint8_t>(((uint32_t(a) * (uint32_t(b) + 1u)) * 0x10000u) >> 24);
}

inline Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 scale(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float lengthSquared(const Vec3& a) { return dot(a, a); }
inline bool nearZero(const Vec3& a) { return lengthSquared(a) < 0.00000001f; }
inline Vec3 normalized(const Vec3& a) {
    const float len = sqrtf(lengthSquared(a));
    return len == 0.0f ? Vec3{0.0f, 0.0f, 0.0f} : scale(a, 1.0f / len);
}

inline Vec3 transform(const Matrix34& matrix, const Vec3& value) {
    return {
        matrix.m[0][0] * value.x + matrix.m[0][1] * value.y + matrix.m[0][2] * value.z + matrix.m[0][3],
        matrix.m[1][0] * value.x + matrix.m[1][1] * value.y + matrix.m[1][2] * value.z + matrix.m[1][3],
        matrix.m[2][0] * value.x + matrix.m[2][1] * value.y + matrix.m[2][2] * value.z + matrix.m[2][3],
    };
}

inline void publishReady(ReadyProtocol& protocol) {
    __atomic_store_n(&protocol.ready, 1u, __ATOMIC_RELEASE);
}

inline bool workerCanRead(ReadyProtocol& protocol) {
    if (__atomic_load_n(&protocol.ready, __ATOMIC_ACQUIRE) != 1) return false;
    __atomic_fetch_add(&protocol.payloadReads, 1u, __ATOMIC_RELAXED);
    return true;
}

} // namespace smg_jpa_async

extern "C" {
int sms_wiiu_jpa_async_enabled(void);
bool sms_wiiu_jpa_async_start(void);
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG) && defined(SMG_WIIU_JPA_ASYNC_JOB_TYPES)
bool sms_wiiu_jpa_async_enqueue(const smg_jpa_async::Job&);
#endif
// Waits for jobs published before this call, acquiring even completed jobs.
// Prevent concurrent submissions before using join as an input lifetime boundary.
void sms_wiiu_jpa_async_join(void);
bool sms_wiiu_jpa_async_wait_ready(const volatile uint32_t*);
}

#endif
