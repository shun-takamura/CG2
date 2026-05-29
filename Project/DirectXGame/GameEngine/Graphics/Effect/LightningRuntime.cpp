#include "LightningRuntime.h"
#include "Primitive/PrimitivePipeline.h"

void LightningRuntime::Initialize(const PrimitiveGenerator::LightningBoltParams& templateParams,
                                  const std::string& texturePath,
                                  int blendMode) {
    params_ = templateParams;
    texturePath_ = texturePath;
    blendMode_ = blendMode;

    // 位相をずらすため、2本目の初期残寿命を半分にする（1本目=満タン、2本目=半分）
    bolts_[0].remaining = boltLifetime_;
    bolts_[1].remaining = boltLifetime_ - overlapOffset_;
    bolts_[0].initialized = false;
    bolts_[1].initialized = false;
    bolts_[0].renderer.reset();
    bolts_[1].renderer.reset();
    active_ = true;
}

void LightningRuntime::SetEndpoints(const Vector3& start, const Vector3& end) {
    startPos_ = start;
    endPos_ = end;
}

void LightningRuntime::SetViewAngleFadePower(float p) {
    viewAngleFadePower_ = p;
    for (auto& bolt : bolts_) {
        if (bolt.renderer) bolt.renderer->SetViewAngleFadePower(p);
    }
}

void LightningRuntime::Regenerate(BoltState& bolt) {
    // 始終点を現在値に差し替えてシード=0（=毎回ランダム）でメッシュ生成
    PrimitiveGenerator::LightningBoltParams p = params_;
    p.startPos = startPos_;
    p.endPos   = endPos_;
    p.randomSeed = 0;

    bolt.renderer = std::make_unique<EffectPrimitiveRenderer>();
    bolt.renderer->Initialize(7 /*Lightning*/, texturePath_, {}, {}, {}, {}, p);
    bolt.renderer->SetBlendMode(static_cast<PrimitivePipeline::BlendMode>(blendMode_));
    bolt.renderer->SetViewAngleFadePower(viewAngleFadePower_);
    bolt.initialized = true;
}

void LightningRuntime::Update(Camera* camera, float deltaTime) {
    if (!active_) return;

    for (auto& bolt : bolts_) {
        // 寿命を進める
        bolt.remaining -= deltaTime;
        if (bolt.remaining <= 0.0f || !bolt.initialized) {
            // 寿命切れ or 未初期化 → 再生成
            Regenerate(bolt);
            bolt.remaining = boltLifetime_;
        }
        // 通常 Update（UVスクロールや CB 書き込み）
        if (bolt.renderer) {
            bolt.renderer->Update(camera, deltaTime);
        }
    }
}

void LightningRuntime::Draw() {
    if (!active_) return;
    for (auto& bolt : bolts_) {
        if (bolt.renderer && bolt.initialized) {
            // 寿命が後半に入っているほど α を落とすことで「フェードアウト中の古い雷」を演出。
            // 簡易：寿命 1.0 → 0.5 で 1.0 から 0.0 へ線形フェード（half-life で消える）
            // ※ Update で color CB を毎フレ更新しているので、ここでは SetColor で被せても OK
            float halfLife = boltLifetime_ * 0.5f;
            float fade = 1.0f;
            if (halfLife > 0.0001f && bolt.remaining < halfLife) {
                fade = bolt.remaining / halfLife;
                if (fade < 0.0f) fade = 0.0f;
            }
            // 元の頂点αは既に焼き込まれている。ここでは MaterialColor の α を fade に。
            // bolt.renderer->SetColor は SetTranslate/Scale と並ぶ動的設定
            // ※ Material.color が頂点αと乗算されるため、ここでフェードを反映できる
            // 注: 既存 EffectPrimitiveRenderer に SetColor が無ければ追加が必要
            // → ある（SetColor(Vector4) 経由で mesh_.SetColor）
            bolt.renderer->SetColor({ 1.0f, 1.0f, 1.0f, fade });
            bolt.renderer->Draw();
        }
    }
}
