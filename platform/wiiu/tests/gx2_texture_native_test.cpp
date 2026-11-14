#ifndef PORT_GAME_SMG
#error "This test must exercise the Galaxy fast path"
#endif

#include "../gx2_texture.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

uint32_t nextRandom(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

void oldConvert(const uint8_t* src, uint32_t fmt, uint32_t w, uint32_t h, uint8_t* out) {
    const uint32_t tw = (fmt == 0 || fmt == 1) ? 8 : (fmt == 2 ? 8 : 4);
    const uint32_t th = (fmt == 0) ? 8 : 4;
    const uint32_t tileBytes = (fmt == 0 || fmt == 1 || fmt == 2) ? 32 : 32;
    const uint32_t outBytes = (fmt == 0 || fmt == 1) ? 1 : 2;
    const uint32_t cols = (w + tw - 1) / tw, rows = (h + th - 1) / th;
    const uint8_t* p = src;
    for (uint32_t ty = 0; ty < rows; ++ty) {
        for (uint32_t tx = 0; tx < cols; ++tx) {
            const uint32_t x0 = tx * tw, y0 = ty * th;
            const uint32_t count = tw * th;
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t x = x0 + (i % tw), y = y0 + (i / tw);
                if (x >= w || y >= h) continue;
                uint8_t* d = out + (size_t(y) * w + x) * outBytes;
                switch (fmt) {
                case 0: d[0] = uint8_t(((p[i >> 1] >> ((i & 1) ? 0 : 4)) & 15) * 17); break;
                case 1: d[0] = p[i]; break;
                case 2: d[0] = uint8_t((p[i] & 15) * 17); d[1] = uint8_t((p[i] >> 4) * 17); break;
                case 3: d[0] = p[2 * i + 1]; d[1] = p[2 * i]; break;
                case 4: d[0] = p[2 * i + 1]; d[1] = p[2 * i]; break;
                default: break;
                }
            }
            p += tileBytes;
        }
    }
}

uint32_t sourceBytes(uint32_t fmt, uint32_t w, uint32_t h) {
    const uint32_t tw = (fmt == 0 || fmt == 1 || fmt == 2) ? 8 : 4;
    const uint32_t th = (fmt == 0) ? 8 : 4;
    return ((w + tw - 1) / tw) * ((h + th - 1) / th) * 32;
}

uint32_t outputBytes(uint32_t fmt, uint32_t w, uint32_t h) {
    return w * h * ((fmt == 0 || fmt == 1) ? 1 : 2);
}

} // namespace

int main() {
    const uint32_t sizes[][2] = {{640, 368}, {320, 184}, {13, 7}, {9, 5}, {1, 1}, {8, 4}};
    uint32_t state = 0x5eeda11u;
    for (uint32_t fmt = 0; fmt <= 4; ++fmt) {
        for (const auto& size : sizes) {
            const uint32_t w = size[0], h = size[1];
            std::vector<uint8_t> src(sourceBytes(fmt, w, h));
            std::vector<uint8_t> expected(outputBytes(fmt, w, h));
            std::vector<uint8_t> actual(expected.size());
            for (uint8_t& byte : src) byte = uint8_t(nextRandom(state));
            oldConvert(src.data(), fmt, w, h, expected.data());
            gx::gx2ConvertTextureNative(src.data(), fmt, w, h, actual.data());
            if (std::memcmp(expected.data(), actual.data(), actual.size()) != 0) {
                std::cerr << "gx2_texture_native_test: mismatch format " << fmt
                          << " size " << w << 'x' << h << '\n';
                return 1;
            }
        }
    }
    std::cout << "gx2_texture_native_test: PASS (formats 0-4, 6 sizes, byte-identical)\n";
    return 0;
}
