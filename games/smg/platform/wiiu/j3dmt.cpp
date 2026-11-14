#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/System/J3DMT.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/J3DGraphAnimator/J3DMTContext.hpp>
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DMtxBuffer.hpp>
#include <revolution/os.h>

#include <stdint.h>

extern "C" {
typedef int (*J3DMTThreadEntryPointFn)(int, const char**);
int cafe_OSCreateThread(void*, J3DMTThreadEntryPointFn, int32_t, char*, void*, uint32_t, int32_t,
                        uint8_t) __asm__("OSCreateThread");
int32_t cafe_OSResumeThread(void*) __asm__("OSResumeThread");
void cafe_OSInitEvent(void*, int, int) __asm__("OSInitEvent");
void cafe_OSSignalEvent(void*) __asm__("OSSignalEvent");
void cafe_OSWaitEvent(void*) __asm__("OSWaitEvent");
int32_t cafe_OSAddAtomic(volatile int32_t*, int32_t) __asm__("OSAddAtomic");
void cafe_DCFlushRange(void*, uint32_t) __asm__("DCFlushRange");
void cafe_OSYieldThread(void) __asm__("OSYieldThread");
uint32_t OSGetCoreId(void);
int sms_wiiu_sd_marker(const char*);
uint32_t sms_wiiu_frame_count(void);
void port_log(const char*, ...);
}

namespace {

const int kMaxJobs = 4096;

struct Job {
    ModelManager* mManager;
    LiveActor* mActor;
    J3DModel* mModel;
    J3DMTTraversalContext mContext;
};

uint8_t s_helperThread[0x1000] __attribute__((aligned(16)));
uint8_t s_helperStack[32 * 1024] __attribute__((aligned(16)));
uint8_t s_go[0x100] __attribute__((aligned(16)));
volatile int32_t s_claim;
volatile int32_t s_done;
volatile int s_jobCount;
Job s_jobs[kMaxJobs];
int s_threadState;
int s_markerState = -1;
int s_inCategory;
int s_requesting;
ModelManager* s_requestManager;
LiveActor* s_requestActor;
int s_requestQueued;
unsigned s_jobsLogged;
unsigned s_modelsLogged;
unsigned s_eligibleLogged;
unsigned s_serialLogged;
unsigned s_modelCallbackLogged;
unsigned s_jointCallbackLogged;
unsigned s_deformerLogged;
unsigned s_customJointCalcLogged;
unsigned s_userMatrixLogged;
unsigned s_userAreaLogged;
unsigned s_userAnmMtxLogged;
unsigned s_userAreaOtherRulesLogged;
unsigned s_userAreaJointCallbackLogged;
unsigned s_userAreaCustomJointCalcLogged;
unsigned s_noModelManagerLogged;
unsigned s_invalidModelLogged;
unsigned s_helperDoneLogged;
unsigned s_core1DoneLogged;
uint64_t s_joinWaitUs;
uint64_t s_rejectedRecursiveUs;
uint64_t s_eligibleRecursiveUs;
uint32_t s_j3dCalcDepth[3];
OSTime s_j3dCalcStart[3];
uint64_t s_j3dCalcUs;
unsigned s_j3dCalcCalls;
int s_rejectedCalcPending;
OSTime s_rejectedCalcStart;
OSTime s_lastLogTime;
unsigned s_beginCategoryCalls;

bool markerEnabled() {
    if (s_markerState < 0)
        s_markerState = sms_wiiu_sd_marker("j3dmt") != 0 ? 1 : 0;
    return s_markerState != 0;
}

enum EligibilityFailure {
    FailureNone,
    FailureNoModelManager,
    FailureInvalidModel,
    FailureModelCallback,
    FailureDeformer,
    FailureJointCallback,
    FailureCustomJointCalc,
    FailureUserMatrix,
};

EligibilityFailure modelEligibilityFailure(ModelManager* manager) {
    if (manager == nullptr)
        return FailureNoModelManager;

    J3DModel* model = manager->getJ3DModel();
    if (model == nullptr || model->mModelData == nullptr || model->mMtxBuffer == nullptr)
        return FailureInvalidModel;

    // The prework and all model callbacks remain on core 1. A joint-local
    // calculator can reach arbitrary game state, so it is serial too.
    if (model->mCalcCallBack != nullptr || model->mVtxColorCalc != nullptr || model->mUnkCalc1 != nullptr ||
        model->mUnkCalc2 != nullptr)
        return FailureModelCallback;
    if (model->mDeformData != nullptr || model->mSkinDeform != nullptr)
        return FailureDeformer;
    const bool userArea = model->mUserArea != 0;
    const bool userAnmMtx = model->mMtxBuffer->mpUserAnmMtx != nullptr;
    if (userArea)
        ++s_userAreaLogged;
    if (userAnmMtx)
        ++s_userAnmMtxLogged;
    if (userArea || userAnmMtx)
        return FailureUserMatrix;

    J3DJointTree& tree = model->mModelData->getJointTree();
    for (u16 i = 0; i < tree.getJointNum(); i++) {
        J3DJoint* joint = tree.getJointNodePointer(i);
        if (joint == nullptr || joint->getCallBack() != nullptr)
            return FailureJointCallback;
        if (joint->getMtxCalc() != nullptr)
            return FailureCustomJointCalc;
    }

    return FailureNone;
}

void diagnoseUserArea(ModelManager* manager) {
    J3DModel* model = manager->getJ3DModel();
    if (model == nullptr || model->mModelData == nullptr || model->mMtxBuffer == nullptr || model->mUserArea == 0)
        return;
    if (model->mCalcCallBack != nullptr || model->mVtxColorCalc != nullptr || model->mUnkCalc1 != nullptr ||
        model->mUnkCalc2 != nullptr || model->mDeformData != nullptr || model->mSkinDeform != nullptr ||
        model->mMtxBuffer->mpUserAnmMtx != nullptr)
        return;

    bool jointCallback = false;
    bool customJointCalc = false;
    J3DJointTree& tree = model->mModelData->getJointTree();
    for (u16 i = 0; i < tree.getJointNum(); i++) {
        J3DJoint* joint = tree.getJointNodePointer(i);
        if (joint == nullptr || joint->getCallBack() != nullptr)
            jointCallback = true;
        if (joint != nullptr && joint->getMtxCalc() != nullptr)
            customJointCalc = true;
    }
    if (jointCallback)
        ++s_userAreaJointCallbackLogged;
    if (customJointCalc)
        ++s_userAreaCustomJointCalcLogged;
    if (!jointCallback && !customJointCalc)
        ++s_userAreaOtherRulesLogged;
}

void logEligibilityFailure(EligibilityFailure failure) {
    switch (failure) {
    case FailureNoModelManager: ++s_noModelManagerLogged; break;
    case FailureInvalidModel: ++s_invalidModelLogged; break;
    case FailureModelCallback: ++s_modelCallbackLogged; break;
    case FailureDeformer: ++s_deformerLogged; break;
    case FailureJointCallback: ++s_jointCallbackLogged; break;
    case FailureCustomJointCalc: ++s_customJointCalcLogged; break;
    case FailureUserMatrix: ++s_userMatrixLogged; break;
    case FailureNone: ++s_eligibleLogged; break;
    }
}

void flushJob(const Job& job) {
    J3DMtxBuffer* buffer = job.mModel->mMtxBuffer;
    if (buffer->mpAnmMtx != nullptr) {
        cafe_DCFlushRange(buffer->mpAnmMtx, job.mModel->mModelData->getJointNum() * sizeof(Mtx));
    }
    if (buffer->mpScaleFlagArr != nullptr) {
        cafe_DCFlushRange(buffer->mpScaleFlagArr, job.mModel->mModelData->getJointNum() * sizeof(u8));
    }
    if (buffer->mpWeightEvlpMtx != nullptr && job.mModel->mModelData->getWEvlpMtxNum() != 0) {
        cafe_DCFlushRange(buffer->mpWeightEvlpMtx, job.mModel->mModelData->getWEvlpMtxNum() * sizeof(Mtx));
    }
    if (buffer->mpEvlpScaleFlagArr != nullptr && job.mModel->mModelData->getWEvlpMtxNum() != 0) {
        cafe_DCFlushRange(buffer->mpEvlpScaleFlagArr, job.mModel->mModelData->getWEvlpMtxNum() * sizeof(u8));
    }
}

void runJob(int index) {
    Job& job = s_jobs[index];
    J3DMT::activate(&job.mContext);
    const OSTime start = OSGetTime();
    job.mModel->calcAnmMtxPure();
    s_eligibleRecursiveUs += (uint64_t)OSTicksToMicroseconds(OSGetTime() - start);
    flushJob(job);
    J3DMT::deactivate();

    if (OSGetCoreId() == 2)
        ++s_helperDoneLogged;
    else if (OSGetCoreId() == 1)
        ++s_core1DoneLogged;
    cafe_OSAddAtomic(&s_done, 1);
}

void runAvailableJobs() {
    for (;;) {
        const int index = cafe_OSAddAtomic(&s_claim, 1);
        if (index >= s_jobCount)
            return;
        runJob(index);
    }
}

int helper(int, const char**) {
    for (;;) {
        cafe_OSWaitEvent(s_go);
        __asm__ volatile("sync" ::: "memory");
        runAvailableJobs();
    }
    return 0;
}

bool startHelper() {
    if (s_threadState != 0)
        return s_threadState > 0;

    s_threadState = -1;
    cafe_OSInitEvent(s_go, 0, 1);
    if (!cafe_OSCreateThread(s_helperThread, helper, 0, nullptr, s_helperStack + sizeof(s_helperStack), sizeof(s_helperStack), 17, 1 << 2)) {
        port_log("[j3dmt] core-2 helper create failed; serial animation retained\n");
        return false;
    }
    cafe_OSResumeThread(s_helperThread);
    s_threadState = 1;
    port_log("[j3dmt] core-2 helper started\n");
    return true;
}

void resetQueue() {
    s_claim = 0;
    s_done = 0;
    s_jobCount = 0;
}

void joinJobs() {
    if (!s_inCategory || s_jobCount == 0)
        return;

    const OSTime start = OSGetTime();
    __asm__ volatile("sync" ::: "memory");
    cafe_OSSignalEvent(s_go);
    runAvailableJobs();
    while (s_done < s_jobCount) {
        cafe_OSYieldThread();
        runAvailableJobs();
    }
    s_joinWaitUs += (uint64_t)OSTicksToMicroseconds(OSGetTime() - start);
}

void finishSerialTails() {
    for (int i = 0; i < s_jobCount; i++) {
        s_jobs[i].mManager->updateDL(true);
        if (s_jobs[i].mActor->mCollisionParts != nullptr)
            MR::setCollisionMtx(s_jobs[i].mActor);
    }
}

void maybeLog() {
    const OSTime now = OSGetTime();
    if (s_lastLogTime != 0 && now - s_lastLogTime < OSSecondsToTicks(2))
        return;

    s_lastLogTime = now;
    port_log("[j3dmt] begin-calls=%u frame=%u models=%u eligible=%u rejected(model-callback=%u joint-callback=%u deformer=%u custom-joint-calc=%u user-matrix=%u user-area=%u user-anm-mtx=%u user-area-other=%u user-area-joint-callback=%u user-area-custom-joint-calc=%u no-model-manager=%u invalid-model=%u) jobs=%u serial=%u recursive-us(rejected=%llu eligible=%llu) j3d-calc-us=%llu j3d-calc-calls=%u helper-done=%u core-1-done=%u join-wait-us=%llu\n",
             s_beginCategoryCalls, sms_wiiu_frame_count(), s_modelsLogged, s_eligibleLogged, s_modelCallbackLogged,
             s_jointCallbackLogged, s_deformerLogged,
             s_customJointCalcLogged, s_userMatrixLogged, s_userAreaLogged, s_userAnmMtxLogged,
             s_userAreaOtherRulesLogged, s_userAreaJointCallbackLogged, s_userAreaCustomJointCalcLogged,
             s_noModelManagerLogged, s_invalidModelLogged,
             s_jobsLogged, s_serialLogged, (unsigned long long)s_rejectedRecursiveUs,
             (unsigned long long)s_eligibleRecursiveUs, (unsigned long long)s_j3dCalcUs, s_j3dCalcCalls,
             s_helperDoneLogged, s_core1DoneLogged,
             (unsigned long long)s_joinWaitUs);
    s_jobsLogged = 0;
    s_modelsLogged = 0;
    s_eligibleLogged = 0;
    s_serialLogged = 0;
    s_modelCallbackLogged = 0;
    s_jointCallbackLogged = 0;
    s_deformerLogged = 0;
    s_customJointCalcLogged = 0;
    s_userMatrixLogged = 0;
    s_userAreaLogged = 0;
    s_userAnmMtxLogged = 0;
    s_userAreaOtherRulesLogged = 0;
    s_userAreaJointCallbackLogged = 0;
    s_userAreaCustomJointCalcLogged = 0;
    s_noModelManagerLogged = 0;
    s_invalidModelLogged = 0;
    s_helperDoneLogged = 0;
    s_core1DoneLogged = 0;
    s_joinWaitUs = 0;
    s_rejectedRecursiveUs = 0;
    s_eligibleRecursiveUs = 0;
    s_j3dCalcUs = 0;
    s_j3dCalcCalls = 0;
}

} // namespace

extern "C" int smg_j3dmt_enabled() {
    return markerEnabled() && startHelper();
}

extern "C" void smg_j3dmt_calc_enter() {
    if (!markerEnabled())
        return;
    const uint32_t core = OSGetCoreId();
    if (core >= 3)
        return;
    if (s_j3dCalcDepth[core]++ == 0)
        s_j3dCalcStart[core] = OSGetTime();
}

extern "C" void smg_j3dmt_calc_exit() {
    if (!markerEnabled())
        return;
    const uint32_t core = OSGetCoreId();
    if (core >= 3 || s_j3dCalcDepth[core] == 0)
        return;
    if (--s_j3dCalcDepth[core] == 0) {
        s_j3dCalcUs += (uint64_t)OSTicksToMicroseconds(OSGetTime() - s_j3dCalcStart[core]);
        ++s_j3dCalcCalls;
    }
}

extern "C" void smg_j3dmt_begin_category(int category) {
    if (!smg_j3dmt_enabled())
        return;
    ++s_beginCategoryCalls;
    if (s_beginCategoryCalls <= 10 || s_beginCategoryCalls % 1000 == 0)
        port_log("[j3dmt] begin-call=%u category=%d frame=%u\n", s_beginCategoryCalls, category,
                 sms_wiiu_frame_count());
    s_inCategory = 1;
    s_requesting = 0;
    s_requestManager = nullptr;
    s_requestActor = nullptr;
    s_requestQueued = 0;
    resetQueue();
    maybeLog();
}

extern "C" int smg_j3dmt_try_queue_actor(LiveActor* actor) {
    EligibilityFailure failure = FailureNone;
    if (s_inCategory)
        failure = actor == nullptr ? FailureNoModelManager : modelEligibilityFailure(actor->mModelManager);
    if (!s_inCategory || failure != FailureNone) {
        if (s_inCategory) {
            ++s_modelsLogged;
            if (actor != nullptr && actor->mModelManager != nullptr)
                diagnoseUserArea(actor->mModelManager);
            logEligibilityFailure(failure);
            s_rejectedCalcPending = actor != nullptr && actor->mModelManager != nullptr;
            ++s_serialLogged;
        }
        joinJobs();
        return 0;
    }

    ++s_modelsLogged;
    logEligibilityFailure(FailureNone);
    s_requesting = 1;
    s_requestManager = actor->mModelManager;
    s_requestActor = actor;
    s_requestQueued = 0;
    actor->calcAnmMtx();
    s_requesting = 0;
    s_requestManager = nullptr;
    s_requestActor = nullptr;
    return s_requestQueued;
}

extern "C" int smg_j3dmt_serial_calc_begin() {
    if (!s_rejectedCalcPending)
        return 0;
    s_rejectedCalcPending = 0;
    s_rejectedCalcStart = OSGetTime();
    return 1;
}

extern "C" void smg_j3dmt_serial_calc_end() {
    s_rejectedRecursiveUs += (uint64_t)OSTicksToMicroseconds(OSGetTime() - s_rejectedCalcStart);
}

extern "C" int smg_j3dmt_try_queue_model(ModelManager* manager) {
    if (!s_inCategory || !s_requesting || manager != s_requestManager)
        return 0;

    Job& job = s_jobs[s_jobCount++];
    job.mManager = manager;
    job.mActor = s_requestActor;
    job.mModel = manager->getJ3DModel();
    manager->calcBeforeAnmMtx();
    job.mContext.mMtxBuffer = job.mModel->mMtxBuffer;
    job.mContext.mJoint = nullptr;
    job.mContext.mCurrentMtxCalc = nullptr;
    s_requestQueued = 1;
    ++s_jobsLogged;
    ++s_eligibleLogged;
    return 1;
}

extern "C" void smg_j3dmt_end_category() {
    if (!s_inCategory)
        return;

    joinJobs();
    finishSerialTails();
    s_inCategory = 0;
    maybeLog();
    resetQueue();
}

#endif
