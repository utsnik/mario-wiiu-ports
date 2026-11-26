#pragma once

#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class J3DModelData;

struct ShadowVolumeModelDrawMtx {
    TPos3f mMtx;
    s32 mIndex;
};

class ShadowVolumeModel : public ShadowVolumeDrawer {
public:
    ShadowVolumeModel(const char*);

    virtual ~ShadowVolumeModel();
    virtual u32 calcModelDrawMtx(ShadowVolumeModelDrawMtx*, u32) const;
    virtual void loadModelDrawMtx() const override;
    virtual void drawShape() const;

    virtual const ShadowVolumeModel* getShadowVolumeModel() const override;

    J3DModelData* getModelData() const {
        return mModelData;
    }

    void initVolumeModel(const char*);

    /* 0x1C */ J3DModelData* mModelData;
};

inline ShadowVolumeModel::~ShadowVolumeModel() {
}
