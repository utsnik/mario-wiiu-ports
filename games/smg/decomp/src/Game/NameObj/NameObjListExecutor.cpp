#include "Game/NameObj/NameObjListExecutor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
#include "wiiu/drawown.h"
#include "Game/System/J3DMT.hpp"
#include "wiiu/gphase.h"
extern "C" const char* sms_wiiu_app_root(void);
#include <whb/log.h>
#include <cstdio>
#include <cstdlib>
namespace gx2prod { void service(); }
extern "C" int sms_wiiu_pipe5_enabled(void);
extern "C" int smg_reorder_pump_enabled(void);
extern "C" void sms_wiiu_reorder_pump(void);
extern "C" void smg_wiiu_gpucat_mark(int begin, int category);

namespace {

bool s_skipDrawLoaded;
bool s_skipDraw[256];

void loadSkipDraw() {
    if (s_skipDrawLoaded)
        return;
    s_skipDrawLoaded = true;

    char path[256];
    std::snprintf(path, sizeof path, "%sskipdraw", sms_wiiu_app_root());
    FILE* file = std::fopen(path, "rb");
    if (!file)
        return;

    char buf[512] = {};
    const size_t n = std::fread(buf, 1, sizeof buf - 1, file);
    std::fclose(file);
    buf[n] = 0;

    char ids[512] = {};
    int len = 0;
    for (char* p = buf; *p;) {
        char* end = nullptr;
        const long value = std::strtol(p, &end, 10);
        if (end == p) {
            ++p;
            continue;
        }
        if (value >= 0 && value < 256) {
            s_skipDraw[value] = true;
            len += std::snprintf(ids + len, sizeof ids - len, "%s%ld", len ? " " : "", value);
        }
        p = end;
    }
    WHBLogPrintf("SMG Wii U: skipdraw on (diagnostic): ids=%s", ids);
}

} // namespace
#endif

NameObjListExecutor::NameObjListExecutor() : mBufferHolder(), mMovementList(), mCalcAnimList(), mDrawList() {
}

NameObjListExecutor::~NameObjListExecutor() {
    delete mMovementList;
    delete mCalcAnimList;
    delete mDrawList;
    delete mBufferHolder;
}

template MR::Vector< MR::AssignableArray< DrawBufferGroup* > >::~Vector();

void NameObjListExecutor::init() {
    initMovementList();
    initCalcAnimList();
    initCalcViewAndEntryList();
    initDrawList();
}

s32 NameObjListExecutor::registerDrawBuffer(LiveActor* pActor, int drawBufferType) {
    return mBufferHolder->registerDrawBuffer(pActor, drawBufferType);
}

void NameObjListExecutor::allocateDrawBufferActorList() {
    mMovementList->allocateBuffer();
    mCalcAnimList->allocateBuffer();
    mDrawList->allocateBuffer();
    mBufferHolder->allocateActorListBuffer();
}

void NameObjListExecutor::registerPreDrawFunction(const MR::FunctorBase& rFunc, int drawType) {
    mDrawList->registerExecuteBeforeFunction(rFunc, drawType);
}

void NameObjListExecutor::findLightInfo(LiveActor* pActor, int drawBufferType, int executorIndex) const {
    mBufferHolder->findLightInfo(pActor, drawBufferType, executorIndex);
}

void NameObjListExecutor::incrementCheckMovement(NameObj* pObj, int category) {
    mMovementList->incrementCheck(pObj, category);
}

void NameObjListExecutor::incrementCheckCalcAnim(NameObj* pObj, int category) {
    mCalcAnimList->incrementCheck(pObj, category);
}

void NameObjListExecutor::incrementCheckDraw(NameObj* pObj, int category) {
    mDrawList->incrementCheck(pObj, category);
}

void NameObjListExecutor::addToMovement(NameObj* pObj, int category) {
    mMovementList->add(pObj, category);
}

void NameObjListExecutor::addToCalcAnim(NameObj* pObj, int category) {
    mCalcAnimList->add(pObj, category);
}

void NameObjListExecutor::addToDrawBuffer(LiveActor* pActor, int drawBufferType, int executorIndex) {
    mBufferHolder->active(pActor, drawBufferType, executorIndex);
}

void NameObjListExecutor::addToDraw(NameObj* pObj, int category) {
    mDrawList->add(pObj, category);
}

void NameObjListExecutor::removeToMovement(NameObj* pObj, int category) {
    mMovementList->remove(pObj, category);
}

void NameObjListExecutor::removeToCalcAnim(NameObj* pObj, int category) {
    mCalcAnimList->remove(pObj, category);
}

void NameObjListExecutor::removeToDrawBuffer(LiveActor* pActor, int drawBufferType, int executorIndex) {
    mBufferHolder->deactive(pActor, drawBufferType, executorIndex);
}

void NameObjListExecutor::removeToDraw(NameObj* pObj, int category) {
    mDrawList->remove(pObj, category);
}

void NameObjListExecutor::executeMovement(int category) {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    uint64_t gphase_start = smg_gphase_begin_category(SmgGPhaseMovement, category);
#endif
    mMovementList->execute(category);
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    if (sms_wiiu_pipe5_enabled())
        gx2prod::service();
    else if (smg_reorder_pump_enabled())
        sms_wiiu_reorder_pump();
    smg_gphase_end_category(SmgGPhaseMovement, category, gphase_start);
#endif
}

void NameObjListExecutor::executeCalcAnim(int category) {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    uint64_t gphase_start = smg_gphase_begin_category(SmgGPhaseCategoryCalcAnim, category);
    if (smg_j3dmt_enabled())
        smg_j3dmt_begin_category(category);
#endif
    mCalcAnimList->execute(category);
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    if (sms_wiiu_pipe5_enabled())
        gx2prod::service();
    else if (smg_reorder_pump_enabled())
        sms_wiiu_reorder_pump();
    if (smg_j3dmt_enabled())
        smg_j3dmt_end_category();
    smg_gphase_end_category(SmgGPhaseCategoryCalcAnim, category, gphase_start);
#endif
}

void NameObjListExecutor::entryDrawBuffer2D() {
    mBufferHolder->entry(MR::CameraType_2D);
}

void NameObjListExecutor::entryDrawBuffer3D() {
    mBufferHolder->entry(MR::CameraType_3D);
}

void NameObjListExecutor::entryDrawBufferMirror() {
    mBufferHolder->entry(MR::CameraType_Mirror);
}

void NameObjListExecutor::drawOpa(int drawBufferType) {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    smg_wiiu_gpucat_mark(1, 100 + drawBufferType);  // gpucat ids 100+ = DrawBufferType opa
#endif
    mBufferHolder->drawOpa(drawBufferType);
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    smg_wiiu_gpucat_mark(0, 100 + drawBufferType);
#endif
}

void NameObjListExecutor::drawXlu(int drawBufferType) {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    smg_wiiu_gpucat_mark(1, 150 + drawBufferType);  // gpucat ids 150+ = DrawBufferType xlu
#endif
    mBufferHolder->drawXlu(drawBufferType);
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    smg_wiiu_gpucat_mark(0, 150 + drawBufferType);
#endif
}

void NameObjListExecutor::executeDraw(int category) {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    loadSkipDraw();
    uint64_t gphase_start = smg_gphase_begin_category(SmgGPhaseDraw, category);
    sms_wiiu_drawown_push_draw_type(static_cast<unsigned>(category));
    smg_wiiu_gpucat_mark(1, category);
#endif
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    if (category == MR::DrawType_ShadowVolume) {
        mDrawList->executeShadowVolumeBatch(category);
    } else
#endif
    {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
        if (category < 0 || category >= 256 || !s_skipDraw[category])
#endif
            mDrawList->execute(category);
    }
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    smg_wiiu_gpucat_mark(0, category);
    sms_wiiu_drawown_pop();
    smg_gphase_end_category(SmgGPhaseDraw, category, gphase_start);
#endif
}

void NameObjListExecutor::initMovementList() {
}

void NameObjListExecutor::initCalcAnimList() {
}

void NameObjListExecutor::initCalcViewAndEntryList() {
}

void NameObjListExecutor::initDrawList() {
}
