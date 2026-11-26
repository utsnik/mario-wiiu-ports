#include "platform/wiiu/depth_peek.h"

#include <cassert>
#include <limits>

int main() {
    assert(gx::depthToPeekZ(0.0f) == 0u);
    assert(gx::depthToPeekZ(0.5f) == 0x7fffffu);
    assert(gx::depthToPeekZ(1.0f) == 0xffffffu);
    assert(gx::depthToPeekZ(std::numeric_limits<float>::quiet_NaN()) == 0xffffffu);
    assert(gx::depthToPeekZ(-1.0f) == 0u);

    const gx::DepthPeekAnswer noSample = gx::resolveDepthPeekNonBlocking(
        12, {false, 0, 0});
    assert(noSample.z == 0xffffffu);
    assert(noSample.age == gx::kPeekZNoSampleAge);
    assert(noSample.rawBits == 0u);

    const gx::DepthPeekSample skySample = {true, 10, 0x0000803fu};
    const gx::DepthPeekAnswer skyAnswer = gx::resolveDepthPeekNonBlocking(12, skySample);
    assert(skyAnswer.z == 0xffffffu);
    assert(skyAnswer.age == 2u);
    assert(skyAnswer.rawBits == skySample.rawBits);

    const gx::DepthPeekSample groundSample = {true, 10, 0x22ad7e3fu};
    const gx::DepthPeekAnswer groundAnswer = gx::resolveDepthPeekNonBlocking(12, groundSample);
    assert(groundAnswer.z == 16690465u);
    assert(groundAnswer.age == 2u);
    assert(groundAnswer.rawBits == groundSample.rawBits);
    return 0;
}
