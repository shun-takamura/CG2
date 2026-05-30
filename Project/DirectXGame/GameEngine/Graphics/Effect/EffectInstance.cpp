#include "EffectInstance.h"
#include "GPUParticleManager.h"
#include "LightManager.h"
#include "PrimitivePipeline.h"
#include "SoundManager.h"
#include "MathUtility.h"
#include <algorithm>
#define NOMINMAX
#include<cmath>

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

    // axis まわりに v を ang 回転（ロドリゲスの回転公式）
    Vector3 RotateAroundAxis(const Vector3& v, const Vector3& axis, float ang) {
        float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        if (len < 1e-5f) return v;
        Vector3 k = { axis.x / len, axis.y / len, axis.z / len };
        float c = std::cos(ang), s = std::sin(ang);
        float kv = k.x * v.x + k.y * v.y + k.z * v.z;
        Vector3 cr = { k.y * v.z - k.z * v.y, k.z * v.x - k.x * v.z, k.x * v.y - k.y * v.x };
        return {
            v.x * c + cr.x * s + k.x * kv * (1.0f - c),
            v.y * c + cr.y * s + k.y * kv * (1.0f - c),
            v.z * c + cr.z * s + k.z * kv * (1.0f - c),
        };
    }
}

void EffectInstance::Initialize(const EffectDef& def, const Vector3& worldPos, GPUParticleManager* gpu) {
    def_ = def;
    worldPos_ = worldPos;
    elapsedTime_ = 0.0f;
    finished_ = false;
    gpu_ = gpu;

    primitives_.resize(def_.primitives.size());
    particles_.resize(def_.particles.size());
    lights_.resize(def_.lights.size());
    sounds_.resize(def_.sounds.size());
    for (auto& l : lights_) {
        l.slot = kInvalidLightSlot;
    }
}

void EffectInstance::Update(Camera* camera, float deltaTime) {
    elapsedTime_ += deltaTime;
    orbitClock_  += deltaTime; // ループでリセットしない（GPU orbit と同じく連続）

    // ===== Primitive =====
    for (size_t i = 0; i < def_.primitives.size(); ++i) {
        const EffectPrimitiveComponent& pc = def_.primitives[i];
        PrimitiveRuntime& rt = primitives_[i];
        if (rt.finished) continue;

        float local = elapsedTime_ - pc.startTime;
        if (local < 0.0f) continue;

        // 開始：レンダラ生成
        if (!rt.started) {
            rt.renderer = std::make_unique<EffectPrimitiveRenderer>();
            rt.renderer->Initialize(pc.meshType, pc.texturePath,
                                    pc.ringParams, pc.cylinderParams, pc.helixParams, pc.beamParams,
                                    pc.lightningParams);
            rt.renderer->SetBlendMode(static_cast<PrimitivePipeline::BlendMode>(pc.blendMode));
            rt.renderer->SetBillboardMode(pc.billboardMode);
            rt.renderer->SetRotate(pc.rotate);

            // 静的マテリアル
            rt.renderer->SetDepthWrite(pc.depthWrite);
            rt.renderer->SetAlphaReference(pc.alphaReference);
            rt.renderer->SetCullBackface(pc.cullBackface);
            rt.renderer->SetSamplerMode(pc.samplerMode);
            rt.renderer->SetViewAngleFadePower(pc.viewAngleFadePower);
            // UV
            rt.renderer->SetUVScroll(pc.uvAutoScroll ? pc.uvScrollSpeed : Vector2{ 0.0f, 0.0f });
            rt.renderer->SetUVOffset(pc.uvOffset);
            rt.renderer->SetUVScale(pc.uvScale);
            rt.renderer->SetUVFlipU(pc.uvFlipU);
            rt.renderer->SetUVFlipV(pc.uvFlipV);

            // Distortion（useDistortion かつノーマルマップ指定がある場合のみ反映）
            if (pc.useDistortion && !pc.distortionTexturePath.empty()) {
                rt.renderer->SetDistortionTexture(pc.distortionTexturePath);
                rt.renderer->SetDistortionStrength(pc.distortionStrength);
                rt.renderer->SetDistortionUVScroll(pc.distortionUvAutoScroll ? pc.distortionUvScrollSpeed : Vector2{ 0.0f, 0.0f });
                rt.renderer->SetDistortionUVOffset(pc.distortionUvOffset);
                rt.renderer->SetDistortionUVScale(pc.distortionUvScale);
                rt.renderer->SetDistortionUVFlipU(pc.distortionUvFlipU);
                rt.renderer->SetDistortionUVFlipV(pc.distortionUvFlipV);
            }
            rt.started = true;
        }

        // 寿命は Effect の totalDuration でクランプ（Effect 終了後は残らない）
        float maxLife = max(0.0001f, def_.totalDuration - pc.startTime);
        float life = pc.lifetime > 0.0001f ? pc.lifetime : 0.0001f;
        if (life > maxLife) life = maxLife;

        float t = Saturate(local / life);
        Vector3 scale = LerpV3(pc.startScale, pc.endScale, t);
        Vector4 color = LerpV4(pc.startColor, pc.endColor, t);

        // エフェクト全体の向き worldRotation_ を適用：
        // offset をその回転で回し、Primitive の向きにも加算する（弾の進行方向追従など）。
        const bool hasWorldRot = (worldRotation_.x != 0.0f || worldRotation_.y != 0.0f || worldRotation_.z != 0.0f);
        Vector3 offset = pc.offset;
        if (hasWorldRot) {
            Matrix4x4 rotMat = MakeRotateMatrix(worldRotation_);
            offset = TransformMatrix(pc.offset, rotMat);
            rt.renderer->SetRotate({ worldRotation_.x + pc.rotate.x, worldRotation_.y + pc.rotate.y, worldRotation_.z + pc.rotate.z });
        }
        Vector3 pos = { worldPos_.x + offset.x, worldPos_.y + offset.y, worldPos_.z + offset.z };

        rt.renderer->SetTranslate(pos);
        rt.renderer->SetScale(scale);
        rt.renderer->SetColor(color);
        rt.renderer->Update(camera, deltaTime);

        // 寿命終了（totalDuration を超えた場合も含む）
        if (local >= life || elapsedTime_ >= def_.totalDuration) {
            rt.renderer.reset();
            rt.finished = true;
        }
    }

    // ===== Particle =====
    for (size_t i = 0; i < def_.particles.size(); ++i) {
        const EffectParticleComponent& pc = def_.particles[i];
        ParticleRuntime& rt = particles_[i];
        if (!gpu_) { rt.burstFired = true; continue; }

        // orbit 有効時、発生平面(ringNormal)を tumble と同じ回転で回す（帯が首を振り、発生と粒子が同期）。
        // spin（帯上の流れ）の軸＝この「現在のリング法線」。
        Vector3 effRingNormal = pc.ringNormal;
        if (pc.orbitEnabled) {
            effRingNormal = RotateAroundAxis(pc.ringNormal, pc.orbitTumbleAxis, pc.orbitTumbleSpeed * orbitClock_);
        }

        // 周回（orbit）は burst 後も毎フレーム更新（中心はエフェクト位置＋offset に追従）。
        if (gpu_->HasGroup(pc.gpuParticleGroupName)) {
            const Vector3 center = { worldPos_.x + pc.offset.x, worldPos_.y + pc.offset.y, worldPos_.z + pc.offset.z };
            gpu_->SetGroupOrbit(pc.gpuParticleGroupName, pc.orbitEnabled, center,
                                effRingNormal, pc.orbitSpinSpeed,        // spin：帯上を流れる（現在のリング法線まわり）
                                pc.orbitTumbleAxis, pc.orbitTumbleSpeed); // tumble：帯自体の回転
            if (pc.orbitEnabled) {
                gpu_->SetEmitterShape(pc.gpuParticleGroupName, pc.emitShape, effRingNormal, pc.ringThickness);
            }
        }

        if (rt.burstFired) continue;
        if (elapsedTime_ < pc.startTime) continue;

        // グループ名が指定されていて未登録なら、texturePath で自動生成（エディタで完結できるように）
        if (!pc.gpuParticleGroupName.empty() && !gpu_->HasGroup(pc.gpuParticleGroupName)) {
            gpu_->CreateGroup(pc.gpuParticleGroupName,
                pc.texturePath.empty() ? "Resources/Textures/circle.dds" : pc.texturePath);
        }

        // 1回だけバースト発射（duration は将来の拡張用、現状未使用）
        if (gpu_->HasGroup(pc.gpuParticleGroupName)) {
            gpu_->SetGroupBillboardMode(pc.gpuParticleGroupName, pc.billboardMode);
            Vector3 pos = { worldPos_.x + pc.offset.x, worldPos_.y + pc.offset.y, worldPos_.z + pc.offset.z };

            // 初速モード（0=ランダム / 1=方向固定 / 2=放射）。mode に応じた baseVelocity を渡す。
            if (pc.velocityMode == 1) {
                float len = std::sqrt(pc.velocityDir.x * pc.velocityDir.x +
                                      pc.velocityDir.y * pc.velocityDir.y +
                                      pc.velocityDir.z * pc.velocityDir.z);
                Vector3 v = (len > 1e-5f)
                    ? Vector3{ pc.velocityDir.x / len * pc.velocitySpeed,
                               pc.velocityDir.y / len * pc.velocitySpeed,
                               pc.velocityDir.z / len * pc.velocitySpeed }
                    : Vector3{ 0.0f, pc.velocitySpeed, 0.0f };
                gpu_->SetEmitterVelocity(pc.gpuParticleGroupName, v, pc.velocityJitter, 1);
            } else if (pc.velocityMode == 2) {
                gpu_->SetEmitterVelocity(pc.gpuParticleGroupName, { pc.velocitySpeed, 0.0f, 0.0f }, pc.velocityJitter, 2);
            } else if (pc.velocityMode == 3) {
                // 接線（公転）：speed を baseVelocity.x に。方向はシェーダが ringNormal から接線を計算
                gpu_->SetEmitterVelocity(pc.gpuParticleGroupName, { pc.velocitySpeed, 0.0f, 0.0f }, pc.velocityJitter, 3);
            } else {
                gpu_->SetEmitterVelocity(pc.gpuParticleGroupName, { 0.0f, 0.0f, 0.0f }, 0.0f, 0);
            }

            // 発生形状（Sphere / Ring）。orbit 時は回転済みの法線で発生させ、粒子と同期させる。
            gpu_->SetEmitterShape(pc.gpuParticleGroupName, pc.emitShape, effRingNormal, pc.ringThickness);

            // 多色グラデーション（Fixed カラーモード）。
            // Start(loc=0)/End(loc=1) を常に両端キーとして使い、colorKeys はその間に挿入する中間キー。
            // → 中間キー0個=Start→End の2色、1個=3色…（Random モードでは無効）。
            if (pc.colorMode == 1) {
                std::vector<std::pair<float, Vector4>> keys;
                keys.reserve(pc.colorKeys.size() + 2);
                keys.emplace_back(0.0f, pc.startColor);
                for (const auto& k : pc.colorKeys) keys.emplace_back(k.location, k.color);
                keys.emplace_back(1.0f, pc.endColor);
                std::sort(keys.begin(), keys.end(),
                    [](const std::pair<float, Vector4>& a, const std::pair<float, Vector4>& b) { return a.first < b.first; });
                gpu_->SetEmitterGradient(pc.gpuParticleGroupName, keys);
            } else {
                gpu_->SetEmitterGradient(pc.gpuParticleGroupName, {});
            }

            // 粒子寿命：ループ時は周期を超えて生き残らせる（境界で全消えしないように）。
            // 非ループ時のみ totalDuration を超えないようにクランプ。
            float particleLife = pc.particleLifeTime;
            if (!def_.loop) {
                float remaining = max(0.0001f, def_.totalDuration - pc.startTime);
                particleLife = min(particleLife, remaining);
            }
            gpu_->BurstEmit(pc.gpuParticleGroupName, pos, pc.burstCount, pc.emitRadius,
                            static_cast<uint32_t>(pc.colorMode), pc.startColor, pc.endColor,
                            pc.scaleMin, pc.scaleMax, pc.uniformScale, particleLife);
        }
        rt.burstFired = true;
    }

    // ===== Light =====
    LightManager* lm = LightManager::GetInstance();
    for (size_t i = 0; i < def_.lights.size(); ++i) {
        const EffectLightComponent& lc = def_.lights[i];
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

        // 寿命を totalDuration でクランプ
        float maxLife = max(0.0001f, def_.totalDuration - lc.startTime);
        float life = lc.lifetime > 0.0001f ? lc.lifetime : 0.0001f;
        if (life > maxLife) life = maxLife;

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

        // 寿命終了：解放（totalDuration 超過も含む）
        if (local >= life || elapsedTime_ >= def_.totalDuration) {
            if (rt.isSpot) {
                lm->ReleaseSpotLight(rt.slot);
            } else {
                lm->ReleasePointLight(rt.slot);
            }
            rt.slot = kInvalidLightSlot;
            rt.finished = true;
        }
    }

    // ===== Sound =====
    SoundManager* sm = SoundManager::GetInstance();
    for (size_t i = 0; i < def_.sounds.size(); ++i) {
        const EffectSoundComponent& sc = def_.sounds[i];
        SoundRuntime& rt = sounds_[i];
        if (rt.finished) continue;

        if (!rt.started) {
            if (elapsedTime_ < sc.startTime) continue;
            Vector3 pos = { worldPos_.x + sc.offset.x, worldPos_.y + sc.offset.y, worldPos_.z + sc.offset.z };
            rt.handle = sm->Play3DSound(sc.soundName, pos, { 0.0f, 0.0f, 0.0f }, sc.distanceScale);
            rt.started = true;
            if (rt.handle == 0) {
                // 再生失敗（音源未ロード等）。リトライしない。
                rt.finished = true;
                continue;
            }
        }

        // 毎フレーム位置を追従（エフェクトが動いていなくても、リスナーが動けば必要）
        if (rt.handle != 0) {
            Vector3 pos = { worldPos_.x + sc.offset.x, worldPos_.y + sc.offset.y, worldPos_.z + sc.offset.z };
            sm->UpdateEmitter(rt.handle, pos, { 0.0f, 0.0f, 0.0f });
        }
        // Effect の totalDuration を超えたら音を強制停止する（Effect の時間を絶対とする）
        if (rt.handle != 0 && elapsedTime_ >= def_.totalDuration) {
            sm->Stop3DSound(rt.handle);
            rt.handle = 0;
            rt.finished = true;
        }
    }

    // 外部からの停止要求：即終了させる
    if (stopRequested_) {
        Cleanup();
        finished_ = true;
        return;
    }

    // ループ：totalDuration を超えたら全 runtime を片付けて最初から再生し直す
    if (def_.loop && elapsedTime_ >= def_.totalDuration) {
        ResetForLoop();
        return;
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
    if (allDone) {
        for (auto& rt : sounds_) { if (!rt.started) { allDone = false; break; } }
    }
    if (allDone && elapsedTime_ >= def_.totalDuration) {
        finished_ = true;
    }
}

void EffectInstance::ResetForLoop() {
    // 現在確保中のライト・サウンドはここで解放（Primitive renderer は Update 内で
    // 自分の finished と共に reset 済みなので再生成される）
    LightManager* lm = LightManager::GetInstance();
    for (auto& rt : lights_) {
        if (rt.slot != kInvalidLightSlot) {
            if (rt.isSpot) lm->ReleaseSpotLight(rt.slot);
            else           lm->ReleasePointLight(rt.slot);
        }
        rt = LightRuntime{};
        rt.slot = kInvalidLightSlot;
    }
    SoundManager* sm = SoundManager::GetInstance();
    for (auto& rt : sounds_) {
        if (rt.handle != 0) {
            sm->Stop3DSound(rt.handle);
        }
        rt = SoundRuntime{};
    }
    for (auto& rt : primitives_) {
        rt.renderer.reset();
        rt.started = false;
        rt.finished = false;
    }
    for (auto& rt : particles_) {
        rt.burstFired = false;
    }
    elapsedTime_ = 0.0f;
}

void EffectInstance::Draw() {
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished) {
            rt.renderer->Draw();
        }
    }
}

void EffectInstance::DrawDistortionPass() {
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished && rt.renderer->HasDistortionTexture()) {
            rt.renderer->DrawDistortionPass();
        }
    }
}

void EffectInstance::DrawDistortionPassPreview() {
    for (auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished && rt.renderer->HasDistortionTexture()) {
            rt.renderer->DrawDistortionPassPreview();
        }
    }
}

bool EffectInstance::HasActiveDistortionSource() const {
    for (const auto& rt : primitives_) {
        if (rt.renderer && rt.started && !rt.finished && rt.renderer->HasDistortionTexture()) {
            return true;
        }
    }
    return false;
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
    // ライトのスロット予約はその場で返す（GPU描画が参照しないため安全）。
    LightManager* lm = LightManager::GetInstance();
    for (auto& rt : lights_) {
        if (rt.slot != kInvalidLightSlot) {
            if (rt.isSpot) lm->ReleaseSpotLight(rt.slot);
            else           lm->ReleasePointLight(rt.slot);
            rt.slot = kInvalidLightSlot;
            rt.finished = true;
        }
    }
    // 再生中の3D サウンドも停止
    SoundManager* sm = SoundManager::GetInstance();
    for (auto& rt : sounds_) {
        if (rt.handle != 0) {
            sm->Stop3DSound(rt.handle);
            rt.handle = 0;
        }
        rt.finished = true;
    }
    // Primitive renderer の破棄は EffectInstance のデストラクタ任せ。
    // この Cleanup() が ImGui コールバック等の Draw 中に呼ばれる可能性があるため、
    // renderer のリソース解放はマネージャ側で1フレーム遅延させる前提。
}
