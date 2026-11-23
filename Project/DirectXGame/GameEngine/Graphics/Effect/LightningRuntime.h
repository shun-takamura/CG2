#pragma once
#include "EffectPrimitiveRenderer.h"
#include "Primitive/PrimitiveGenerator.h"
#include "Primitive/PrimitivePipeline.h"
#include "Vector3.h"
#include <memory>
#include <string>

class Camera;

/// <summary>
/// 動的な始点・終点を持つ雷を1本管理するランタイム。
/// 内部で 2 本のレンダラを位相ずらしで交互再生成し、寿命毎にメッシュを作り直す
/// （= シードを毎回ランダムにして"パチパチ"する見た目を出す）。
/// 敵やプレイヤーを追従する電撃（ゲームの必殺技 Storm Lock など）で使う想定。
/// </summary>
class LightningRuntime {
public:
    LightningRuntime() = default;
    ~LightningRuntime() = default;

    /// <summary>
    /// テンプレートとなる LightningBoltParams（始終点以外の見た目／フラクタル／枝の設定）と
    /// テクスチャパスを渡して初期化。実描画用の始終点は SetEndpoints で毎フレ更新する。
    /// </summary>
    /// <param name="templateParams">見た目・フラクタル・枝の設定（始終点とシードは無視される）</param>
    /// <param name="texturePath">雷のテクスチャのパス（空なら white1x1）</param>
    /// <param name="blendMode">PrimitivePipeline::BlendMode の値（既定 2=Add）</param>
    void Initialize(const PrimitiveGenerator::LightningBoltParams& templateParams,
                    const std::string& texturePath,
                    int blendMode = PrimitivePipeline::kBlendModeAdd);

    /// <summary>次に作り直す雷の始点・終点を設定する（今出ている雷は形を変えない）</summary>
    /// <param name="start">始点のワールド座標</param>
    /// <param name="end">終点のワールド座標</param>
    void SetEndpoints(const Vector3& start, const Vector3& end);

    /// <summary>2 本の寿命を進め、切れた（または未生成の）方を今の始終点・新しい乱数で作り直す</summary>
    /// <param name="camera">ビルボードと WVP に使うカメラ</param>
    /// <param name="deltaTime">経過時間 [秒]</param>
    void Update(Camera* camera, float deltaTime);
    /// <summary>2 本を描く。寿命の後半は α を 1→0 へ下げてフェードさせる</summary>
    void Draw();

    /// <summary>動作中か</summary>
    /// <returns>Initialize 後で Stop していなければ true</returns>
    bool IsActive() const { return active_; }
    /// <summary>更新と描画を止める</summary>
    void Stop() { active_ = false; }

    // チューニング
    /// <summary>1 本の雷の寿命を設定する</summary>
    /// <param name="sec">寿命 [秒]</param>
    void SetBoltLifetime(float sec)       { boltLifetime_  = sec; }
    /// <summary>2 本目の寿命のずらし量を設定する（次の Initialize から効く）</summary>
    /// <param name="sec">ずらし量 [秒]</param>
    void SetOverlapOffset(float sec)      { overlapOffset_ = sec; }
    /// <summary>斜めから見た面を薄くする強さを設定する（今の雷にもすぐ反映）</summary>
    /// <param name="p">べき乗の指数（0 で無効）</param>
    void SetViewAngleFadePower(float p);
    /// <summary>ブレンドモードを設定する（次に作り直す雷から効く）</summary>
    /// <param name="mode">PrimitivePipeline::BlendMode の値</param>
    void SetBlendMode(int mode)           { blendMode_ = mode; }

private:
    // 1本ぶんの状態
    struct BoltState {
        std::unique_ptr<EffectPrimitiveRenderer> renderer;
        float remaining = 0.0f;   // 寿命の残り（0以下で再生成）
        bool initialized = false; // 初回生成済みか
    };

    /// <summary>今の始終点で renderer を作り直す（毎回シード=0 で乱数化）</summary>
    /// <param name="bolt">作り直す 1 本</param>
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
