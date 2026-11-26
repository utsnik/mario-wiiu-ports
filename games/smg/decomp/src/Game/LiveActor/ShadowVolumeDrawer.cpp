#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include "Game/LiveActor/ShadowVolumeModel.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <JSystem/J3DGraphBase/J3DShapeDraw.hpp>
#include "wiiu/jpa_final_vertex.h"
#include "wiiu/shadow_volume_merge.h"
#include "wiiu/shadow_volume_instancing.h"
#include <cstring>
#include <limits.h>
extern "C" int sms_wiiu_sd_marker(const char*);
extern "C" void sms_port_heap_enter(void);
extern "C" void port_log(const char*, ...);
extern "C" void sms_port_heap_leave(void);
extern "C" int sms_wiiu_jpa_finalvx_enabled(void);
extern "C" void* sms_wiiu_jpa_finalvx_alloc(uint32_t bytes, uint32_t alignment, uint32_t* slotOut);
extern "C" bool sms_wiiu_jpa_finalvx_submit(const SmsWiiuJpaFinalVxDesc* desc);
extern "C" bool sms_wiiu_shadow_volume_instanced(const void* modelData,
                                                   const float* matrices,
                                                   const uint8_t* matrixIndices,
                                                   u32 instanceCount);
#endif

namespace {
    static Color8 sShapeColor(0xC0, 0xC0, 0, 4);
    static Color8 sDebugShapeColor(0xFF, 0, 0, 0x80);
};  // namespace

ShadowVolumeDrawInit::ShadowVolumeDrawInit() : NameObj("\x83\x56\x83\x83\x83\x68\x83\x45\x83\x7b\x83\x8a\x83\x85\x81\x5b\x83\x80\x95\x60\x89\xe6\x8f\x89\x8a\xfa\x89\xbb") {
    MR::registerPreDrawFunction(MR::Functor(&MR::setupShadowVolumeDraw), MR::DrawType_ShadowVolume);
}

extern "C" bool sms_wiiu_shadow_volume_model_walk(
    const void* modelData, SmsWiiuShadowVolumeModelCommandFn callback, void* context) {
    if (!modelData || !callback) return false;
    J3DModelData* data = const_cast<J3DModelData*>(
        static_cast<const J3DModelData*>(modelData));
    if (data->getJointNum() == 0) return false;
    J3DMaterial* material = data->getJointNodePointer(0)->getMesh();
    for (; material; material = material->getNext()) {
        J3DShape* shape = material->getShape();
        if (!shape || !shape->getVcdVatCmd()) return false;
        callback(shape->getVcdVatCmd(), J3DShape::kVcdVatDLSize, 0, 0, context);
        for (u32 group = 0; group < shape->getMtxGroupNum(); ++group) {
            J3DShapeDraw* draw = shape->getShapeDraw(group);
            if (draw)
                callback(0, 0, draw->getDisplayList(), draw->getDisplayListSize(), context);
        }
    }
    return true;
}

extern "C" const void* sms_wiiu_shadow_volume_model_pos_array(const void* modelData) {
    if (!modelData) return 0;
    J3DModelData* data = const_cast<J3DModelData*>(
        static_cast<const J3DModelData*>(modelData));
    return data->getVertexData().getVtxPosArray();
}

ShadowVolumeDrawer::ShadowVolumeDrawer(const char* pName) : ShadowDrawer(pName) {
    mStartDrawShapeOffset = 0.0f;
    mEndDrawShapeOffset = 0.0f;
    mIsCutDropShadow = false;
    MR::createSceneObj(SceneObj_ShadowVolumeDrawInit);
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ShadowVolume);
}

void ShadowVolumeDrawer::setStartDrawShepeOffset(f32 offs) {
    mStartDrawShapeOffset = offs;
}

void ShadowVolumeDrawer::setEndDrawShepeOffset(f32 offs) {
    mEndDrawShapeOffset = offs;
}

void ShadowVolumeDrawer::onCutDropShadow() {
    mIsCutDropShadow = true;
}

void ShadowVolumeDrawer::offCutDropShadow() {
    mIsCutDropShadow = false;
}

void ShadowVolumeDrawer::calcBaseDropPosition(TVec3f* pVec) const {
    calcBaseDropPosition(pVec, getController());
}

void ShadowVolumeDrawer::calcBaseDropPosition(TVec3f* pPosition, const ShadowController* pController) const {
    TVec3f position;
    TVec3f direction;
    pController->getDropPos(&position);
    pController->getDropDir(&direction);
    pPosition->set(position + direction * mStartDrawShapeOffset);
}

f32 ShadowVolumeDrawer::calcBaseDropLength() const {
    return calcBaseDropLength(getController());
}

f32 ShadowVolumeDrawer::calcBaseDropLength(const ShadowController* pController) const {
    f32 length = pController->getDropLength();

    if (mIsCutDropShadow) {
        if (pController->isProjected()) {
            length = pController->getProjectionLength();
        }
    }

    f32 endOffset = mEndDrawShapeOffset;
    f32 startOffset = mStartDrawShapeOffset;
    length += -startOffset + endOffset;
    return length;
}

void ShadowVolumeDrawer::loadModelDrawMtx() const {
}

void ShadowVolumeDrawer::drawShape() const {
}

bool ShadowVolumeDrawer::isDraw() const {
    return getController()->isDraw();
}

bool ShadowVolumeDrawer::isShadowVolumeBatchable() const {
    return true;
}

u32 ShadowVolumeDrawer::getShadowVolumeBatchState() const {
    return 0;
}

namespace {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    s32 sShadowVolumeBatchMarker = -1;
    s32 sShadowVolumeMergeMarker = -1;
    s32 sShadowVolumeInstancedMarker = -1;
#endif

    void setShadowVolumePassState(bool subtract) {
        GXSetCullMode(subtract ? GX_CULL_BACK : GX_CULL_FRONT);
        GXSetBlendMode(subtract ? GX_BM_SUBTRACT : GX_BM_BLEND,
                       subtract ? GX_BL_ZERO : GX_BL_ONE,
                       subtract ? GX_BL_ZERO : GX_BL_ONE, GX_LO_COPY);
    }

#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    struct ShadowVolumePositionReader {
        const J3DModelData* mModelData;
        mutable bool mValid;

        bool read(u32 index, SmsWiiuShadowVolumeVec3* pOut) const {
            if (!pOut || index >= mModelData->getVtxNum()) {
                mValid = false;
                return false;
            }
            const J3DVertexData& data = const_cast<J3DModelData*>(mModelData)->getVertexData();
            if (data.getVtxPosType() == GX_F32) {
                const f32* positions = static_cast<const f32*>(data.getVtxPosArray());
                pOut->x = positions[index * 3 + 0];
                pOut->y = positions[index * 3 + 1];
                pOut->z = positions[index * 3 + 2];
                return true;
            }
            if (data.getVtxPosType() == GX_S16 && data.getVtxPosFrac() < 31) {
                const s16* positions = static_cast<const s16*>(data.getVtxPosArray());
                const f32 scale = 1.0f / static_cast<f32>(1u << data.getVtxPosFrac());
                pOut->x = positions[index * 3 + 0] * scale;
                pOut->y = positions[index * 3 + 1] * scale;
                pOut->z = positions[index * 3 + 2] * scale;
                return true;
            }
            mValid = false;
            return false;
        }
    };

    struct ShadowVolumeCachedModel {
        const J3DModelData* mModelData;
        // Model data is freed with its scene and the address can be reused by
        // another model: the position array and first display list must match too.
        const void* mPosArray;
        const void* mFirstDl;
        u32 mFirstDlSize;
        SmsWiiuShadowVolumeTriangle* mTriangles;
        u32 mTriangleCount;
        bool mHasMatrixIndices;
        bool mValid;
        ShadowVolumeCachedModel* mNext;
    };

    ShadowVolumeCachedModel* sShadowVolumeCachedModels = 0;

    bool getShadowVolumeShapeLayout(J3DShape* pShape, u32* pStride, u32* pPosOffset,
                                    u32* pPosBytes, u32* pMatrixOffset, u32* pMatrixBytes,
                                    bool* pHasMatrix) {
        if (!pShape || !pStride || !pPosOffset || !pPosBytes || !pMatrixOffset ||
            !pMatrixBytes || !pHasMatrix) return false;
        *pStride = 0;
        *pPosOffset = 0;
        *pPosBytes = 0;
        *pMatrixOffset = UINT32_MAX;
        *pMatrixBytes = 0;
        *pHasMatrix = false;
        for (GXVtxDescList* desc = pShape->getVtxDesc(); desc->attr != GX_VA_NULL; ++desc) {
            const u32 bytes = sms_wiiu_shadow_volume_attr_bytes(desc->type);
            if (desc->type == GX_DIRECT || (desc->type != GX_NONE && bytes == 0)) return false;
            if (desc->attr == GX_VA_POS) {
                if (bytes == 0) return false;
                *pPosOffset = *pStride;
                *pPosBytes = bytes;
            } else if (desc->attr == GX_VA_PNMTXIDX && bytes != 0) {
                *pMatrixOffset = *pStride;
                *pMatrixBytes = bytes;
                *pHasMatrix = true;
            }
            *pStride += bytes;
        }
        return *pStride != 0 && *pPosBytes != 0;
    }

    bool countShadowVolumeShapeTriangles(J3DShape* pShape, const ShadowVolumePositionReader& reader,
                                         u32* pCount, bool* pHasMatrix) {
        u32 stride, posOffset, posBytes, matrixOffset, matrixBytes;
        bool hasMatrix;
        if (!getShadowVolumeShapeLayout(pShape, &stride, &posOffset, &posBytes, &matrixOffset,
                                        &matrixBytes, &hasMatrix)) return false;
        for (u32 group = 0; group < pShape->getMtxGroupNum(); ++group) {
            J3DShapeDraw* draw = pShape->getShapeDraw(group);
            if (!draw) continue;
            u32 count = 0;
            if (!sms_wiiu_shadow_volume_extract_triangles(
                    draw->getDisplayList(), draw->getDisplayListSize(), stride, posOffset, posBytes,
                    matrixOffset, matrixBytes, reader, 0, 0, &count)) return false;
            if (count > UINT32_MAX - *pCount) return false;
            *pCount += count;
        }
        *pHasMatrix = *pHasMatrix || hasMatrix;
        return true;
    }

    void shadowVolumeModelSignature(const J3DModelData* pModelData, const void** pPos,
                                    const void** pDl, u32* pDlSize) {
        *pPos = 0; *pDl = 0; *pDlSize = 0;
        if (!pModelData || pModelData->getJointNum() == 0) return;
        J3DModelData* data = const_cast<J3DModelData*>(pModelData);
        *pPos = data->getVertexData().getVtxPosArray();
        J3DMaterial* material = data->getJointNodePointer(0)->getMesh();
        J3DShape* shape = material ? material->getShape() : 0;
        J3DShapeDraw* draw = shape && shape->getMtxGroupNum() ? shape->getShapeDraw(0) : 0;
        if (draw) { *pDl = draw->getDisplayList(); *pDlSize = draw->getDisplayListSize(); }
    }

    ShadowVolumeCachedModel* cacheShadowVolumeModel(const J3DModelData* pModelData) {
        const void* pos; const void* dl; u32 dlSize;
        shadowVolumeModelSignature(pModelData, &pos, &dl, &dlSize);
        ShadowVolumeCachedModel* cache = 0;
        for (ShadowVolumeCachedModel* c = sShadowVolumeCachedModels; c; c = c->mNext) {
            if (c->mModelData != pModelData) continue;
            if (c->mPosArray == pos && c->mFirstDl == dl && c->mFirstDlSize == dlSize)
                return c->mValid ? c : 0;
            cache = c;  // same address, different model: rebuild this entry
            break;
        }
        if (cache) {
            sms_port_heap_enter();
            delete[] cache->mTriangles;
            sms_port_heap_leave();
        } else {
            // System heap, never a game JKR heap (the scene heap is nearly full while drawing).
            sms_port_heap_enter();
            cache = new ShadowVolumeCachedModel;
            sms_port_heap_leave();
            if (!cache) return 0;
            cache->mNext = sShadowVolumeCachedModels;
            sShadowVolumeCachedModels = cache;
        }
        cache->mModelData = pModelData;
        cache->mPosArray = pos;
        cache->mFirstDl = dl;
        cache->mFirstDlSize = dlSize;
        cache->mTriangles = 0;
        cache->mTriangleCount = 0;
        cache->mHasMatrixIndices = false;
        cache->mValid = false;
        if (!pModelData || pModelData->getJointNum() == 0 ||
            const_cast<J3DModelData*>(pModelData)->getVertexData().getVtxPosArray() == 0) return 0;

        ShadowVolumePositionReader reader = {pModelData, true};
        J3DMaterial* material = pModelData->getJointNodePointer(0)->getMesh();
        u32 triangleCount = 0;
        bool hasMatrix = false;
        for (; material; material = material->getNext()) {
            if (!material->getShape() || !countShadowVolumeShapeTriangles(
                    material->getShape(), reader, &triangleCount, &hasMatrix)) return 0;
        }
        if (!reader.mValid || triangleCount == 0) return 0;
        sms_port_heap_enter();
        cache->mTriangles = new SmsWiiuShadowVolumeTriangle[triangleCount];
        sms_port_heap_leave();
        if (!cache->mTriangles) return 0;
        u32 written = 0;
        material = pModelData->getJointNodePointer(0)->getMesh();
        for (; material; material = material->getNext()) {
            J3DShape* shape = material->getShape();
            u32 stride, posOffset, posBytes, matrixOffset, matrixBytes;
            bool shapeHasMatrix;
            if (!getShadowVolumeShapeLayout(shape, &stride, &posOffset, &posBytes, &matrixOffset,
                                            &matrixBytes, &shapeHasMatrix)) return 0;
            for (u32 group = 0; group < shape->getMtxGroupNum(); ++group) {
                J3DShapeDraw* draw = shape->getShapeDraw(group);
                if (!draw) continue;
                u32 shapeWritten = 0;
                if (!sms_wiiu_shadow_volume_extract_triangles(
                        draw->getDisplayList(), draw->getDisplayListSize(), stride, posOffset, posBytes,
                        matrixOffset, matrixBytes, reader, cache->mTriangles + written,
                        triangleCount - written, &shapeWritten)) return 0;
                written += shapeWritten;
            }
        }
        if (!reader.mValid || written != triangleCount) return 0;
        cache->mTriangleCount = written;
        cache->mHasMatrixIndices = hasMatrix;
        cache->mValid = true;
        return cache;
    }

    struct ShadowVolumeMergeInfo {
        ShadowVolumeDrawer* mVolume;
        const ShadowVolumeModel* mModel;
        ShadowVolumeCachedModel* mCache;
        ShadowVolumeModelDrawMtx mMatrices[10];
        u32 mMatrixCount;
    };

    struct ShadowVolumeInstancedMember {
        ShadowVolumeDrawer* mVolume;
        ShadowVolumeModelDrawMtx mMatrix;
    };

    struct ShadowVolumeInstancedGroup {
        const J3DModelData* mModelData;
        u32 mFirst;
        u32 mCount;
        u32 mNext;
    };

    bool prepareShadowVolumeMergeInfo(ShadowVolumeMergeInfo* info) {
        if (!info || !info->mVolume || !info->mModel || !info->mCache) return false;
        info->mMatrixCount = info->mModel->calcModelDrawMtx(info->mMatrices, ARRAY_SIZE(info->mMatrices));
        if (info->mMatrixCount == 0 || info->mMatrixCount > ARRAY_SIZE(info->mMatrices)) return false;
        if (info->mMatrixCount > 1 && !info->mCache->mHasMatrixIndices) return false;
        for (u32 i = 0; i < info->mMatrixCount; ++i)
            if (info->mMatrices[i].mIndex < 0 || info->mMatrices[i].mIndex > 255) return false;
        return true;
    }

    bool findShadowVolumeMatrix(const ShadowVolumeMergeInfo& info, u8 index, const TPos3f** pMtx) {
        for (u32 i = 0; i < info.mMatrixCount; ++i) {
            if (info.mMatrices[i].mIndex == index) {
                *pMtx = &info.mMatrices[i].mMtx;
                return true;
            }
        }
        return false;
    }

    SmsWiiuJpaFinalVxDesc* allocShadowVolumeFinalVx(u32 vertexCount) {
        if (vertexCount == 0 || vertexCount > UINT32_MAX / 20u ||
            vertexCount > UINT32_MAX / sizeof(u32)) return 0;
        const u32 vertexBytes = vertexCount * 20u;
        const u32 indexBytes = vertexCount * sizeof(u32);
        u32 slot = 0;
        SmsWiiuJpaFinalVxDesc* desc = static_cast<SmsWiiuJpaFinalVxDesc*>(
            sms_wiiu_jpa_finalvx_alloc(sizeof(SmsWiiuJpaFinalVxDesc), 32, &slot));
        if (!desc) return 0;
        const u32 descriptorSlot = slot;
        uint8_t* vertices = static_cast<uint8_t*>(
            sms_wiiu_jpa_finalvx_alloc(vertexBytes, 32, &slot));
        if (!vertices || slot != descriptorSlot) return 0;
        u32* indices = static_cast<u32*>(sms_wiiu_jpa_finalvx_alloc(indexBytes, 256, &slot));
        if (!indices || slot != descriptorSlot) return 0;
        memset(desc, 0, sizeof(*desc));
        desc->vertices = vertices;
        desc->indices = indices;
        desc->vertexCount = vertexCount;
        desc->indexCount = vertexCount;
        desc->particleCount = 1;
        desc->vertexStride = sms_wiiu_jpa_finalvx_stride(SMS_WIIU_JPA_FINALVX_TEX0);
        desc->packedFmt = SMS_WIIU_JPA_FINALVX_TEX0;
        desc->primitiveClass = SMS_WIIU_JPA_FINALVX_PRIM_TRIS;
        static u32 nextRunId = 1;
        desc->runId = nextRunId++;
        if (desc->runId == 0) desc->runId = nextRunId++;
        desc->vertexBytes = vertexBytes;
        desc->indexBytes = indexBytes;
        desc->slot = descriptorSlot;
        for (u32 i = 0; i < vertexCount; ++i) indices[i] = i;
        return desc;
    }

    bool fillShadowVolumeFinalVx(const ShadowVolumeMergeInfo* infos, u32 infoCount,
                                 SmsWiiuJpaFinalVxDesc* desc) {
        if (!infos || !desc) return false;
        u32 vertex = 0;
        const float tex[2] = {0.0f, 0.0f};
        for (u32 i = 0; i < infoCount; ++i) {
            const ShadowVolumeMergeInfo& info = infos[i];
            for (u32 t = 0; t < info.mCache->mTriangleCount; ++t) {
                const SmsWiiuShadowVolumeTriangle& triangle = info.mCache->mTriangles[t];
                for (u32 corner = 0; corner < 3; ++corner) {
                    const TPos3f* matrix;
                    if (!findShadowVolumeMatrix(info, triangle.matrix[corner], &matrix)) return false;
                    const SmsWiiuShadowVolumeVec3 transformed = sms_wiiu_shadow_volume_transform_point(
                        matrix->mMtx, triangle.pos[corner]);
                    const float pos[3] = {transformed.x, transformed.y, transformed.z};
                    sms_wiiu_jpa_finalvx_pack(const_cast<uint8_t*>(desc->vertices) + vertex * desc->vertexStride,
                                              desc->packedFmt, pos, 0, 0, tex);
                    ++vertex;
                }
            }
        }
        return vertex == desc->vertexCount;
    }

    void drawShadowVolumePass(ShadowVolumeDrawer* const* volumes, u32 count, bool onlyUnmerged,
                              const ShadowVolumeMergeInfo* infos, u32 infoCount) {
        for (u32 i = 0; i < count; ++i) {
            ShadowVolumeDrawer* volume = volumes[i];
            if (!volume->isDraw()) continue;
            bool merged = false;
            if (onlyUnmerged) {
                for (u32 j = 0; j < infoCount; ++j)
                    if (infos[j].mVolume == volume) merged = true;
            }
            if (!onlyUnmerged || !merged) {
                volume->loadModelDrawMtx();
                volume->drawShape();
            }
        }
    }

    bool prepareShadowVolumeInstancedMember(ShadowVolumeDrawer* volume,
                                            ShadowVolumeInstancedMember* member) {
        if (!volume || !member || !volume->isDraw()) return false;
        const ShadowVolumeModel* model = volume->getShadowVolumeModel();
        if (!model || !model->getModelData()) return false;
        ShadowVolumeModelDrawMtx matrices[10];
        if (model->calcModelDrawMtx(matrices, ARRAY_SIZE(matrices)) != 1) return false;
        if (matrices[0].mIndex < 0 || matrices[0].mIndex > 255) return false;
        member->mVolume = volume;
        member->mMatrix = matrices[0];
        return true;
    }

    bool sameShadowVolumeInstancedModel(const ShadowVolumeInstancedGroup& group,
                                        const ShadowVolumeInstancedMember& member) {
        return group.mModelData == member.mVolume->getShadowVolumeModel()->getModelData();
    }

    bool drawShadowVolumeInstancedPass(const ShadowVolumeInstancedGroup* groups, u32 groupCount,
                                       const ShadowVolumeInstancedMember* members, bool subtract) {
        bool allSubmitted = true;
        for (u32 groupNo = 0; groupNo < groupCount; ++groupNo) {
            const ShadowVolumeInstancedGroup& group = groups[groupNo];
            if (group.mCount < 2) {
                allSubmitted = false;
                for (u32 i = 0; i < group.mCount; ++i) {
                    ShadowVolumeDrawer* volume = members[group.mFirst + i].mVolume;
                    if (volume->isDraw()) {
                        volume->loadModelDrawMtx();
                        volume->drawShape();
                    }
                }
                continue;
            }

            sms_port_heap_enter();
            float* matrices = new float[group.mCount * 12];
            uint8_t* matrixIndices = new uint8_t[group.mCount];
            sms_port_heap_leave();
            bool submitted = matrices && matrixIndices;
            if (submitted) {
                for (u32 i = 0; i < group.mCount; ++i) {
                    const ShadowVolumeModelDrawMtx& matrix = members[group.mFirst + i].mMatrix;
                    memcpy(matrices + i * 12, matrix.mMtx, sizeof(float) * 12);
                    matrixIndices[i] = static_cast<uint8_t>(matrix.mIndex);
                }
                submitted = sms_wiiu_shadow_volume_instanced(group.mModelData, matrices,
                                                              matrixIndices, group.mCount);
            }
            sms_port_heap_enter();
            delete[] matrixIndices;
            delete[] matrices;
            sms_port_heap_leave();
            if (submitted) continue;

            allSubmitted = false;
            for (u32 i = 0; i < group.mCount; ++i) {
                ShadowVolumeDrawer* volume = members[group.mFirst + i].mVolume;
                if (volume->isDraw()) {
                    volume->loadModelDrawMtx();
                    volume->drawShape();
                }
            }
        }
        (void)subtract;
        return allSubmitted;
    }
#endif
}

bool ShadowVolumeDrawer::isBatchEnabled() {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    if (sShadowVolumeBatchMarker < 0) {
        sShadowVolumeBatchMarker = sms_wiiu_sd_marker("nosvbatch") == 0;
    }
    return sShadowVolumeBatchMarker != 0;
#else
    return false;
#endif
}

const ShadowVolumeModel* ShadowVolumeDrawer::getShadowVolumeModel() const {
    return 0;
}

void ShadowVolumeDrawer::drawBatch(NameObj* const* pObjects, u32 count) {
#if defined(TARGET_WIIU) && defined(PORT_GAME_SMG)
    u32 batchState = 0;
    bool haveBatchState = false;
    for (u32 i = 0; i < count; i++) {
        ShadowVolumeDrawer* volume = static_cast< ShadowVolumeDrawer* >(pObjects[i]);
        if (!volume->isShadowVolumeBatchable()) {
            for (u32 j = 0; j < count; j++) {
                static_cast< ShadowVolumeDrawer* >(pObjects[j])->draw();
            }
            return;
        }

        u32 state = volume->getShadowVolumeBatchState();
        if (!haveBatchState) {
            batchState = state;
            haveBatchState = true;
        } else if (state != batchState) {
            // A state change could make the two-pass reorder observable. Keep
            // the retail object-by-object order for mixed-state extensions.
            for (u32 j = 0; j < count; j++) {
                static_cast< ShadowVolumeDrawer* >(pObjects[j])->draw();
            }
            return;
        }
    }

    GXColor shapeColor = reinterpret_cast< GXColor& >(::sShapeColor);
    GXSetTevColor(GX_TEVREG0, shapeColor);
    GXSetColorUpdate(0);
    GXSetDstAlpha(0, 0);

    if (sShadowVolumeInstancedMarker < 0)
        sShadowVolumeInstancedMarker = sms_wiiu_sd_marker("svinst") != 0;
    if (sShadowVolumeInstancedMarker && count > 1) {
        sms_port_heap_enter();
        ShadowVolumeInstancedGroup* groups = new ShadowVolumeInstancedGroup[count];
        ShadowVolumeInstancedMember* members = new ShadowVolumeInstancedMember[count];
        bool* eligible = new bool[count];
        sms_port_heap_leave();
        u32 groupCount = 0;
        bool haveMultiVolumeGroup = false;
        if (groups && members && eligible) {
            memset(eligible, 0, sizeof(bool) * count);
            for (u32 i = 0; i < count; ++i) {
                ShadowVolumeInstancedMember member;
                if (!prepareShadowVolumeInstancedMember(
                        static_cast<ShadowVolumeDrawer*>(pObjects[i]), &member)) continue;
                eligible[i] = true;
                const J3DModelData* modelData = member.mVolume->getShadowVolumeModel()->getModelData();
                u32 groupNo = 0;
                for (; groupNo < groupCount; ++groupNo)
                    if (groups[groupNo].mModelData == modelData) break;
                if (groupNo == groupCount) {
                    groups[groupCount].mModelData = modelData;
                    groups[groupCount].mFirst = 0;
                    groups[groupCount].mCount = 0;
                    groups[groupCount].mNext = 0;
                    ++groupCount;
                }
                ++groups[groupNo].mCount;
            }
            u32 memberOffset = 0;
            for (u32 groupNo = 0; groupNo < groupCount; ++groupNo) {
                groups[groupNo].mFirst = memberOffset;
                groups[groupNo].mNext = memberOffset;
                memberOffset += groups[groupNo].mCount;
                haveMultiVolumeGroup = haveMultiVolumeGroup || groups[groupNo].mCount > 1;
            }
            if (haveMultiVolumeGroup) {
                for (u32 i = 0; i < count; ++i) {
                    if (!eligible[i]) continue;
                    ShadowVolumeInstancedMember member;
                    if (!prepareShadowVolumeInstancedMember(
                            static_cast<ShadowVolumeDrawer*>(pObjects[i]), &member)) continue;
                    for (u32 groupNo = 0; groupNo < groupCount; ++groupNo) {
                        if (sameShadowVolumeInstancedModel(groups[groupNo], member)) {
                            members[groups[groupNo].mNext++] = member;
                            break;
                        }
                    }
                }
                setShadowVolumePassState(false);
                drawShadowVolumeInstancedPass(groups, groupCount, members, false);
                for (u32 i = 0; i < count; ++i) {
                    if (eligible[i]) continue;
                    ShadowVolumeDrawer* volume = static_cast<ShadowVolumeDrawer*>(pObjects[i]);
                    if (volume->isDraw()) {
                        volume->loadModelDrawMtx();
                        volume->drawShape();
                    }
                }
                setShadowVolumePassState(true);
                drawShadowVolumeInstancedPass(groups, groupCount, members, true);
                for (u32 i = 0; i < count; ++i) {
                    if (eligible[i]) continue;
                    ShadowVolumeDrawer* volume = static_cast<ShadowVolumeDrawer*>(pObjects[i]);
                    if (volume->isDraw()) {
                        volume->loadModelDrawMtx();
                        volume->drawShape();
                    }
                }
                sms_port_heap_enter();
                delete[] eligible;
                delete[] members;
                delete[] groups;
                sms_port_heap_leave();
                GXSetCullMode(GX_CULL_NONE);
                GXSetColorUpdate(1);
                GXSetDstAlpha(1, 0);
                return;
            }
        }
        sms_port_heap_enter();
        delete[] eligible;
        delete[] members;
        delete[] groups;
        sms_port_heap_leave();
    }

    if (sShadowVolumeMergeMarker < 0)
        sShadowVolumeMergeMarker = sms_wiiu_sd_marker("svmerge") != 0;
    ShadowVolumeDrawer** volumes = 0;
    ShadowVolumeMergeInfo* infos = 0;
    u32 infoCount = 0;
    u32 mergedVertices = 0;
    const bool tryMerge = sShadowVolumeMergeMarker && sms_wiiu_jpa_finalvx_enabled();
    // [svmerge] counters per 300 batches: drawn volumes, no model / no cache / matrix prep failures, merged, failed submits.
    static u32 sSvBatches, sSvDrawn, sSvNoModel, sSvNoCache, sSvPrep, sSvMerged, sSvSubmitFail;
    if (sShadowVolumeMergeMarker && ++sSvBatches == 300) {
        port_log("[svmerge] finalvx=%d per300: drawn=%u nomodel=%u nocache=%u prep=%u merged=%u submitfail=%u\n",
                 sms_wiiu_jpa_finalvx_enabled(), sSvDrawn, sSvNoModel, sSvNoCache, sSvPrep, sSvMerged, sSvSubmitFail);
        sSvBatches = sSvDrawn = sSvNoModel = sSvNoCache = sSvPrep = sSvMerged = sSvSubmitFail = 0;
    }
    if (tryMerge) {
        sms_port_heap_enter();
        volumes = new ShadowVolumeDrawer*[count];
        infos = new ShadowVolumeMergeInfo[count];
        sms_port_heap_leave();
        if (volumes && infos) {
            for (u32 i = 0; i < count; ++i) {
                volumes[i] = static_cast<ShadowVolumeDrawer*>(pObjects[i]);
                if (!volumes[i]->isDraw()) continue;
                ++sSvDrawn;
                const ShadowVolumeModel* model = volumes[i]->getShadowVolumeModel();
                if (!model) { ++sSvNoModel; continue; }
                ShadowVolumeCachedModel* cache = cacheShadowVolumeModel(model->getModelData());
                if (!cache) { ++sSvNoCache; continue; }
                ShadowVolumeMergeInfo& info = infos[infoCount];
                info.mVolume = volumes[i];
                info.mModel = model;
                info.mCache = cache;
                if (!prepareShadowVolumeMergeInfo(&info) ||
                    cache->mTriangleCount > (UINT32_MAX - mergedVertices) / 3u) { ++sSvPrep; continue; }
                ++sSvMerged;
                mergedVertices += cache->mTriangleCount * 3u;
                ++infoCount;
            }
        }
    }

    const bool haveMerge = volumes && infos && infoCount != 0 && mergedVertices != 0;
    if (haveMerge) {
        setShadowVolumePassState(false);
        SmsWiiuJpaFinalVxDesc* front = allocShadowVolumeFinalVx(mergedVertices);
        bool frontSubmitted = front && fillShadowVolumeFinalVx(infos, infoCount, front);
        if (frontSubmitted) {
            Mtx identity;
            PSMTXIdentity(identity);
            GXLoadPosMtxImm(identity, GX_PNMTX0);
            GXSetCurrentMtx(GX_PNMTX0);
            frontSubmitted = sms_wiiu_jpa_finalvx_submit(front);
        }
        if (!frontSubmitted) {
            ++sSvSubmitFail;
            setShadowVolumePassState(false);
            drawShadowVolumePass(volumes, count, false, infos, infoCount);
            setShadowVolumePassState(true);
            drawShadowVolumePass(volumes, count, false, infos, infoCount);
        } else {
            // Keep unsupported models on the old per-volume path in this pass.
            drawShadowVolumePass(volumes, count, true, infos, infoCount);
            setShadowVolumePassState(true);
            SmsWiiuJpaFinalVxDesc* back = allocShadowVolumeFinalVx(mergedVertices);
            bool backSubmitted = back && fillShadowVolumeFinalVx(infos, infoCount, back);
            if (backSubmitted) {
                Mtx identity;
                PSMTXIdentity(identity);
                GXLoadPosMtxImm(identity, GX_PNMTX0);
                GXSetCurrentMtx(GX_PNMTX0);
                backSubmitted = sms_wiiu_jpa_finalvx_submit(back);
            }
            if (!backSubmitted) {
                drawShadowVolumePass(volumes, count, false, infos, infoCount);
            } else {
                drawShadowVolumePass(volumes, count, true, infos, infoCount);
            }
        }
    } else {
        setShadowVolumePassState(false);
        if (volumes && infos)
            drawShadowVolumePass(volumes, count, false, infos, infoCount);
        else {
            for (u32 i = 0; i < count; ++i) {
                ShadowVolumeDrawer* volume = static_cast<ShadowVolumeDrawer*>(pObjects[i]);
                if (volume->isDraw()) {
                    volume->loadModelDrawMtx();
                    volume->drawShape();
                }
            }
        }
        setShadowVolumePassState(true);
        if (volumes && infos)
            drawShadowVolumePass(volumes, count, false, infos, infoCount);
        else {
            for (u32 i = 0; i < count; ++i) {
                ShadowVolumeDrawer* volume = static_cast<ShadowVolumeDrawer*>(pObjects[i]);
                if (volume->isDraw()) {
                    volume->loadModelDrawMtx();
                    volume->drawShape();
                }
            }
        }
    }

    sms_port_heap_enter();
    delete[] infos;
    delete[] volumes;
    sms_port_heap_leave();

    GXSetCullMode(GX_CULL_NONE);
    GXSetColorUpdate(1);
    GXSetDstAlpha(1, 0);
#else
    for (u32 i = 0; i < count; i++) {
        static_cast< ShadowVolumeDrawer* >(pObjects[i])->draw();
    }
#endif
}

void ShadowVolumeDrawer::draw() const {
    if (isDraw()) {
        loadModelDrawMtx();
        GXColor shapeColor = reinterpret_cast< GXColor& >(::sShapeColor);
        GXSetTevColor(GX_TEVREG0, shapeColor);
        GXSetColorUpdate(0);
        GXSetDstAlpha(0, 0);
        GXSetCullMode(GX_CULL_FRONT);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_COPY);
        drawShape();
        GXSetCullMode(GX_CULL_BACK);
        GXSetBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_COPY);
        drawShape();
        GXSetCullMode(GX_CULL_NONE);
        GXSetColorUpdate(1);
        GXSetDstAlpha(1, 0);
    }
}

ShadowVolumeDrawInit::~ShadowVolumeDrawInit() {
}
