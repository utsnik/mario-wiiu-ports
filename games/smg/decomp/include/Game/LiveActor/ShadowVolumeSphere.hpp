#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"

class ShadowVolumeSphere : public ShadowVolumeModel {
public:
    ShadowVolumeSphere();

    virtual ~ShadowVolumeSphere();
    virtual u32 calcModelDrawMtx(ShadowVolumeModelDrawMtx*, u32) const override;
    virtual bool isDraw() const;

    void setRadius(f32);

    /* 0x20 */ f32 mRadius;
};
