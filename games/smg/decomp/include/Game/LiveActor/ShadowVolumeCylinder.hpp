#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"

class ShadowVolumeCylinder : public ShadowVolumeModel {
public:
    ShadowVolumeCylinder();

    virtual ~ShadowVolumeCylinder();
    virtual u32 calcModelDrawMtx(ShadowVolumeModelDrawMtx*, u32) const override;

    void setRadius(f32);

    /* 0x20 */ f32 mRadius;
};
