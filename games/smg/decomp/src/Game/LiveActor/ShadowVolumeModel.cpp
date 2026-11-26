#include "Game/LiveActor/ShadowVolumeModel.hpp"
#include "Game/Util.hpp"
#include <revolution/gx.h>

ShadowVolumeModel::ShadowVolumeModel(const char* pName) : ShadowVolumeDrawer(pName) {
    mModelData = 0;
}

u32 ShadowVolumeModel::calcModelDrawMtx(ShadowVolumeModelDrawMtx* pMtx, u32 max) const {
    (void)pMtx;
    (void)max;
    return 0;
}

const ShadowVolumeModel* ShadowVolumeModel::getShadowVolumeModel() const {
    return this;
}

void ShadowVolumeModel::initVolumeModel(const char* pModelName) {
    mModelData = MR::getJ3DModelData(pModelName);
}

void ShadowVolumeModel::drawShape() const {
    MR::drawSimpleModel(mModelData);
}

void ShadowVolumeModel::loadModelDrawMtx() const {
    ShadowVolumeModelDrawMtx matrices[10];
    const u32 count = calcModelDrawMtx(matrices, ARRAY_SIZE(matrices));
    for (u32 i = 0; i < count; ++i) {
        GXLoadPosMtxImm(matrices[i].mMtx, matrices[i].mIndex);
    }
}
