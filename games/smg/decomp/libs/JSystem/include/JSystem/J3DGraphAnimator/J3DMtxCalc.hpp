#pragma once

#include <revolution.h>
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
#include "JSystem/J3DGraphAnimator/J3DMTContext.hpp"
#endif

class J3DAnmTransform;
class J3DMtxBuffer;
class J3DJoint;

class J3DMtxCalc {
public:
    static void setMtxBuffer(J3DMtxBuffer*);

    virtual ~J3DMtxCalc() {
    }
    virtual void setAnmTransform(J3DAnmTransform*) {
    }
    virtual J3DAnmTransform* getAnmTransform() {
        return NULL;
    }
    virtual void setAnmTransform(u8, J3DAnmTransform*) {
    }
    virtual J3DAnmTransform* getAnmTransform(u8) {
        return NULL;
    }
    virtual void setWeight(u8, f32) {
    }
    virtual f32 getWeight(u8) const {
        return 0.0f;
    }
    virtual void init(const Vec& param_0, const Mtx&) = 0;
    virtual void calc() = 0;

    static J3DMtxBuffer* getMtxBuffer() {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        return J3DMT::mtxBuffer();
#else
        return mMtxBuffer;
#endif
    }
    static J3DJoint* getJoint() {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        return J3DMT::joint();
#else
        return mJoint;
#endif
    }
    static void setJoint(J3DJoint* joint) {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        J3DMT::joint() = joint;
#else
        mJoint = joint;
#endif
    }

    static J3DMtxBuffer* mMtxBuffer;
    static J3DJoint* mJoint;
};  // Size: 0x4
