#include "JSystem/J3DGraphAnimator/J3DJoint.hpp"
#include "JSystem/J3DGraphAnimator/J3DMaterialAnm.hpp"
#include "JSystem/J3DGraphAnimator/J3DModel.hpp"
#include "JSystem/J3DGraphAnimator/J3DMtxBuffer.hpp"
#include "JSystem/J3DGraphBase/J3DDrawBuffer.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
#include "JSystem/J3DGraphAnimator/J3DMTContext.hpp"
extern "C" u32 OSGetCoreId(void);
#endif

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
namespace J3DMT {
namespace {
J3DMTTraversalContext* s_active[3];
/* Cores with an active context.  Zero unless the j3dmt marker runs joint
 * calc on core 2, so the accessors (several per joint, every model, every
 * frame) skip the coreinit OSGetCoreId call.  A core only finds its own
 * context after its own activate() incremented this, so the fast path never
 * hides a context from the core that set it. */
volatile u32 s_activeCount;
}

J3DMTTraversalContext* getActiveContext() {
    if (s_activeCount == 0)
        return nullptr;
    const u32 core = OSGetCoreId();
    return core < 3 ? s_active[core] : nullptr;
}

void activate(J3DMTTraversalContext* context) {
    const u32 core = OSGetCoreId();
    if (core >= 3)
        return;
    J3DMTTraversalContext* const previous = s_active[core];
    if (context != nullptr && previous == nullptr)
        __sync_fetch_and_add(&s_activeCount, 1);
    s_active[core] = context;
    if (context == nullptr && previous != nullptr)
        __sync_fetch_and_sub(&s_activeCount, 1);
}

void deactivate() {
    activate(nullptr);
}

J3DMtxBuffer*& mtxBuffer() {
    J3DMTTraversalContext* context = getActiveContext();
    return context != nullptr ? context->mMtxBuffer : J3DMtxCalc::mMtxBuffer;
}

J3DJoint*& joint() {
    J3DMTTraversalContext* context = getActiveContext();
    return context != nullptr ? context->mJoint : J3DMtxCalc::mJoint;
}

J3DMtxCalc*& currentMtxCalc() {
    J3DMTTraversalContext* context = getActiveContext();
    return context != nullptr ? context->mCurrentMtxCalc : J3DJoint::mCurrentMtxCalc;
}

Mtx& currentMtx() {
    J3DMTTraversalContext* context = getActiveContext();
    return context != nullptr ? context->mCurrentMtx : J3DSys::mCurrentMtx;
}

Vec& currentS() {
    J3DMTTraversalContext* context = getActiveContext();
    return context != nullptr ? context->mCurrentS : J3DSys::mCurrentS;
}

Vec& parentS() {
    J3DMTTraversalContext* context = getActiveContext();
    return context != nullptr ? context->mParentS : J3DSys::mParentS;
}
}
#endif

void J3DMtxCalcJ3DSysInitBasic::init(Vec const& scale, Mtx const& mtx) {
    Vec unitScale = {1.0f, 1.0f, 1.0f};
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    J3DMT::currentS() = scale;
    J3DMT::parentS() = unitScale;
    JMAMTXApplyScale(mtx, J3DMT::currentMtx(), scale.x, scale.y, scale.z);
#else
    J3DSys::mCurrentS = scale;
    J3DSys::mParentS = unitScale;
    JMAMTXApplyScale(mtx, J3DSys::mCurrentMtx, scale.x, scale.y, scale.z);
#endif
}

void J3DMtxCalcJ3DSysInitMaya::init(Vec const& scale, Mtx const& mtx) {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    J3DMT::parentS() = (Vec){1.0f, 1.0f, 1.0f};
    J3DMT::currentS() = scale;
    JMAMTXApplyScale(mtx, J3DMT::currentMtx(), scale.x, scale.y, scale.z);
#else
    J3DSys::mParentS = (Vec){1.0f, 1.0f, 1.0f};
    J3DSys::mCurrentS = scale;
    JMAMTXApplyScale(mtx, J3DSys::mCurrentMtx, scale.x, scale.y, scale.z);
#endif
}

J3DMtxBuffer* J3DMtxCalc::mMtxBuffer;

J3DJoint* J3DMtxCalc::mJoint;

inline s32 checkScaleOne(const Vec&);

void J3DMtxCalcCalcTransformBasic::calcTransform(J3DTransformInfo const& transInfo) {
    J3DMtxBuffer* mtxBuf = J3DMtxCalc::getMtxBuffer();
    u16 jntNo = J3DMtxCalc::getJoint()->getJntNo();

    MtxPtr anmMtx = mtxBuf->getAnmMtx(jntNo);

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    J3DMT::currentS().x *= transInfo.mScale.x;
    J3DMT::currentS().y *= transInfo.mScale.y;
    J3DMT::currentS().z *= transInfo.mScale.z;
#else
    J3DSys::mCurrentS.x *= transInfo.mScale.x;
    J3DSys::mCurrentS.y *= transInfo.mScale.y;
    J3DSys::mCurrentS.z *= transInfo.mScale.z;
#endif
    J3DGetTranslateRotateMtx(transInfo, anmMtx);

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    if (!checkScaleOne(J3DMT::currentS())) {
#else
    if (!checkScaleOne(J3DSys::mCurrentS)) {
#endif
        mtxBuf->setScaleFlag(jntNo, 0);
        JMAMTXApplyScale(anmMtx, anmMtx, transInfo.mScale.x, transInfo.mScale.y, transInfo.mScale.z);
    } else {
        mtxBuf->setScaleFlag(jntNo, 1);
    }

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    PSMTXConcat(J3DMT::currentMtx(), anmMtx, J3DMT::currentMtx());
    PSMTXCopy(J3DMT::currentMtx(), anmMtx);
#else
    PSMTXConcat(J3DSys::mCurrentMtx, anmMtx, J3DSys::mCurrentMtx);
    PSMTXCopy(J3DSys::mCurrentMtx, anmMtx);
#endif
}

void J3DMtxCalcCalcTransformSoftimage::calcTransform(J3DTransformInfo const& transInfo) {
    J3DMtxBuffer* mtxBuf = J3DMtxCalc::getMtxBuffer();
    u16 jntNo = J3DMtxCalc::getJoint()->getJntNo();

    MtxPtr anmMtx = mtxBuf->getAnmMtx(jntNo);

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    J3DGetTranslateRotateMtx(transInfo.mRotation.x, transInfo.mRotation.y, transInfo.mRotation.z, transInfo.mTranslate.x * J3DMT::currentS().x,
                             transInfo.mTranslate.y * J3DMT::currentS().y, transInfo.mTranslate.z * J3DMT::currentS().z, anmMtx);
    PSMTXConcat(J3DMT::currentMtx(), anmMtx, J3DMT::currentMtx());
#else
    J3DGetTranslateRotateMtx(transInfo.mRotation.x, transInfo.mRotation.y, transInfo.mRotation.z, transInfo.mTranslate.x * J3DSys::mCurrentS.x,
                             transInfo.mTranslate.y * J3DSys::mCurrentS.y, transInfo.mTranslate.z * J3DSys::mCurrentS.z, anmMtx);
    PSMTXConcat(J3DSys::mCurrentMtx, anmMtx, J3DSys::mCurrentMtx);
#endif

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    J3DMT::currentS().x *= transInfo.mScale.x;
    J3DMT::currentS().y *= transInfo.mScale.y;
    J3DMT::currentS().z *= transInfo.mScale.z;
#else
    J3DSys::mCurrentS.x *= transInfo.mScale.x;
    J3DSys::mCurrentS.y *= transInfo.mScale.y;
    J3DSys::mCurrentS.z *= transInfo.mScale.z;
#endif

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    if (!checkScaleOne(J3DMT::currentS())) {
#else
    if (!checkScaleOne(J3DSys::mCurrentS)) {
#endif
        mtxBuf->setScaleFlag(jntNo, 0);

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        JMAMTXApplyScale(J3DMT::currentMtx(), anmMtx, J3DMT::currentS().x, J3DMT::currentS().y, J3DMT::currentS().z);
        anmMtx[0][3] = J3DMT::currentMtx()[0][3];
        anmMtx[1][3] = J3DMT::currentMtx()[1][3];
        anmMtx[2][3] = J3DMT::currentMtx()[2][3];
#else
        JMAMTXApplyScale(J3DSys::mCurrentMtx, anmMtx, J3DSys::mCurrentS.x, J3DSys::mCurrentS.y, J3DSys::mCurrentS.z);
        anmMtx[0][3] = J3DSys::mCurrentMtx[0][3];
        anmMtx[1][3] = J3DSys::mCurrentMtx[1][3];
        anmMtx[2][3] = J3DSys::mCurrentMtx[2][3];
#endif
    } else {
        mtxBuf->setScaleFlag(jntNo, 1);
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        PSMTXCopy(J3DMT::currentMtx(), anmMtx);
#else
        PSMTXCopy(J3DSys::mCurrentMtx, anmMtx);
#endif
    }
}

void J3DMtxCalcCalcTransformMaya::calcTransform(J3DTransformInfo const& transInfo) {
    J3DJoint* joint = J3DMtxCalc::getJoint();
    J3DMtxBuffer* mtxBuf = J3DMtxCalc::getMtxBuffer();

    u16 jntNo = joint->getJntNo();

    MtxPtr anmMtx = mtxBuf->getAnmMtx(jntNo);

    J3DGetTranslateRotateMtx(transInfo, anmMtx);

    if (transInfo.mScale.x == 1.0f && transInfo.mScale.y == 1.0f && transInfo.mScale.z == 1.0f) {
        mtxBuf->setScaleFlag(jntNo, 1);
    } else {
        mtxBuf->setScaleFlag(jntNo, 0);
        JMAMTXApplyScale(anmMtx, anmMtx, transInfo.mScale.x, transInfo.mScale.y, transInfo.mScale.z);
    }

    if (joint->getScaleCompensate() == 1) {
        Vec inverseScale;
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        inverseScale.x = JMath::fastReciprocal(J3DMT::parentS().x);
        inverseScale.y = JMath::fastReciprocal(J3DMT::parentS().y);
        inverseScale.z = JMath::fastReciprocal(J3DMT::parentS().z);
#else
        inverseScale.x = JMath::fastReciprocal(J3DSys::mParentS.x);
        inverseScale.y = JMath::fastReciprocal(J3DSys::mParentS.y);
        inverseScale.z = JMath::fastReciprocal(J3DSys::mParentS.z);
#endif

        anmMtx[0][0] *= inverseScale.x;
        anmMtx[0][1] *= inverseScale.x;
        anmMtx[0][2] *= inverseScale.x;
        anmMtx[1][0] *= inverseScale.y;
        anmMtx[1][1] *= inverseScale.y;
        anmMtx[1][2] *= inverseScale.y;
        anmMtx[2][0] *= inverseScale.z;
        anmMtx[2][1] *= inverseScale.z;
        anmMtx[2][2] *= inverseScale.z;
    }

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    PSMTXConcat(J3DMT::currentMtx(), anmMtx, J3DMT::currentMtx());
    PSMTXCopy(J3DMT::currentMtx(), anmMtx);
#else
    PSMTXConcat(J3DSys::mCurrentMtx, anmMtx, J3DSys::mCurrentMtx);
    PSMTXCopy(J3DSys::mCurrentMtx, anmMtx);
#endif

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    J3DMT::parentS().x = transInfo.mScale.x;
    J3DMT::parentS().y = transInfo.mScale.y;
    J3DMT::parentS().z = transInfo.mScale.z;
#else
    J3DSys::mParentS.x = transInfo.mScale.x;
    J3DSys::mParentS.y = transInfo.mScale.y;
    J3DSys::mParentS.z = transInfo.mScale.z;
#endif
}

void J3DJoint::appendChild(J3DJoint* pChild) {
    if (mChild == NULL) {
        mChild = pChild;
    } else {
        J3DJoint* curChild = mChild;
        while (curChild->getYounger() != NULL) {
            curChild = curChild->getYounger();
        }
        curChild->setYounger(pChild);
    }
}

J3DJoint::J3DJoint() {
    mCallBackUserData = NULL;
    mCallBack = NULL;
    field_0x8 = NULL;
    mChild = NULL;
    mYounger = NULL;
    mJntNo = 0;
    mKind = 1;
    mScaleCompensate = false;
    __memcpy(&mTransformInfo, &j3dDefaultTransformInfo, sizeof(J3DTransformInfo));
    mBoundingSphereRadius = 0.0f;
    mMtxCalc = NULL;
    mMesh = NULL;

    Vec init = {0.0f, 0.0f, 0.0f};
    mMin = init;
    mMax = init;
}

void J3DJoint::entryIn() {
    MtxPtr anmMtx = j3dSys.getModel()->getAnmMtx(mJntNo);
    j3dSys.getDrawBuffer(0)->setZMtx(anmMtx);
    j3dSys.getDrawBuffer(1)->setZMtx(anmMtx);
    for (J3DMaterial* mesh = mMesh; mesh != NULL;) {
        if (mesh->getShape()->checkFlag(J3DShpFlag_Visible)) {
            mesh = mesh->getNext();
        } else {
            J3DMatPacket* matPacket = j3dSys.getModel()->getMatPacket(mesh->getIndex());
            J3DShapePacket* shapePacket = j3dSys.getModel()->getShapePacket(mesh->getShape()->getIndex());
            if (!matPacket->isLocked()) {
                if (mesh->getMaterialAnm()) {
                    J3DMaterialAnm* piVar8 = mesh->getMaterialAnm();
                    piVar8->calc(mesh);
                }
                mesh->calc(anmMtx);
            }
            mesh->setCurrentMtx();
            matPacket->setMaterialAnmID(mesh->getMaterialAnm());
            matPacket->setShapePacket(shapePacket);
            J3DDrawBuffer* drawBuffer = j3dSys.getDrawBuffer(mesh->isDrawModeOpaTexEdge());
            if (matPacket->entry(drawBuffer)) {
                j3dSys.setMatPacket(matPacket);
                J3DDrawBuffer::entryNum++;
                mesh->makeDisplayList();
            }
            mesh = mesh->getNext();
        }
    }
}

J3DMtxCalc* J3DJoint::mCurrentMtxCalc;

void J3DJoint::recursiveCalc() {
    Mtx currentMtx;
    Vec currentScale;
    Vec parentScale;
    J3DJointCallBack callback;
    J3DMtxCalc* previousCalc = NULL;
    PSMTXCopy(
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        J3DMT::currentMtx()
#else
        J3DSys::mCurrentMtx
#endif
        , currentMtx);
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    currentScale = J3DMT::currentS();
    parentScale = J3DMT::parentS();
#else
    currentScale = J3DSys::mCurrentS;
    parentScale = J3DSys::mParentS;
#endif
    J3DMtxCalc* calc = mMtxCalc;
    if (calc != NULL) {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        previousCalc = J3DMT::currentMtxCalc();
#else
        previousCalc = mCurrentMtxCalc;
#endif
        J3DMtxCalc::setJoint(this);
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        J3DMT::currentMtxCalc() = calc;
#else
        mCurrentMtxCalc = calc;
#endif
        calc->calc();
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    } else if (J3DMT::currentMtxCalc() != NULL) {
#else
    } else if (mCurrentMtxCalc != NULL) {
#endif
        J3DMtxCalc::setJoint(this);
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        J3DMT::currentMtxCalc()->calc();
#else
        mCurrentMtxCalc->calc();
#endif
    }
    callback = mCallBack;
    if (callback != NULL) {
        callback(this, 0);
    }
    if (mChild != NULL) {
        mChild->recursiveCalc();
    }
    PSMTXCopy(currentMtx,
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
              J3DMT::currentMtx()
#else
              J3DSys::mCurrentMtx
#endif
    );
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    J3DMT::currentS() = currentScale;
    J3DMT::parentS() = parentScale;
#else
    J3DSys::mCurrentS = currentScale;
    J3DSys::mParentS = parentScale;
#endif
    if (previousCalc != NULL) {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        J3DMT::currentMtxCalc() = previousCalc;
#else
        mCurrentMtxCalc = previousCalc;
#endif
    }
    if (callback != NULL) {
        callback(this, 1);
    }
    if (mYounger != NULL) {
        mYounger->recursiveCalc();
    }
}

inline s32 checkScaleOne(const Vec& param_0) {
    if (param_0.x == 1.0f && param_0.y == 1.0f && param_0.z == 1.0f) {
        return true;
    } else {
        return false;
    }
}
