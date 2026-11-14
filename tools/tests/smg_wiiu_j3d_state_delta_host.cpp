#include <cassert>
#include <cstdint>
#include <vector>

#include "j3d_state_delta.h"

int main() {
    assert(j3dStateDeltaCanSkip(0x08, 0x50, 1));
    assert(!j3dStateDeltaCanSkip(0x08, 0xA0, 1));
    assert(j3dStateDeltaCanSkip(0x61, 0x20, 0x20000001));
    assert(!j3dStateDeltaCanSkip(0x61, 0, 0xFE000001));
    assert(!j3dStateDeltaCanSkip(0x61, 0, 0x52000001));
    assert(!j3dStateDeltaCanSkip(0x61, 0, 0x45000001));
    assert(!j3dStateDeltaCanSkip(0x61, 0, 0x65000001));
    assert(!j3dStateDeltaCanSkip(0x61, 0, 0x66000001));
    assert(!j3dStateDeltaCanSkip(0x61, 0, 0x4A000001));
    assert(!j3dStateDeltaCanSkip(0x20, 0, 0));
    assert(j3dStateDeltaCanSkip(0x10, 0, 0));

    std::vector<J3DStateDeltaOp> ops;
    ops.push_back({J3D_STATE_DELTA_CP, 0x08, J3D_STATE_DELTA_FLAG_SAFE_SKIP, 0x50, 0, 1, 1});
    ops.push_back({J3D_STATE_DELTA_BP, 0x61, J3D_STATE_DELTA_FLAG_SAFE_SKIP, 0, 0, 1, 0x20000001});
    ops.push_back({J3D_STATE_DELTA_XF, 0x10, J3D_STATE_DELTA_FLAG_SAFE_SKIP, 0, 4, 2, 0});
    ops.push_back({J3D_STATE_DELTA_XF, 0x20, 0, 0, 8, 2, 0});
    assert(ops[0].domain == J3D_STATE_DELTA_CP && ops[1].domain == J3D_STATE_DELTA_BP);
    assert(ops[2].count == 2 && ops[3].type == J3D_STATE_DELTA_XF_INDEXED);
    assert(j3dStateDeltaMarkDomain(J3D_STATE_DELTA_XF, false) == 0);
    assert(j3dStateDeltaMarkDomain(J3D_STATE_DELTA_XF, true) == J3D_STATE_DELTA_XF);
    assert(j3dStateDeltaDirtyForCP(0xA0) == J3D_DIRTY_CP_LAYOUT_ARRAY);
    assert(j3dStateDeltaDirtyForBP(0xF6) == J3D_DIRTY_TEV_TEXTURE);
    assert(j3dStateDeltaDirtyForXF(0x1000, 2) == J3D_DIRTY_XF_MEMORY_UNIFORM);
    assert((j3dStateDeltaDirtyForBP(0x00) & J3D_DIRTY_TEV_TEXTURE) != 0);
    assert((j3dStateDeltaDirtyForBP(0x40) & J3D_DIRTY_PE_RASTER) != 0);
    assert((j3dStateDeltaDirtyForCP(0x50) & J3D_DIRTY_VERTEX_KEY_XF_REG) != 0);
    return 0;
}
