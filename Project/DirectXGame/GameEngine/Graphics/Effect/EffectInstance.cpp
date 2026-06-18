#include "EffectInstance.h"
#include "GPUParticleManager.h"
#include "LightManager.h"
#include "PrimitivePipeline.h"
#include "SoundManager.h"
#include "MathUtility.h"
#include "Quaternion.h"
#include <algorithm>
#include <random>
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

    // [-r, r] の一様乱数
    float RandSym(float r) {
        if (r <= 0.0f) return 0.0f;
        static std::mt19937 rng{ std::random_device{}() };
        std::uniform_real_distribution<float> dist(-r, r);
        return dist(rng);
    }

    // オイラー角(radian, Z*Y*X 順)→クオータニオン。MakeRotateMatrix(Vector3) と同じ合成順に合わせる。
    Quaternion EulerToQuat(const Vector3& e) {
        Quaternion qx = MakeRotateAxisAngleQuaternion({ 1.0f, 0.0f, 0.0f }, e.x);
        Quaternion qy = MakeRotateAxisAngleQuaternion({ 0.0f, 1.0f, 0.0f }, e.y);
        Quaternion qz = MakeRotateAxisAngleQuaternion({ 0.0f, 0.0f, 1.0f }, e.z);
        // 行列が Z*Y*X（Xが最初に適用）なので、点に対し q = qz * qy * qx
        return Multiply(Multiply(qz, qy), qx);
    }

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

    // ===== SyncFromDef 用：プリミティブの「メッシュ再生成が要る変更か」を判定するための比較 =====
    bool V3Eq(const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
    bool V4Eq(const Vector4& a, const Vector4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

    bool RingEq(const PrimitiveGenerator::RingParams& a, const PrimitiveGenerator::RingParams& b) {
        return a.outerRadius == b.outerRadius && a.innerRadius == b.innerRadius && a.divisions == b.divisions
            && V4Eq(a.innerColor, b.innerColor) && V4Eq(a.outerColor, b.outerColor)
            && a.startAngle == b.startAngle && a.endAngle == b.endAngle
            && a.uvHorizon == b.uvHorizon && a.fadeStart == b.fadeStart && a.fadeEnd == b.fadeEnd
            && a.outerRadiusPerDivision == b.outerRadiusPerDivision;
    }
    bool CylEq(const PrimitiveGenerator::CylinderParams& a, const PrimitiveGenerator::CylinderParams& b) {
        return a.topRadius == b.topRadius && a.bottomRadius == b.bottomRadius && a.height == b.height
            && a.divisions == b.divisions && V4Eq(a.topColor, b.topColor) && V4Eq(a.bottomColor, b.bottomColor)
            && a.startAngle == b.startAngle && a.endAngle == b.endAngle;
    }
    bool HelixEq(const PrimitiveGenerator::HelixParams& a, const PrimitiveGenerator::HelixParams& b) {
        return a.startHelixRadius == b.startHelixRadius && a.endHelixRadius == b.endHelixRadius
            && a.startTubeRadius == b.startTubeRadius && a.endTubeRadius == b.endTubeRadius
            && a.pitch == b.pitch && a.turns == b.turns
            && a.circleSegments == b.circleSegments && a.lengthSegments == b.lengthSegments
            && V4Eq(a.startColor, b.startColor) && V4Eq(a.endColor, b.endColor);
    }
    bool AppEq(const PrimitiveGenerator::BeamAppearance& a, const PrimitiveGenerator::BeamAppearance& b) {
        return a.startWidth == b.startWidth && a.endWidth == b.endWidth && a.planeCount == b.planeCount
            && a.fadeStartLength == b.fadeStartLength && a.fadeEndLength == b.fadeEndLength
            && V4Eq(a.startColor, b.startColor) && V4Eq(a.endColor, b.endColor)
            && a.uvWrapByLength == b.uvWrapByLength && a.uvTilesPerUnit == b.uvTilesPerUnit;
    }
    bool BeamEq(const PrimitiveGenerator::BeamParams& a, const PrimitiveGenerator::BeamParams& b) {
        return V3Eq(a.startPos, b.startPos) && V3Eq(a.endPos, b.endPos)
            && AppEq(a.appearance, b.appearance) && a.lengthSegments == b.lengthSegments;
    }
    bool LightningEq(const PrimitiveGenerator::LightningBoltParams& a, const PrimitiveGenerator::LightningBoltParams& b) {
        return V3Eq(a.startPos, b.startPos) && V3Eq(a.endPos, b.endPos) && AppEq(a.appearance, b.appearance)
            && a.generations == b.generations && a.maxOffsetRatio == b.maxOffsetRatio && a.randomSeed == b.randomSeed
            && a.branchProbability == b.branchProbability && a.branchLengthScale == b.branchLengthScale
            && a.branchMaxAngle == b.branchMaxAngle && a.branchWidthScale == b.branchWidthScale
            && a.branchColorScale == b.branchColorScale;
    }

    // メッシュそのものを作り直す必要がある変更か（meshType / テクスチャ / 該当ジオメトリparams）。
    // blend / billboard / material / UV / distortion はレンダラの setter でライブ適用できるので含めない。
    bool PrimitiveMeshNeedsRebuild(const EffectPrimitiveComponent& a, const EffectPrimitiveComponent& b) {
        if (a.meshType != b.meshType) return true;
        if (a.texturePath != b.texturePath) return true;
        switch (a.meshType) {
        case 3: return !RingEq(a.ringParams, b.ringParams);       // Ring
        case 4: return !CylEq(a.cylinderParams, b.cylinderParams);// Cylinder
        case 5: return !HelixEq(a.helixParams, b.helixParams);    // Helix
        case 6: return !BeamEq(a.beamParams, b.beamParams);       // Beam
        case 7: return !LightningEq(a.lightningParams, b.lightningParams); // Lightning
        default: return false; // Plane/Box/Sphere は meshType + texture のみ
        }
    }

    // メッシュ再生成不要な静的パラメータ（blend / billboard / material / UV / distortion）を
    // 既存レンダラへ適用する。spawn 時と SyncFromDef のライブ反映で共用する。
    void ApplyPrimitiveStaticParams(EffectPrimitiveRenderer& r, const EffectPrimitiveComponent& pc) {
        r.SetBlendMode(static_cast<PrimitivePipeline::BlendMode>(pc.blendMode));
        r.SetBillboardMode(pc.billboardMode);
        r.SetDepthWrite(pc.depthWrite);
        r.SetAlphaReference(pc.alphaReference);
        r.SetCullBackface(pc.cullBackface);
        r.SetSamplerMode(pc.samplerMode);
        r.SetViewAngleFadePower(pc.viewAngleFadePower);
        r.SetUVScroll(pc.uvAutoScroll ? pc.uvScrollSpeed : Vector2{ 0.0f, 0.0f });
        r.SetUVOffset(pc.uvOffset);
        r.SetUVScale(pc.uvScale);
        r.SetUVFlipU(pc.uvFlipU);
        r.SetUVFlipV(pc.uvFlipV);
        // Distortion：useDistortion かつテクスチャ指定があれば反映、なければリセット（空文字で OFF）。
        if (pc.useDistortion && !pc.distortionTexturePath.empty()) {
            r.SetDistortionTexture(pc.distortionTexturePath);
            r.SetDistortionStrength(pc.distortionStrength);
            r.SetDistortionUVScroll(pc.distortionUvAutoScroll ? pc.distortionUvScrollSpeed : Vector2{ 0.0f, 0.0f });
            r.SetDistortionUVOffset(pc.distortionUvOffset);
            r.SetDistortionUVScale(pc.distortionUvScale);
            r.SetDistortionUVFlipU(pc.distortionUvFlipU);
            r.SetDistortionUVFlipV(pc.distortionUvFlipV);
        } else {
            r.SetDistortionTexture("");
        }
        // Dissolve：マスク反映（有効かつパスありのときのみ。閾値は毎フレーム別途 push する）。
        if (pc.useDissolve && !pc.dissolveMaskPath.empty()) {
            r.SetDissolveMask(pc.dissolveMaskPath);
        } else {
            r.SetDissolveMask("");
        }
        // アウトライン（時間非依存なのでここで静的に反映）
        r.SetDissolveEdge(pc.dissolveEdgeEnable, pc.dissolveEdgeColor, pc.dissolveEdgeWidth);
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

std::string EffectInstance::EffGroupName(const std::string& name) const {
    if (name.empty() || !isPreview_) return name;
    return std::string(GPUParticleManager::kPreviewPrefix) + name;
}

void EffectInstance::CollectPreviewGroupNames(std::vector<std::string>& out) const {
    if (!isPreview_) return;
    for (const auto& pc : def_.particles) {
        if (pc.gpuParticleGroupName.empty()) continue;
        out.push_back(EffGroupName(pc.gpuParticleGroupName));
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

            // 向きの初期化：基準 rotate（× 出現時ランダム）をクオータニオンで合成。
            Quaternion baseQ = EulerToQuat(pc.rotate);
            if (pc.randomRotateOnSpawn) {
                Quaternion randQ = EulerToQuat({ RandSym(pc.randomRotateRange.x),
                                                 RandSym(pc.randomRotateRange.y),
                                                 RandSym(pc.randomRotateRange.z) });
                baseQ = Multiply(baseQ, randQ);
            }
            rt.orientation = Normalize(baseQ);

            // blend / billboard / material / UV / distortion（SyncFromDef のライブ反映と共用）
            ApplyPrimitiveStaticParams(*rt.renderer, pc);
            rt.started = true;
        }

        // 寿命は Effect の totalDuration でクランプ（Effect 終了後は残らない）
        float maxLife = max(0.0001f, def_.totalDuration - pc.startTime);
        float life = pc.lifetime > 0.0001f ? pc.lifetime : 0.0001f;
        if (life > maxLife) life = maxLife;

        float t = Saturate(local / life);
        Vector3 scale = LerpV3(pc.startScale, pc.endScale, t);
        Vector4 color = LerpV4(pc.startColor, pc.endColor, t);

        // 持続回転：角速度ベクトル rotateSpeed を毎フレーム積分（クオータニオン合成。ジンバルロックなし）。
        {
            const Vector3& w = pc.rotateSpeed;
            float wlen = std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z);
            if (wlen > 1e-6f) {
                float ang = wlen * deltaTime;
                Quaternion dq = MakeRotateAxisAngleQuaternion({ w.x / wlen, w.y / wlen, w.z / wlen }, ang);
                rt.orientation = Normalize(Multiply(dq, rt.orientation));
            }
        }

        // エフェクト全体の向き worldRotation_ を適用：
        // offset をその回転で回し、Primitive の向きにも合成する（弾の進行方向追従など）。
        const bool hasWorldRot = (worldRotation_.x != 0.0f || worldRotation_.y != 0.0f || worldRotation_.z != 0.0f);
        Vector3 offset = pc.offset;
        Quaternion finalQ = rt.orientation;
        if (hasWorldRot) {
            Matrix4x4 rotMat = MakeRotateMatrix(worldRotation_);
            offset = TransformMatrix(pc.offset, rotMat);
            finalQ = Multiply(EulerToQuat(worldRotation_), rt.orientation);
        }
        rt.renderer->SetRotateQuaternion(finalQ);
        Vector3 pos = { worldPos_.x + offset.x, worldPos_.y + offset.y, worldPos_.z + offset.z };

        rt.renderer->SetTranslate(pos);
        rt.renderer->SetScale(scale);
        rt.renderer->SetColor(color);

        // ディゾルブ閾値：コンポーネント出現(local)を基準に、In(出現:1→0) と Out(消滅:0→1) を
        // max で時間合成する（0=完全表示 / 1=完全に消えた）。マスク未設定や In/Out 両方OFFなら無効。
        {
            float th = 0.0f;
            if (pc.dissolveInEnable) {
                float t = local - pc.dissolveInStartTime;
                float d = pc.dissolveInDuration > 0.0001f ? pc.dissolveInDuration : 0.0001f;
                float inTh = (t <= 0.0f) ? 1.0f : (t >= d ? 0.0f : 1.0f - t / d);
                if (inTh > th) th = inTh;
            }
            if (pc.dissolveOutEnable) {
                float t = local - pc.dissolveOutStartTime;
                float d = pc.dissolveOutDuration > 0.0001f ? pc.dissolveOutDuration : 0.0001f;
                float outTh = (t <= 0.0f) ? 0.0f : (t >= d ? 1.0f : t / d);
                if (outTh > th) th = outTh;
            }
            bool active = pc.useDissolve && !pc.dissolveMaskPath.empty()
                       && (pc.dissolveInEnable || pc.dissolveOutEnable);
            rt.renderer->SetDissolve(active, th);
        }

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

        // プレビューインスタンスならグループ名にプレフィックスを付け、シーンと物理バッファを分離する。
        const std::string groupName = EffGroupName(pc.gpuParticleGroupName);

        // orbit 有効時、発生平面(ringNormal)を tumble と同じ回転で回す（帯が首を振り、発生と粒子が同期）。
        // spin（帯上の流れ）の軸＝この「現在のリング法線」。
        Vector3 effRingNormal = pc.ringNormal;
        if (pc.orbitEnabled) {
            effRingNormal = RotateAroundAxis(pc.ringNormal, pc.orbitTumbleAxis, pc.orbitTumbleSpeed * orbitClock_);
        }

        // 周回（orbit）は burst 後も毎フレーム更新（中心はエフェクト位置＋offset に追従）。
        if (gpu_->HasGroup(groupName)) {
            const Vector3 center = { worldPos_.x + pc.offset.x, worldPos_.y + pc.offset.y, worldPos_.z + pc.offset.z };
            gpu_->SetGroupOrbit(groupName, pc.orbitEnabled, center,
                                effRingNormal, pc.orbitSpinSpeed,        // spin：帯上を流れる（現在のリング法線まわり）
                                pc.orbitTumbleAxis, pc.orbitTumbleSpeed); // tumble：帯自体の回転
            if (pc.orbitEnabled) {
                gpu_->SetEmitterShape(groupName, pc.emitShape, effRingNormal, pc.ringThickness);
            }
        }

        if (rt.burstFired) continue;
        if (elapsedTime_ < pc.startTime) continue;

        // グループ名が指定されていて未登録なら、texturePath で自動生成（エディタで完結できるように）
        if (!groupName.empty() && !gpu_->HasGroup(groupName)) {
            gpu_->CreateGroup(groupName,
                pc.texturePath.empty() ? "Resources/Textures/circle.dds" : pc.texturePath);
        }

        // 1回だけバースト発射（duration は将来の拡張用、現状未使用）
        if (gpu_->HasGroup(groupName)) {
            gpu_->SetGroupBillboardMode(groupName, pc.billboardMode);
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
                gpu_->SetEmitterVelocity(groupName, v, pc.velocityJitter, 1);
            } else if (pc.velocityMode == 2) {
                gpu_->SetEmitterVelocity(groupName, { pc.velocitySpeed, 0.0f, 0.0f }, pc.velocityJitter, 2);
            } else if (pc.velocityMode == 3) {
                // 接線（公転）：speed を baseVelocity.x に。方向はシェーダが ringNormal から接線を計算
                gpu_->SetEmitterVelocity(groupName, { pc.velocitySpeed, 0.0f, 0.0f }, pc.velocityJitter, 3);
            } else {
                gpu_->SetEmitterVelocity(groupName, { 0.0f, 0.0f, 0.0f }, 0.0f, 0);
            }

            // 発生形状（Sphere / Ring）。orbit 時は回転済みの法線で発生させ、粒子と同期させる。
            gpu_->SetEmitterShape(groupName, pc.emitShape, effRingNormal, pc.ringThickness);

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
                gpu_->SetEmitterGradient(groupName, keys);
            } else {
                gpu_->SetEmitterGradient(groupName, {});
            }

            // 粒子寿命：ループ時は周期を超えて生き残らせる（境界で全消えしないように）。
            // 非ループ時のみ totalDuration を超えないようにクランプ。
            float particleLife = pc.particleLifeTime;
            if (!def_.loop) {
                float remaining = max(0.0001f, def_.totalDuration - pc.startTime);
                particleLife = min(particleLife, remaining);
            }
            Vector3 rotRange = pc.randomRotateOnSpawn ? pc.randomRotateRange : Vector3{ 0.0f, 0.0f, 0.0f };
            gpu_->BurstEmit(groupName, pos, pc.burstCount, pc.emitRadius,
                            static_cast<uint32_t>(pc.colorMode), pc.startColor, pc.endColor,
                            pc.scaleMin, pc.scaleMax, pc.uniformScale, particleLife,
                            pc.startScale, pc.endScale,
                            rotRange, pc.rotateSpeed);
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

void EffectInstance::SyncFromDef(const EffectDef& newDef) {
    // ----- Primitive -----
    if (newDef.primitives.size() != def_.primitives.size()) {
        // 数が変わった（パーツ追加/削除）→ index ずれ事故を避けるため、この型の runtime を作り直す。
        for (auto& rt : primitives_) rt.renderer.reset();
        primitives_.clear();
        primitives_.resize(newDef.primitives.size());
    } else {
        // 数が同じ → started 済みは、メッシュ変更なら部分リビルド、それ以外はライブ setter で反映。
        for (size_t i = 0; i < newDef.primitives.size(); ++i) {
            PrimitiveRuntime& rt = primitives_[i];
            if (!rt.started || !rt.renderer) continue; // 未spawnは新 def_ から自然に生成される
            if (PrimitiveMeshNeedsRebuild(def_.primitives[i], newDef.primitives[i])) {
                rt.renderer.reset();
                rt.started = false;   // 次 Update で新メッシュとして再生成（elapsedTime_ は保持）
                rt.finished = false;
            } else {
                ApplyPrimitiveStaticParams(*rt.renderer, newDef.primitives[i]);
            }
        }
    }

    // ----- Particle -----
    if (newDef.particles.size() != def_.particles.size()) {
        particles_.clear();
        particles_.resize(newDef.particles.size());
    }

    // ----- Light（確保中スロットを解放してから作り直す）-----
    if (newDef.lights.size() != def_.lights.size()) {
        LightManager* lm = LightManager::GetInstance();
        for (auto& rt : lights_) {
            if (rt.slot != kInvalidLightSlot) {
                if (rt.isSpot) lm->ReleaseSpotLight(rt.slot);
                else           lm->ReleasePointLight(rt.slot);
            }
        }
        lights_.clear();
        lights_.resize(newDef.lights.size());
        for (auto& l : lights_) l.slot = kInvalidLightSlot;
    }

    // ----- Sound（鳴動中を止めてから作り直す）-----
    if (newDef.sounds.size() != def_.sounds.size()) {
        SoundManager* sm = SoundManager::GetInstance();
        for (auto& rt : sounds_) {
            if (rt.handle != 0) sm->Stop3DSound(rt.handle);
        }
        sounds_.clear();
        sounds_.resize(newDef.sounds.size());
    }

    // 新 def を採用。毎フレーム読みの値（scale/color/timing/offset 等）はこれで即ライブ反映される。
    def_ = newDef;
}

void EffectInstance::Rewind() {
    ResetForLoop();          // ライト/サウンド解放・runtime 片付け・elapsedTime_=0
    orbitClock_ = 0.0f;
    finished_ = false;
    stopRequested_ = false;
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
