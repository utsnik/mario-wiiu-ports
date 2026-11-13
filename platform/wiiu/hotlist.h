/* SD marker "hotlist": per-display-list summaries for the [gpuframe] top
 * positions.  The worker fills one HotListInfo per recorded list (a ring read
 * in order by core 1), core 1 stores a copy next to the list's GPU stamp, and
 * the frame-end report prints one [hotlist] line per top position.  Shared by
 * gx2_renderer.cpp and gx_stub.cpp; nothing here is used without the marker.
 * Callers are captured on the game thread at GXBegin/GXCallDisplayList and
 * carried in the producer stream before the worker fills these summaries. */
#pragma once
#include <stdint.h>

struct HotTex {
    uint32_t src;       /* TextureEntry::source (GX texture bytes), 0 for a copy */
    uint32_t img;       /* GX2 surface image address (matches "[efb] copy entry ... img=") */
    uint16_t w, h;
    uint8_t fmt, map;
    uint8_t pad[2];
};

struct HotIdentity {
    uint32_t model;
    uint32_t modelData;
    char texName[2][16]; /* 2 x 16 bytes = the 8 stream words */
};

struct HotDraw {
    uint32_t nidx;      /* index count of this draw */
    uint32_t cm0;       /* BP_CMODE0 */
    uint16_t stages;    /* TEV stage count */
    uint8_t texMask;
    uint8_t zmode;      /* BP_ZMODE & 0xff: bit0 compare, bits1-3 func, bit4 update */
    uint16_t vpw, vph;  /* GX viewport width and height */
    HotTex tex[2];      /* the first two sampled maps */
    uint32_t caller[8]; /* first return addresses at the draw entry; host filters them */
    HotIdentity identity;
};

struct HotListInfo {
    uint32_t ord;       /* list ordinal in the frame (worker order, 0 based) */
    uint32_t list;      /* display list address: the ring's integrity check */
    uint32_t draws;     /* draws recorded into the list */
    uint32_t skipped;   /* draws left out by the hotskip marker */
    uint32_t totalIdx;
    uint32_t maxIdx;
    uint32_t aux[3];    /* copy stamps: destination image, width, height */
    uint8_t valid;      /* 1 a list summary, 2 a copy tag */
    uint8_t pass;      /* 1 bindshadow, 2 lightdb, 0 ordinary list */
    uint8_t pad[2];
    HotDraw first, big;
};
