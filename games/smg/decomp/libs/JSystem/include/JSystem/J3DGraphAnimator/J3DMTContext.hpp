#pragma once

#include <revolution.h>

class J3DMtxBuffer;
class J3DMtxCalc;
class J3DJoint;

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
struct J3DMTTraversalContext {
    J3DMtxBuffer* mMtxBuffer;
    J3DJoint* mJoint;
    J3DMtxCalc* mCurrentMtxCalc;
    Mtx mCurrentMtx;
    Vec mCurrentS;
    Vec mParentS;
};

namespace J3DMT {
J3DMTTraversalContext* getActiveContext();
void activate(J3DMTTraversalContext*);
void deactivate();
J3DMtxBuffer*& mtxBuffer();
J3DJoint*& joint();
J3DMtxCalc*& currentMtxCalc();
Mtx& currentMtx();
Vec& currentS();
Vec& parentS();
}
#endif
