#pragma once
#include "DirectXCore.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include "SpotLight.h"
#include "MathUtility.h"
#include <array>
#include <numbers>
#include <cmath>
#include <wrl.h>

// Acquire 失敗時に返す無効スロット値
static constexpr uint32_t kInvalidLightSlot = UINT32_MAX;

/// <summary>
/// 平行光源・点光源・スポットライトの値を持ち、3D 描画の定数バッファへ積む。
/// シングルトンにする理由：ライトの定数バッファは全ての 3D 描画で共通の 1 組のため。
/// </summary>
class LightManager {
private:
    DirectXCore* dxCore_ = nullptr;

    // DirectionalLight
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    // PointLightGroup (複数対応)
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    PointLightGroup* pointLightGroupData_ = nullptr;

    // SpotLightGroup (複数対応)
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    SpotLightGroup* spotLightGroupData_ = nullptr;

    // 動的予約フラグ（Acquire/Release で管理）
    std::array<bool, kMaxPointLights> pointReserved_{};
    std::array<bool, kMaxSpotLights>  spotReserved_{};

    // コンストラクタ（private）
    LightManager() = default;
    ~LightManager() = default;

public:
    // コピー禁止
    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;

    /// <summary>シングルトンインスタンスを返す</summary>
    /// <returns>LightManager のインスタンス</returns>
    static LightManager* GetInstance();

    /// <summary>
    /// 平行光源・点光源グループ・スポットライトグループの定数バッファ（256 byte 境界）を作ってマップし、初期値を入れる。
    /// 初期値はどのライトも強さ 0（光らない）
    /// </summary>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
    void Initialize(DirectXCore* dxCore);
    /// <summary>3 つのライトの定数バッファを解放する</summary>
    void Finalize();

    /// <summary>
    /// 描画前にライトをバインドする（全オブジェクト共通）。モデル描画用ルートシグネチャの
    /// [3]=平行光源(b1)、[5]=点光源グループ(b3)、[6]=スポットライトグループ(b4) に CBV を入れる
    /// </summary>
    /// <param name="commandList">コマンドリスト</param>
    void BindLights(ID3D12GraphicsCommandList* commandList);

    // ===== DirectionalLight設定 =====
    /// <summary>平行光源の色を設定する</summary>
    /// <param name="color">色（RGBA）</param>
    void SetDirectionalLightColor(const Vector4& color) { directionalLightData_->color = color; }
    /// <summary>平行光源の向きを設定する</summary>
    /// <param name="direction">光の進む向き（ワールド空間）</param>
    void SetDirectionalLightDirection(const Vector3& direction) { directionalLightData_->direction = direction; }
    /// <summary>平行光源の輝度を設定する</summary>
    /// <param name="intensity">輝度（0 で光らない）</param>
    void SetDirectionalLightIntensity(float intensity) { directionalLightData_->intensity = intensity; }
    /// <summary>ライティング方式を設定する</summary>
    /// <param name="type">LightingMode の値（0=なし、1=ランバート、2=ハーフランバート）</param>
    void SetDirectionalLightType(int type) { directionalLightData_->lightingType = type; }

    // ===== PointLight設定 (インデックス指定) =====
    /// <summary>シェーダーが処理する点光源の数を設定する</summary>
    /// <param name="count">有効数（kMaxPointLights を超えたら切り詰める）</param>
    void SetPointLightCount(uint32_t count);
    /// <summary>シェーダーが処理する点光源の数を返す</summary>
    /// <returns>有効数</returns>
    uint32_t GetPointLightCount() const { return pointLightGroupData_->activeCount; }
    /// <summary>点光源の色を設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxPointLights 以上は無視）</param>
    /// <param name="color">色（RGBA）</param>
    void SetPointLightColor(uint32_t index, const Vector4& color);
    /// <summary>点光源の位置を設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxPointLights 以上は無視）</param>
    /// <param name="position">ワールド座標 [m]</param>
    void SetPointLightPosition(uint32_t index, const Vector3& position);
    /// <summary>点光源の輝度を設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxPointLights 以上は無視）</param>
    /// <param name="intensity">輝度（0 で光らない）</param>
    void SetPointLightIntensity(uint32_t index, float intensity);
    /// <summary>点光源の届く半径を設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxPointLights 以上は無視）</param>
    /// <param name="radius">半径 [m]</param>
    void SetPointLightRadius(uint32_t index, float radius);
    /// <summary>点光源の距離による減衰率を設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxPointLights 以上は無視）</param>
    /// <param name="decay">減衰率（べき）</param>
    void SetPointLightDecay(uint32_t index, float decay);

    // ===== 動的予約API（Effect Editor 用） =====
    /// <summary>
    /// 空きの点光源スロットを確保する。確保した時点で intensity=0 にリセットされ、
    /// activeCount はそのスロットまで広げる（縮めない）
    /// </summary>
    /// <returns>確保したスロット番号。空き無しなら kInvalidLightSlot</returns>
    uint32_t AcquirePointLight();
    /// <summary>
    /// 空きのスポットライトスロットを確保する。確保した時点で intensity=0 にリセットされ、
    /// activeCount はそのスロットまで広げる（縮めない）
    /// </summary>
    /// <returns>確保したスロット番号。空き無しなら kInvalidLightSlot</returns>
    uint32_t AcquireSpotLight();
    /// <summary>点光源スロットを解放する（intensity=0 にして予約フラグを下ろす。activeCount は縮めない）</summary>
    /// <param name="slot">AcquirePointLight で得たスロット番号（範囲外は無視）</param>
    void ReleasePointLight(uint32_t slot);
    /// <summary>スポットライトスロットを解放する（intensity=0 にして予約フラグを下ろす。activeCount は縮めない）</summary>
    /// <param name="slot">AcquireSpotLight で得たスロット番号（範囲外は無視）</param>
    void ReleaseSpotLight(uint32_t slot);

    // ===== SpotLight設定 (インデックス指定) =====
    /// <summary>シェーダーが処理するスポットライトの数を設定する</summary>
    /// <param name="count">有効数（kMaxSpotLights を超えたら切り詰める）</param>
    void SetSpotLightCount(uint32_t count);
    /// <summary>シェーダーが処理するスポットライトの数を返す</summary>
    /// <returns>有効数</returns>
    uint32_t GetSpotLightCount() const { return spotLightGroupData_->activeCount; }
    /// <summary>スポットライトの色を設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxSpotLights 以上は無視）</param>
    /// <param name="color">色（RGBA）</param>
    void SetSpotLightColor(uint32_t index, const Vector4& color);
    /// <summary>スポットライトの位置を設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxSpotLights 以上は無視）</param>
    /// <param name="position">ワールド座標 [m]</param>
    void SetSpotLightPosition(uint32_t index, const Vector3& position);
    /// <summary>スポットライトの向きを設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxSpotLights 以上は無視）</param>
    /// <param name="direction">照らす向き（ワールド空間）</param>
    void SetSpotLightDirection(uint32_t index, const Vector3& direction);
    /// <summary>スポットライトの輝度を設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxSpotLights 以上は無視）</param>
    /// <param name="intensity">輝度（0 で光らない）</param>
    void SetSpotLightIntensity(uint32_t index, float intensity);
    /// <summary>スポットライトの届く最大距離を設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxSpotLights 以上は無視）</param>
    /// <param name="distance">距離 [m]</param>
    void SetSpotLightDistance(uint32_t index, float distance);
    /// <summary>スポットライトの距離による減衰率を設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxSpotLights 以上は無視）</param>
    /// <param name="decay">減衰率（べき）</param>
    void SetSpotLightDecay(uint32_t index, float decay);
    /// <summary>スポットライトの照らす範囲の端の角度を余弦で設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxSpotLights 以上は無視）</param>
    /// <param name="cosAngle">中心軸からの角度 θ の cos(θ)</param>
    void SetSpotLightCosAngle(uint32_t index, float cosAngle);
    /// <summary>スポットライトが端に向けて暗くなり始める角度を余弦で設定する</summary>
    /// <param name="index">ライトのスロット番号（kMaxSpotLights 以上は無視）</param>
    /// <param name="cosFalloffStart">暗くなり始める角度の余弦</param>
    void SetSpotLightCosFalloffStart(uint32_t index, float cosFalloffStart);

    // ===== ゲッター =====
    /// <summary>平行光源の定数バッファ（マップ済み）を返す。書き換えるとそのまま GPU に反映される</summary>
    /// <returns>平行光源のデータ（Initialize 前は nullptr）</returns>
    DirectionalLight* GetDirectionalLightData() { return directionalLightData_; }
    /// <summary>別ルートシグネチャ（水面など）で平行光源 CB をバインドするための取得口</summary>
    /// <returns>平行光源 CB の GPU アドレス。未作成なら 0</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetDirectionalLightGpuAddress() const {
        return directionalLightResource_ ? directionalLightResource_->GetGPUVirtualAddress() : 0;
    }
    /// <summary>点光源グループの定数バッファ（マップ済み）を返す</summary>
    /// <returns>点光源グループのデータ（Initialize 前は nullptr）</returns>
    PointLightGroup* GetPointLightGroupData() { return pointLightGroupData_; }
    /// <summary>スポットライトグループの定数バッファ（マップ済み）を返す</summary>
    /// <returns>スポットライトグループのデータ（Initialize 前は nullptr）</returns>
    SpotLightGroup* GetSpotLightGroupData() { return spotLightGroupData_; }
    /// <summary>指定スロットの点光源を返す</summary>
    /// <param name="index">スロット番号</param>
    /// <returns>点光源のデータ。index が kMaxPointLights 以上なら nullptr</returns>
    PointLight* GetPointLight(uint32_t index);
    /// <summary>指定スロットのスポットライトを返す</summary>
    /// <param name="index">スロット番号</param>
    /// <returns>スポットライトのデータ。index が kMaxSpotLights 以上なら nullptr</returns>
    SpotLight* GetSpotLight(uint32_t index);

    /// <summary>平行光源・点光源・スポットライトのパラメータを ImGui で編集する（角度は度で表示）</summary>
    void OnImGui();
};