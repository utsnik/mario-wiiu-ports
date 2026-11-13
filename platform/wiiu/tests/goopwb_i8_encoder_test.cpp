#include "../goopwb_encoder.h"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>

int main()
{
	const uint32_t logical_width = 10, logical_height = 5;
	const uint32_t storage_width = 20, storage_height = 10, pitch = 23;
	std::vector<uint8_t> rgba(size_t(pitch) * storage_height * 4, 0xa5);
	for (uint32_t y = 0; y < storage_height; ++y)
		for (uint32_t x = 0; x < storage_width; ++x)
			rgba[(size_t(y) * pitch + x) * 4] = uint8_t(y * storage_width + x);

	const uint32_t bytes = sms_wiiu::goopwb_i8_tile_bytes(logical_width, logical_height);
	if (bytes != 128) {
		std::fprintf(stderr, "unexpected I8 byte count: %u\n", unsigned(bytes));
		return 1;
	}
	std::vector<uint8_t> encoded(bytes, 0xcd);
	sms_wiiu::goopwb_encode_i8_tiles(rgba.data(), pitch, logical_width, logical_height,
	                                storage_width, storage_height, encoded.data());

	for (uint32_t y = 0; y < logical_height; ++y) {
		for (uint32_t x = 0; x < logical_width; ++x) {
			const uint32_t tile = (y / 4) * 2 + x / 8;
			const uint32_t offset = (y % 4) * 8 + x % 8;
			const uint8_t expected = uint8_t((y * 2) * storage_width + x * 2);
			if (encoded[tile * 32 + offset] != expected) {
				std::fprintf(stderr, "texel mismatch at %u,%u: got %u expected %u\n",
				             unsigned(x), unsigned(y), unsigned(encoded[tile * 32 + offset]),
				             unsigned(expected));
				return 1;
			}
		}
	}
	for (uint32_t tile = 0; tile < 4; ++tile) {
		for (uint32_t offset = 0; offset < 32; ++offset) {
			const uint32_t x = (tile % 2) * 8 + offset % 8;
			const uint32_t y = (tile / 2) * 4 + offset / 8;
			if ((x >= logical_width || y >= logical_height) && encoded[tile * 32 + offset] != 0) {
				std::fprintf(stderr, "non-zero edge padding at tile %u offset %u\n",
				             unsigned(tile), unsigned(offset));
				return 1;
			}
		}
	}
	std::puts("goopwb I8 encoder: pass");
	// goopcap: encoding row ranges in order (any chunk size) equals the whole-image encode.
	{
		const uint32_t cases[][8] = {{512, 256, 1152, 576, 0, 0, 1152, 576}, {256, 256, 576, 576, 0, 0, 576, 576},
		                             {100, 37, 230, 90, 3, 5, 220, 80}, {8, 4, 8, 4, 0, 0, 8, 4}};
		for (const auto& c : cases) {
			const uint32_t lw = c[0], lh = c[1], sw = c[2], sh = c[3];
			const uint32_t cpitch = sw + 16;
			std::vector<uint8_t> src(size_t(cpitch) * sh * 4);
			for (size_t k = 0; k < src.size(); ++k)
				src[k] = uint8_t((k * 2654435761u) >> 13);
			const uint32_t bytes = sms_wiiu::goopwb_i8_tile_bytes(lw, lh);
			std::vector<uint8_t> whole(bytes, 0xAA), part(bytes, 0x55);
			sms_wiiu::goopwb_encode_i8_tiles_rect(src.data(), cpitch, lw, lh, sw, sh, c[4], c[5], c[6], c[7], whole.data());
			for (uint32_t chunk : {1u, 3u, 4u, 64u, 1000u}) {
				std::fill(part.begin(), part.end(), 0x55);
				for (uint32_t y = 0; y < lh; y += chunk)
					sms_wiiu::goopwb_encode_i8_tiles_rows(src.data(), cpitch, lw, lh, sw, sh, c[4], c[5], c[6], c[7],
					                                      part.data(), y, y + chunk);
				if (part != whole) {
					std::printf("chunked encode differs: %ux%u chunk=%u\n", lw, lh, chunk);
					return 1;
				}
			}
		}
		std::printf("chunked encode matches whole encode\n");
	}
	return 0;
}
