#include "../dsp_mixer.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t voice[0x180], fx[4][0x20], pcm[80];
static int16_t delay[4][80], left[80], right[80];
static uint32_t resampler[256];

template <class T> static void put(uint8_t* p, int offset, T value)
{
	memcpy(p + offset, &value, sizeof value);
}

static const uint8_t* aram(uint32_t addr) { return addr < sizeof pcm ? pcm + addr : NULL; }
static int16_t* mram(uint32_t addr, uint32_t samples)
{
	return addr >= 1 && addr <= 4 && samples <= 80 ? delay[addr - 1] : NULL;
}

static void reset()
{
	memset(voice, 0, sizeof voice);
	memset(fx, 0, sizeof fx);
	memset(delay, 0, sizeof delay);
	memset(pcm, 64, sizeof pcm);
	for (int i = 0; i < 64; i++) {
		resampler[i * 2] = 32767;
		resampler[i * 2 + 1] = 0;
	}
	port_dspmix_setup(1, voice, resampler, NULL, fx);
	port_dspmix_set_aram(aram);
	port_dspmix_set_mram(mram);
}

static int near(int got, int want) { return got >= want - 2 && got <= want + 2; }

int main()
{
	const int sample = 64 * 256;
	const int volume = 0x2B03;
	const int master = 0x3999;
	const int dry = (sample * volume) / 65536;
	const int output = (dry * master) / 4096;
	const int wet = (dry * 0x4000) / 32768;

	reset();
	put<uint16_t>(voice, 0x00, 1);
	put<uint16_t>(voice, 0x04, 0x1000);
	put<uint16_t>(voice, 0x08, 1);
	put<uint16_t>(voice, 0x52, 0x4000);
	put<uint16_t>(voice, 0x54, volume);
	put<uint16_t>(voice, 0x56, volume);
	put<uint16_t>(voice, 0x58, 1);
	put<uint16_t>(voice, 0x100, 8);
	put<uint32_t>(voice, 0x114, sizeof pcm);
	port_dspmix_render(left, right, 80, master);
	assert(near(left[0], output) && "SMG auto-mixer hard-left output uses Q16 volume and 4.12 master");

	reset();
	put<uint16_t>(voice, 0x00, 1);
	put<uint16_t>(voice, 0x04, 0x1000);
	put<uint16_t>(voice, 0x08, 1);
	put<uint16_t>(voice, 0x10, 0x0D00);
	put<uint16_t>(voice, 0x12, volume);
	put<uint16_t>(voice, 0x14, volume);
	put<uint16_t>(voice, 0x100, 8);
	put<uint32_t>(voice, 0x114, sizeof pcm);
	port_dspmix_render(left, right, 80, master);
	assert(near(left[0], output) && "SMG explicit send uses Q16 volume and 4.12 master");

	reset();
	put<uint16_t>(fx[2], 0x00, 1);
	put<uint16_t>(fx[2], 0x02, 1);
	put<uint32_t>(fx[2], 0x04, 3);
	put<uint16_t>(voice, 0x00, 1);
	put<uint16_t>(voice, 0x04, 0x1000);
	put<uint16_t>(voice, 0x08, 1);
	put<uint16_t>(voice, 0x52, 0x4000);
	put<uint16_t>(voice, 0x54, volume);
	put<uint16_t>(voice, 0x56, volume);
	put<uint16_t>(voice, 0x58, 1);
	put<uint16_t>(voice, 0x100, 8);
	put<uint32_t>(voice, 0x114, sizeof pcm);
	port_dspmix_render(left, right, 80, master);
	assert(near(delay[2][0], wet) && "SMG auto-mixer wet send uses Q15 effect scale");
	puts("PASS: SMG Q16 auto/explicit levels, Q15 wet send, and 4.12 master");
}
