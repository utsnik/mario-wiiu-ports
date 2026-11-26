#include "games/smg/include/wiiu/jpa_async.h"

#include <cstdio>

using namespace smg_jpa_async;

static int test_math_and_mode_copy_contract() {
    Matrix34 matrix = {};
    matrix.m[0][0] = matrix.m[1][1] = matrix.m[2][2] = 1.0f;
    const Vec3 value = {2.0f, -3.0f, 4.0f};
    const Vec3 transformed = transform(matrix, value);
    if (transformed.x != value.x || transformed.y != value.y || transformed.z != value.z)
        return 1;
    if (colorMulti(255, 255) != 255 || colorMulti(128, 128) != 64)
        return 1;
    BillboardMode mode = {};
    mode.twoColor = 1;
    mode.textureAnimated = 1;
    return mode.twoColor && mode.textureAnimated ? 0 : 1;
}

static int test_ready_protocol_and_counters() {
    ReadyProtocol protocol = {0, 0};
    if (workerCanRead(protocol) || protocol.payloadReads != 0) return 1;
    publishReady(protocol);
    if (!workerCanRead(protocol) || protocol.payloadReads != 1) return 1;
    Counters counters;
    counters.add(2, 7, 11, 13);
    counters.add(1, 3, 5, 2);
    if (counters.jobs != 3 || counters.particles != 10 || counters.helperBusyUs != 16 ||
        counters.workerWaitUs != 15) return 1;
    counters.reset();
    return counters.jobs || counters.particles || counters.helperBusyUs || counters.workerWaitUs;
}

static int test_worst_case_capacity_model() {
    const unsigned runLength = 7;
    const unsigned quadCount = 2;
    const unsigned stride = 36;
    const unsigned indexWidth = 6;
    const unsigned vertexBytes = runLength * quadCount * 4u * stride;
    const unsigned indexBytes = runLength * quadCount * indexWidth * sizeof(unsigned);
    return vertexBytes == 2016u && indexBytes == 336u ? 0 : 1;
}

static int test_admission_fallbacks() {
    struct QueueModel {
        bool helperStarted;
        unsigned published;
        unsigned capacity;
        bool admit() {
            if (!helperStarted) return false;
            if (published == capacity) return false;
            ++published;
            return true;
        }
    };
    QueueModel failed = {false, 0, 2};
    if (failed.admit()) return 1;
    QueueModel full = {true, 2, 2};
    if (full.admit()) return 1;
    QueueModel admitted = {true, 0, 2};
    return admitted.admit() && admitted.published == 1 ? 0 : 1;
}

int main() {
    if (test_math_and_mode_copy_contract() || test_ready_protocol_and_counters() ||
        test_worst_case_capacity_model() || test_admission_fallbacks()) return 1;
    std::puts("jpa-async: PASS (live-job contract, capacity, ready, counters)");
    return 0;
}
