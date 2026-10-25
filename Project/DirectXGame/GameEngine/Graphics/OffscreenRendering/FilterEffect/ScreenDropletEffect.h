#pragma once
#include "BaseFilterEffect.h"
#include <string>

/// <summary>
/// 画面（レンズ）に付いた水滴（13_RiverWater.md §9）。水しぶき・雲の中の霧雨が画面に付く。
/// 水滴はシェーダが格子と乱数で作るので、外から渡すのは「濡れ具合」と「流れの向き（モード）」だけ。
///   - 重力（Gravity）：水面すれすれ。画面の下の方に付き、大きい物が下へ垂れる
///   - 放射（Radial）：雲の縦穴。画面の周りに霧雨のような小さい水滴が付き、合体しながら全部が外へ流れる
///   - 濡れ具合が上がるほど、水滴の数・大きさ・垂れる割合が増え、付く範囲が広がる。下がる時は薄れて消える
/// モードごとに見た目の値（Settings）を 1 組ずつ持つ。モードは乾ききった時（濡れ具合 0）だけ切り替える
/// （水滴が見えている間に切り替えると跳ぶため。切り替えを求められたら、まず今のモードで乾かす）。
/// 濡れ具合が 0 に戻ると自分で無効になる（フィルタのパスを飛ばす）。
/// </summary>
class ScreenDropletEffect : public BaseFilterEffect
{
public:
    enum class Mode {
        Gravity, // 水面すれすれ
        Radial,  // 雲の縦穴
        Count,
    };

    /// <summary>見た目と濡れ具合の追いかけ方の調整値（モードごとに 1 組）</summary>
    struct Settings {
        float density = 9.0f;           // 止まっている水滴の格子の細かさ（重力：画面の高さあたりのマス数／放射：1 周のマス数 ÷ 2π）。粗いほど 1 粒が大きい
        float sizeMin = 0.15f;          // 水滴の半径の下限（マスに対する割合）。小さすぎると白い点に見える
        float sizeMax = 0.35f;          // 上限（0.35 まで。周りのマスも調べるのではみ出してよい）
        float dryScale = 0.6f;          // 濡れ具合 0 の時の大きさの倍率（小さな水滴）
        float lifetime = 1.6f;          // 止まっている水滴が現れてから消えるまでの平均 [s]（短いほど入れ替わりが速い）
        float refraction = 0.6f;        // レンズのずらしの強さ
        float dripChance = 0.6f;        // 濡れ具合 1 の時に垂れる列の割合
        float dripSpeed = 0.2f;         // 流れ始めの速さ [画面の高さ/s]
        float dripColumns = 14.0f;      // 垂れる列の数（重力：画面の幅あたり／放射：1 周で × 2.5）
        float trailLength = 0.35f;      // 垂れた跡（筋と残る粒）が薄れきるまでの長さ
        float dripRadius = 0.022f;      // 垂れる水滴の膨らみきった半径 [画面の高さ]
        float dripAccel = 0.8f;         // 流れる速さの伸び（合体して重くなる分）[1/s]
        float dripStepRate = 3.0f;      // 流れながらためらう回数 [回/s]（止まりはしない）
        float dripGrow = 0.8f;          // 画面の外まで流れた時の大きさの伸び（1 で 2 倍）
        float topSizeScale = 0.5f;      // 付く範囲の内側（重力は上）の端での大きさの倍率（外側の端で 1）
        float dripGrowTime = 0.8f;      // 垂れる水滴が流れ始める前にその場で膨らむ時間 [s]
        float trailBeadSpacing = 0.025f;// 垂れた跡に残る小さな粒の間隔
        float coverageTopDry = 0.88f;   // 濡れ具合 0 の時に水滴が付き始める所（重力：画面の高さ 0=上端・1=下端／放射：中心からの距離 0=中心・辺 0.71・角 1）
        float coverageTopWet = 0.5f;    // 濡れ具合 1 の時（重力は上限＝画面の下半分）
        float coverageFade = 0.15f;     // 付き始めから濃くなる幅
        float highlight = 0.6f;         // 水滴の上側の光
        float rimDarkness = 0.35f;      // 水滴のふちの暗さ
        float flowSpeed = 0.0f;         // 放射：止まっている水滴が外へ流れる速さ（中心からの距離が e 倍になる回数 [1/s]）。重力では使わない
        float sizeSpeed = 0.0f;         // 大きい水滴が寿命の間に余分に滑る量 [マス]（大きさの 2 乗に比例・加速。1.8 まで）。速さがそろうと泡に見えるので放射で使う
        float wetTime = 0.3f;           // 濡れ具合が上がる時に追いつく速さ（この秒数で約 63%）
        float dryTime = 0.8f;           // 下がる時（乾く）の速さ。濡れる所を抜けたらすぐ消えるように短め
        float sizeDryTime = 3.0f;       // 大きさに使う濡れ具合が下がる速さ。乾く間に見えている水滴が縮まないよう、薄れて消える方を先にする
    };

    void Initialize(
        DirectXCore* dxCore,
        ID3D12RootSignature* copyRootSignature,
        ID3D12RootSignature* effectRootSignature,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
    ) override;

    void UpdateConstantBuffer() override;
    void ShowImGui() override;
    void ResetParams() override;

    std::string GetName() const override { return "ScreenDroplet"; }
    bool NeedsCBuffer() const override { return true; }

    /// <summary>
    /// 毎フレーム呼ぶ（呼び出し元は 1 か所にまとめること）。濡れ具合を target（0..1）へ追いかけ、水滴の時間を進める。
    /// mode が今と違えば、乾ききってから切り替える。濡れ具合が 0 より大きい間だけ有効にする
    /// </summary>
    void Update(float deltaTime, float targetWetness, Mode mode);

    /// <summary>水滴を消して無効にする（シーンを抜ける時など）</summary>
    void Clear();

    /// <summary>モードごとの値を JSON から読む（無ければ既定値のまま）。Save はこのパスに書く</summary>
    void LoadSettings(const std::string& path);
    void SaveSettings() const;

    float GetWetness() const { return wetness_; }
    Mode GetMode() const { return mode_; }
    Settings& GetSettings(Mode mode) { return settings_[static_cast<int>(mode)]; }
    const Settings& GetSettings(Mode mode) const { return settings_[static_cast<int>(mode)]; }

private:
    // ScreenDroplet.PS.hlsl の ScreenDropletParams と 1:1
    struct ParamsCB {
        float time;
        float wetness;
        float density;
        float sizeMin;
        float sizeMax;
        float dryScale;
        float lifetime;
        float refraction;
        float dripChance;
        float dripSpeed;
        float dripColumns;
        float trailLength;
        float coverageTopDry;
        float coverageTopWet;
        float coverageFade;
        float highlight;
        float rimDarkness;
        float dripRadius;
        float dripAccel;
        float dripStepRate;
        float dripGrow;
        float topSizeScale;
        float dripGrowTime;
        float trailBeadSpacing;
        float sizeWetness;
        float radial;
        float flowSpeed;
        float sizeSpeed;
    };

    /// <summary>モードごとの既定値（重力はクラスの既定、放射は霧雨向け）</summary>
    static Settings DefaultSettings(Mode mode);
    const Settings& Current() const { return settings_[static_cast<int>(mode_)]; }

    Settings settings_[static_cast<int>(Mode::Count)];
    Mode mode_ = Mode::Gravity;
    float wetness_ = 0.0f;
    float sizeWetness_ = 0.0f; // 大きさに使う濡れ具合（上がる時は wetness_ と同じ、下がる時は sizeDryTime でゆっくり）
    float time_ = 0.0f;
    std::string settingsPath_;

    // ImGui の確認用
    int editMode_ = 0;              // どちらのモードの値を触るか
    float previewWetness_ = -1.0f;  // 0 以上なら target の代わりに使い、モードも editMode_ にする
};
