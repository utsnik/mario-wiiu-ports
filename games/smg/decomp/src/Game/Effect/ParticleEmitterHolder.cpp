#include "Game/Effect/ParticleEmitterHolder.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/ParticleEmitter.hpp"

#include <JSystem/JParticle/JPAEmitter.hpp>
#include <algorithm>
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
#include "../../../../include/wiiu/jpa_async.h"
#endif

ParticleEmitterHolder::ParticleEmitterHolder(const EffectSystem* pEffectSystem, int numEmitters) : mEffectSystem(pEffectSystem) {
    mEmitters.init(numEmitters);
}

void ParticleEmitterHolder::update(bool param1) {
    for (ParticleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        JPABaseEmitter* pBaseEmitter = pEmitter->mEmitter;
        if (pBaseEmitter == nullptr) {
            continue;
        }

        if (param1 != (pBaseEmitter->getGroupID() == 1)) {
            continue;
        }

        const bool shouldDelete = pBaseEmitter->checkStatus(0x8) && pBaseEmitter->getParticleNumber() == 0;

        if (shouldDelete) {
            mEffectSystem->forceDeleteEmitter(pEmitter);
        } else if (!pEmitter->mStopped) {
            if (MR::Effect::getLinkSingleEmitter(pBaseEmitter) != nullptr) {
                pBaseEmitter->getEmitterCallBackPtr()->init(pBaseEmitter);
            }

            pEmitter->mStopped = true;
        }
    }
}

void ParticleEmitterHolder::forceDeleteAllOneTimeEmitters() {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    sms_wiiu_jpa_async_join();
#endif
    for (ParticleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        if (pEmitter->mEmitter == nullptr) {
            continue;
        }

        if (pEmitter->isContinuousParticle()) {
            continue;
        }

        mEffectSystem->forceDeleteEmitter(pEmitter);
    }
}

void ParticleEmitterHolder::forceDeleteAllEmitters() {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    sms_wiiu_jpa_async_join();
#endif
    for (ParticleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        mEffectSystem->forceDeleteEmitter(pEmitter);
    }
}

void ParticleEmitterHolder::requestMovementOnAllEmitters() {
    for (ParticleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        JPABaseEmitter* pBaseEmitter = pEmitter->mEmitter;
        if (pBaseEmitter == nullptr) {
            continue;
        }

        if (pBaseEmitter->getGroupID() == 1 || pBaseEmitter->getGroupID() == 7) {
            continue;
        }

        pEmitter->pauseOff();
    }
}

ParticleEmitter* ParticleEmitterHolder::findAvailableParticleEmitter() {
    ParticleEmitter* res = std::find_if_array(mEmitters.begin(), mEmitters.end(), std::not1(std::mem_func(&ParticleEmitter::isValid)));
    if (res == mEmitters.end()) {
        return nullptr;
    }

    return res;
}

void ParticleEmitterHolder::requestMovementOffAllLoopEmitters() {
    for (ParticleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        if (pEmitter->mEmitter == nullptr) {
            continue;
        }

        if (!pEmitter->isContinuousParticle()) {
            continue;
        }

        pEmitter->pauseOn();
    }
}

inline bool ParticleEmitter::isValid() const {
    return mEmitter != nullptr;
}

#ifdef __MWERKS__
inline
#endif
bool ParticleEmitter::isContinuousParticle() const NO_INLINE {
    return mEmitter != nullptr && mEmitter->mMaxFrame == 0;
}
