#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"

class ShadowVolumeOvalPole : public ShadowVolumeModel {
public:
    ShadowVolumeOvalPole();

    virtual ~ShadowVolumeOvalPole();
    virtual u32 calcModelDrawMtx(ShadowVolumeModelDrawMtx*, u32) const override;

    void setSize(const TVec3f&);

    /* 0x20 */ TVec3f mSize;
};
