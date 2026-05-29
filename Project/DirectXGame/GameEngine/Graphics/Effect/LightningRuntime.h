#pragma once
#include "EffectPrimitiveRenderer.h"
#include "Primitive/PrimitiveGenerator.h"
#include "Vector3.h"
#include <memory>
#include <string>

class Camera;

/// <summary>
/// 動的な始点・終点を持つ雷を1本管理するランタイム。
/// 内部で 2 本のレンダラを位相ずらしで交互再生成し、寿命毎にメッシュを作り直す
/// （= シードを毎回ランダムにして"パチパチ"する見た目を出す）。
/// 必殺技「傲慢サンダー」のように敵やプレイヤーを追従する電撃で使う想定。
/// </summary>
class LightningRuntime {
public:
    LightningRuntime() = default;
    ~LightningRuntime() = default;

    /// <summary>
    /// テンプレートとなる LightningBoltParams（始終点以外の見た目／フラクタル／枝の設定）と
    /// テクスチャパスを渡して初期化。実描画用の始終点は SetEndpoints で毎フレ更新する。
    /// </summary>
    void Initialize(const PrimitiveGenerator::LightningBoltParams& templateParams,
                    const std::string& texturePath,
                    int blendMode = 2 /*Add*/);

    void SetEndpoints(const Vector3& start, const Vector3& end);

    void Update(Camera* camera, float deltaTime);
    void Draw();

    bool IsActive() const { return active_; }
    void Stop() { active_ = false; }

    // チューニング
    void SetBoltLifetime(float sec)       { boltLifetime_  = sec; }
    void SetOverlapOffset(float sec)      { overlapOffset_ = sec; }
    void SetViewAngleFadePower(float p);
    void SetBlendMode(int mode)           { blendMode_ = mode; }

private:
    // 1本ぶんの状態
    struct BoltState {
        std::unique_ptr<EffectPrimitiveRenderer> renderer;
        float remaining = 0.0f;   // 寿命の残り（0以下で再生成）
        bool initialized = false; // 初回生成済みか
    };

    // 渡された始終点で renderer を作り直す（毎回シード=0 で乱数化）
    void Regenerate(BoltState& bolt);

    BoltState bolts_[2];
    PrimitiveGenerator::LightningBoltParams params_{};
    std::string texturePath_;
    int   blendMode_ = 2;
    float viewAngleFadePower_ = 0.0f;
    float boltLifetime_  = 0.333f;
    float overlapOffset_ = 0.166f;
    Vector3 startPos_{ 0.0f, 0.0f, 0.0f };
    Vector3 endPos_{ 0.0f, 0.0f, 1.0f };
    bool active_ = false;
};
