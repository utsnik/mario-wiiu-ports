#ifndef SMS_WIIU_GOOPWB_ENCODER_H
#define SMS_WIIU_GOOPWB_ENCODER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace sms_wiiu {

static inline uint32_t goopwb_i8_tile_bytes(uint32_t width, uint32_t height)
{
	return ((width + 7) / 8) * ((height + 3) / 4) * 32;
}

/* Encode the R component of an RGBA8 linear surface into GX I8 tiles.  The
 * integer ratios intentionally match the copy shader's nearest texel mapping.
 * Clearing the complete output first also clears padding in partial tiles. */
/* Rows [row_begin, row_end) of the logical image only (goopcap 2026-11-13 spreads one layer's encode over
 * several frames). The output is cleared when row_begin is 0, so encoding every row range in order once is
 * byte-identical to the whole-image encode. */
static inline void goopwb_encode_i8_tiles_rows(const uint8_t* rgba, uint32_t pitch,
                                               uint32_t logical_width,
                                               uint32_t logical_height,
                                               uint32_t storage_width,
                                               uint32_t storage_height,
                                               uint32_t sample_x, uint32_t sample_y,
                                               uint32_t sample_width, uint32_t sample_height,
                                               uint8_t* out, uint32_t row_begin, uint32_t row_end)
{
	const uint32_t bytes = goopwb_i8_tile_bytes(logical_width, logical_height);
	if (!out)
		return;
	if (row_begin == 0)
		memset(out, 0, bytes);
	if (row_end > logical_height)
		row_end = logical_height;
	if (!rgba || !pitch || !logical_width || !logical_height ||
	    !storage_width || !storage_height)
		return;

	/* 2026-11-13: the per-pixel 64-bit divisions were library calls on Espresso (~7 ms a commit on the game
	 * thread in Delfino Plaza). The mapping is separable, so build the column table once and walk rows;
	 * the products fit 32 bits (both factors are surface sizes < 65536). Output is byte-identical. */
	const uint32_t tile_columns = (logical_width + 7) / 8;
	static uint32_t column_offset[4096];
	if (logical_width > 4096)
		return;
	for (uint32_t x = 0; x < logical_width; ++x) {
		uint32_t source_x = sample_x + (x * sample_width) / logical_width;
		if (source_x >= storage_width)
			source_x = storage_width - 1;
		column_offset[x] = source_x * 4;
	}
	for (uint32_t y = row_begin; y < row_end; ++y) {
		uint32_t source_y = sample_y + (y * sample_height) / logical_height;
		if (source_y >= storage_height)
			source_y = storage_height - 1;
		const uint8_t* row = rgba + size_t(source_y) * pitch * 4;
		uint8_t* tile_row = out + (y / 4) * tile_columns * 32 + (y % 4) * 8;
		for (uint32_t x = 0; x < logical_width; ++x)
			tile_row[(x / 8) * 32 + (x % 8)] = row[column_offset[x]];
	}
}

static inline void goopwb_encode_i8_tiles_rect(const uint8_t* rgba, uint32_t pitch,
                                               uint32_t logical_width,
                                               uint32_t logical_height,
                                               uint32_t storage_width,
                                               uint32_t storage_height,
                                               uint32_t sample_x, uint32_t sample_y,
                                               uint32_t sample_width, uint32_t sample_height,
                                               uint8_t* out)
{
	goopwb_encode_i8_tiles_rows(rgba, pitch, logical_width, logical_height, storage_width, storage_height,
	                            sample_x, sample_y, sample_width, sample_height, out, 0, logical_height);
}

static inline void goopwb_encode_i8_tiles(const uint8_t* rgba, uint32_t pitch,
                                          uint32_t logical_width,
                                          uint32_t logical_height,
                                          uint32_t storage_width,
                                          uint32_t storage_height,
                                          uint8_t* out)
{
	goopwb_encode_i8_tiles_rect(rgba, pitch, logical_width, logical_height,
	                           storage_width, storage_height, 0, 0,
	                           storage_width, storage_height, out);
}

} // namespace sms_wiiu

#endif
