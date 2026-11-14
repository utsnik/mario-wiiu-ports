#ifndef SMS_PLATFORM_GX_J3D_STATE_DELTA_H
#define SMS_PLATFORM_GX_J3D_STATE_DELTA_H

#include <stdint.h>

enum { J3D_STATE_DELTA_REPORT_COUNT = 14 };

enum J3DStateDeltaDomain : uint8_t {
    J3D_STATE_DELTA_NONE = 0,
    J3D_STATE_DELTA_CP = 1,
    J3D_STATE_DELTA_BP = 2,
    J3D_STATE_DELTA_XF = 4,
};

enum J3DDirtyDomain : uint32_t {
    J3D_DIRTY_TEV_TEXTURE = 1u << 0,
    J3D_DIRTY_PE_RASTER = 1u << 1,
    J3D_DIRTY_VERTEX_KEY_XF_REG = 1u << 2,
    J3D_DIRTY_XF_MEMORY_UNIFORM = 1u << 3,
    J3D_DIRTY_CP_LAYOUT_ARRAY = 1u << 4,
};

enum J3DStateDeltaOpType : uint8_t {
    J3D_STATE_DELTA_CP_WRITE = 0x08,
    J3D_STATE_DELTA_BP_WRITE = 0x61,
    J3D_STATE_DELTA_XF_WRITE = 0x10,
    J3D_STATE_DELTA_XF_INDEXED = 0x20,
    J3D_STATE_DELTA_CALL = 0x40,
};

enum J3DStateDeltaFlags : uint8_t {
    J3D_STATE_DELTA_FLAG_SAFE_SKIP = 1u << 0,
    J3D_STATE_DELTA_FLAG_SIDE_EFFECT = 1u << 1,
};

struct J3DStateDeltaOp {
    uint8_t domain;
    uint8_t type;
    uint8_t flags;
    uint8_t reg;
    uint16_t address;
    uint16_t count;
    uint32_t value;
};

static inline bool j3dStateDeltaCanSkip(uint8_t type, uint8_t reg, uint32_t value) {
    if (type == J3D_STATE_DELTA_CP_WRITE)
        return reg != 0xA0 && reg != 0xB0;
    if (type == J3D_STATE_DELTA_BP_WRITE) {
        const uint8_t bp = uint8_t(value >> 24);
        return bp != 0xFE && bp != 0x45 && bp != 0x48 && bp != 0x4A && bp != 0x4B &&
               bp != 0x47 && bp != 0x52 && bp != 0x61 &&  // 0x47 PE token: an event, not state
               bp != 0x65 && bp != 0x66 && bp != 0x67;     // 0x67 metrics clear
    }
    return type == J3D_STATE_DELTA_XF_WRITE;
}

static inline uint8_t j3dStateDeltaMarkDomain(uint8_t domain, bool differs) {
    return differs ? domain : uint8_t(J3D_STATE_DELTA_NONE);
}

static inline uint32_t j3dStateDeltaDirtyForBP(uint8_t reg) {
    if (reg == 0x00 || reg == 0x27 || reg == 0xF6 || (reg >= 0x28 && reg <= 0x3F) ||
        (reg >= 0x80 && reg <= 0x9B) || (reg >= 0xC0 && reg <= 0xE7))
        return J3D_DIRTY_TEV_TEXTURE;
    if (reg == 0x20 || reg == 0x21 || (reg >= 0x40 && reg <= 0x51) || reg == 0x59)
        return J3D_DIRTY_PE_RASTER;
    return J3D_DIRTY_PE_RASTER;
}

static inline uint32_t j3dStateDeltaDirtyForCP(uint8_t reg) {
    return reg >= 0xA0 ? J3D_DIRTY_CP_LAYOUT_ARRAY : J3D_DIRTY_VERTEX_KEY_XF_REG;
}

static inline uint32_t j3dStateDeltaDirtyForXF(uint16_t address, uint32_t count) {
    (void)count;
    return address >= 0x1000 ? J3D_DIRTY_XF_MEMORY_UNIFORM : J3D_DIRTY_VERTEX_KEY_XF_REG;
}

#endif
