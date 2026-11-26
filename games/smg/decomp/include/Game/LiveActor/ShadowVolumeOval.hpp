#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"

class ShadowVolumeOval : public ShadowVolumeModel {
public:
    ShadowVolumeOval();

    virtual u32 calcModelDrawMtx(ShadowVolumeModelDrawMtx*, u32) const override;
    virtual bool isDraw() const;

    void setSize(const TVec3f&);

    /* 0x20 */ TVec3f mSize;
};
