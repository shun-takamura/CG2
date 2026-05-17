#include "EffectInstance.h"
#include "GPUParticleManager.h"
#include "LightManager.h"
#include "PrimitivePipeline.h"

namespace {
    // 0-1 にクランプ
    float Saturate(float v) {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }
    Vector3 LerpV3(const Vector3& a, const Vector3& b, float t) {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
        };
    }
    Vector4 LerpV4(const Vector4& a, const Vector4& b, float t) {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t,
        };
    }
    float LerpF(float a, float b, float t) { return a + (b - a) * t; }
}

void EffectInstance::Initialize(const EffectDef* def, const Vector3& worldPos, GPUParticleManager* gpu) {
    def_ = def;
    worldPos_ = worldPos;
    elapsedTime_ = 0.0f;
    finished_ = false;
    gpu_ = gpu;

    if (!def_) return;

    primitives_.resize(def_->primitives.size());
    particles_.resize(def_->particles.size());
    lights_.resize(def_->lights.size());
    for (auto& l : lights_) {
        l.slot = kInvalidLightSlot;
    }
}

void EffectInstance::Update(Camera* camera, float deltaTime) {
    if (!def_) { finished_ = true; return; }

    elapsedTime_ += deltaTime;

    // ===== Primitive =====
    for (size_t i = 0; i < def_->primitives.size(); ++i) {
        const EffectPrimitiveComponent& pc = def_->primitives[i];
        PrimitiveRuntime& rt = primitives_[i];
        if (rt.finished) continue;

        float local = elapsedTime_ - pc.startTime;
        if (local < 0.0f) continue;

        // 開始：レンダラ生成
        if (!rt.started) {
            rt.renderer = std::make_unique<EffectPrimitiveRenderer>();
            rt.renderer->Initialize(pc.meshType, pc.texturePath);
            rt.renderer->SetBlendMode(static_cast<PrimitivePipeline::BlendMode>(pc.blendMode));
            rt.renderer->SetBillboardMode(pc.billboardMode);
            rt.renderer->SetRotate(pc.rotate);
            rt.started = true;
        }

        // 補間
        float life = pc.lifetime > 0.0001f ? pc.lifetime : 0.0001f;
        float t = Saturate(local / life);
        Vector3 scale = LerpV3(pc.startScale, pc.endScale, t);
        Vector4 color = LerpV4(pc.startColor, pc.endColor, t);
        Vector3 pos = { worldPos_.x + pc.offset.x, worldPos_.y + pc.offset.y, worldPos_.z + pc.offset.z };

        rt.renderer->SetTranslate(pos);
        rt.renderer->SetScale(scale);
        rt.renderer->SetColor(color);
        rt.renderer->Update(camera, deltaTime);

        // 寿命終了
        if (local >= pc.lifetime) {
            rt.renderer.reset();
            rt.finished = true;
        }
    }

    // ===== Particle =====
    for (size_t i = 0; i < def_->particles.size(); ++i) {
        const EffectParticleComponent& pc = def_->particles[i];
        ParticleRuntime& rt = particles_[i];
        if (rt.burstFired) continue;
        if (!gpu_) { rt.burstFired = true; continue; }
        if (elapsedTime_ < pc.startTime) continue;

        // 1回だけバースト発射（duration は将来の拡張用、現状未使用）
        if (gpu_->HasGroup(pc.gpuParticleGroupName)) {
            gpu_->SetGroupBillboardMode(pc.gpuParticleGroupName, pc.billboardMode);
            Vector3 pos = { worldPos_.x + pc.offset.x, worldPos_.y + pc.offset.y, worldPos_.z + pc.offset.z };
            gpu_->BurstEmit(pc.gpuParticleGroupName, pos, pc.burstCount);
        }
        rt.burstFired = true;
    }

    // ===== Light =====
    LightManager* lm = LightManager::GetInstance();
    for (size_t i = 0; i < def_->lights.size(); ++i) {
        const EffectLightComponent& lc = def_->lights[i];
        LightRuntime& rt = lights_[i];
        if (rt.finished) continue;

        float local = elapsedTime_ - lc.startTime;
        if (local < 0.0f) continue;

        // 開始：スロット確保
        if (!rt.started) {
            if (lc.kind == EffectLightKind::Spot) {
                rt.slot = lm->AcquireSpotLight();
                rt.isSpot = true;
            } else {
                rt.slot = lm->AcquirePointLight();
                rt.isSpot = false;
            }
            rt.started = true;
            // スロット確保失敗時はそのまま終了扱い
            if (rt.slot == kInvalidLightSlot) {
                rt.finished = true;
                continue;
            }
        }

        if (rt.slot == kInvalidLightSlot) continue;

        // 補間
        float life = lc.lifetime > 0.0001f ? lc.lifetime : 0.0001f;
        float t = Saturate(local / life);
        float intensity = LerpF(lc.startIntensity, lc.endIntensity, t);
        Vector3 pos = { worldPos_.x + lc.offset.x, worldPos_.y + lc.offset.y, worldPos_.z + lc.offset.z };

        if (rt.isSpot) {
            lm->SetSpotLightColor(rt.slot, lc.color);
            lm->SetSpotLightPosition(rt.slot, pos);
            lm->SetSpotLightDirection(rt.slot, lc.direction);
            lm->SetSpotLightIntensity(rt.slot, intensity);
            lm->SetSpotLightDistance(rt.slot, lc.range);
            lm->SetSpotLightCosAngle(rt.slot, lc.spotCosAngle);
            lm->SetSpotLightCosFalloffStart(rt.slot, lc.spotCosFalloffStart);
        } else {
            lm->SetPointLightColor(rt.slot, lc.color);
            lm->SetPointLightPosition(rt.slot, pos);
            lm->SetPointLightIntensity(rt.slot, intensity);
            lm->SetPointLightRadius(rt.slot, lc.range);
        }

        // 寿命終了：解放
        if (local >= lc.lifetime) {
            if (rt.isSpot) {
                lm->ReleaseSpotLight(rt.slot);
            } else {
                lm->ReleasePointLight(rt.slot);
            }
            rt.slot = kInvalidLightSlot;
            rt.finished = true;
        }
    }

    // 全コンポーネント完了判定
    bool allDone = true;
    for (auto& rt : primitives_) { if (!rt.finished) { allDone = false; break; } }
    if (allDone) {
        for (auto& rt : particles_) { if (!rt.burstFired) { allDone = false; break; } }
    }
    if (allDone) {
        for (auto& rt : lights_) { if (!rt.finished) { allDone = false; break; } }
    }
    if (allDone && elapsedTime_ >= def_->totalDuration) {
        finished_ = true;
    }
}

void EffectInstance::Draw() {
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished) {
            rt.renderer->Draw();
        }
    }
}

void EffectInstance::UpdatePreviewWVP(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos) {
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished) {
            rt.renderer->UpdatePreviewWVP(viewMatrix, viewProjectionMatrix, cameraPos);
        }
    }
}

void EffectInstance::DrawPreview() {
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished) {
            rt.renderer->DrawPreview();
        }
    }
}

void EffectInstance::Cleanup() {
    // ライトのスロット予約だけはその場で返す（GPU描画が参照しないため安全）。
    // Primitive renderer の破棄は EffectInstance のデストラクタに任せる。
    // この Cleanup() が ImGui コールバック等の Draw 中に呼ばれる可能性があるため、
    // renderer のリソース解放はマネージャ側で1フレーム遅延させる前提。
    LightManager* lm = LightManager::GetInstance();
    for (auto& rt : lights_) {
        if (rt.slot != kInvalidLightSlot) {
            if (rt.isSpot) lm->ReleaseSpotLight(rt.slot);
            else           lm->ReleasePointLight(rt.slot);
            rt.slot = kInvalidLightSlot;
            rt.finished = true;
        }
    }
}
