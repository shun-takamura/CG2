#pragma once
#include "BaseTransition.h"
#include "ScreenWaterSurface.h"
#include "Vector2.h"
#include "Vector3.h"
#include <wrl.h>
#include <d3d12.h>

/// <summary>
/// 墨のトランジション。画面を上から見下ろした水面として扱い、墨の雫が落ちて黒が広がり真っ黒になる。
/// シーン RT のコピーと波紋は ScreenWaterSurface に任せ、ここは墨の時間と描画だけを持つ。
/// 覆う側が本命（HUB → ステージ）。開く側は黒が薄れるだけの簡易版で、普段はストライプ等に任せる。
/// </summary>
class InkTransition : public BaseTransition {
public:
	struct Settings {
		Vector2 dropCenter = { 0.5f, 0.45f };  // 雫が落ちる点（画面 UV）
		float dropTime = 0.25f;                // 着水までの秒数
		float spreadTime = 1.9f;               // 着水から真っ黒になるまでの秒数
		float spreadEase = 2.2f;               // 広がりの緩急（大きいほど最初に速く広がって後で粘る）
		float edgeSoftness = 0.04f;            // 墨の縁のぼかし幅（画面の幅に対する割合）
		float revealTime = 0.4f;               // 開く側（簡易版）で黒が薄れる秒数

		float impulseRadius = 0.025f;          // 着水のへこみの半径
		float impulseStrength = 0.06f;         // 着水のへこみの深さ

		// 墨の形（ノイズは cloud_noise.dds：R = 形の fBm / G = 低周波の fBm）
		float warpAmount = 0.35f;              // 縁を渦状にゆがめる量（墨の半径に対する割合）
		float warpScale = 2.5f;                // ゆがみの細かさ
		float tendrilAmount = 0.45f;           // 筋（指のように伸びる所）の長さ（半径に対する割合）
		float tendrilFreq = 1.6f;              // 筋の本数の目安（大きいほど細かく多い）
		float detailScale = 9.0f;              // 縁のちぎれ（薄い所）の細かさ
		float wispAmount = 0.8f;               // 縁のちぎれの強さ
		float edgeWidth = 0.18f;               // 濃い芯から縁までの幅（半径に対する割合）
		float flowSpeed = 0.06f;               // ノイズの流れる速さ（墨が水中でうごめく）
		Vector3 thinColor = { 0.10f, 0.12f, 0.18f }; // 薄い所の色（少し青みのある墨色）

		// 墨の中の濃淡（初めは濃い所と薄い所が混ざり、だんだん真っ黒が増える）
		float toneScale = 3.0f;                // 濃淡の模様の大きさ（大きいほど細かい）
		float toneMin = 0.25f;                 // 一番薄い所の濃さ（0 = 透明 / 1 = 濃淡なし）
		float toneDepth = 0.5f;                // 芯の深い所（墨が古い所）ほど先に黒くなる強さ
		float toneDarkStart = 0.3f;            // 覆う進みがここを過ぎたら全体が黒に寄り始める（0.9 で真っ黒）

		// 着水前の雫（上から落ちてくるので、近い＝大きくぼけた所から小さく締まっていく）
		float dropStartRadius = 0.08f;         // 落ち始めの見た目の半径（画面の幅に対する割合）
		float dropEndRadius = 0.012f;          // 着水直前の半径
		float shadowStrength = 0.35f;          // 水面に落ちる雫の影の濃さ

		// JSON との行き来（TransitionTuning.h）。項目を足したらここにも足す
		template <class V>
		void Visit(V& v) {
			v("dropCenter", dropCenter);
			v("dropTime", dropTime);
			v("spreadTime", spreadTime);
			v("spreadEase", spreadEase);
			v("edgeSoftness", edgeSoftness);
			v("revealTime", revealTime);
			v("impulseRadius", impulseRadius);
			v("impulseStrength", impulseStrength);
			v("warpAmount", warpAmount);
			v("warpScale", warpScale);
			v("tendrilAmount", tendrilAmount);
			v("tendrilFreq", tendrilFreq);
			v("detailScale", detailScale);
			v("wispAmount", wispAmount);
			v("edgeWidth", edgeWidth);
			v("flowSpeed", flowSpeed);
			v("thinColor", thinColor);
			v("toneScale", toneScale);
			v("toneMin", toneMin);
			v("toneDepth", toneDepth);
			v("toneDarkStart", toneDarkStart);
			v("dropStartRadius", dropStartRadius);
			v("dropEndRadius", dropEndRadius);
			v("shadowStrength", shadowStrength);
		}
	};

	InkTransition() = default;
	~InkTransition() override = default;

	/// <summary>水面の部品と定数バッファを解放する</summary>
	void Finalize() override;
	/// <summary>シーン RT を写して、墨の広がりを全画面パスでシーン RT へ描き戻す（トランジション中だけ）</summary>
	void Draw() override;

	/// <summary>トランジションの種類を返す</summary>
	/// <returns>TransitionType::Ink</returns>
	TransitionType GetType() const override { return TransitionType::Ink; }
	/// <summary>トランジション名を返す</summary>
	/// <returns>"Ink"</returns>
	std::string GetName() const override { return "Ink"; }

	/// <summary>調整値を返す（書き換える時はコピーして SetSettings で戻す）</summary>
	/// <returns>今の値</returns>
	const Settings& GetSettings() const { return settings_; }
	/// <summary>調整値をまとめて差し替える</summary>
	/// <param name="value">新しい値</param>
	void SetSettings(const Settings& value) { settings_ = value; }

	/// <summary>調整値を ImGui で調整する</summary>
	void OnImGui();

	/// <summary>調整値を JSON から読む（Resources/Json/Tuning/Transition.json の GetName() の欄）</summary>
	/// <param name="json">このトランジションの欄</param>
	void LoadTuning(const JsonValue& json) override;
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="json">書き込み先（このトランジションの欄）</param>
	void SaveTuning(JsonValue& json) const override;

protected:
	/// <summary>墨の進みと開く側の濃さを戻し、水面を平らにする</summary>
	void OnStartCover() override;
	/// <summary>水面の部品・全画面パス・ノイズのテクスチャ・定数バッファを作る</summary>
	void OnInitialize() override;
	/// <summary>着水からの経過で墨の広がり（coverage_）を 0→1 に進める</summary>
	/// <param name="dt">経過 [秒]（使わない。経過は GetPhaseTime で見る）</param>
	/// <returns>真っ黒になったら true</returns>
	bool UpdateCover(float dt) override;
	/// <summary>開く側（簡易版）で黒の濃さを 1→0 に薄める</summary>
	/// <param name="dt">経過 [秒]（使わない。経過は GetPhaseTime で見る）</param>
	/// <returns>黒が消え切ったら true</returns>
	bool UpdateReveal(float dt) override;
	/// <summary>真っ黒の見た目にする</summary>
	void SetFullyCovered() override;
	/// <summary>着水の瞬間に一度だけ水面をへこませ、Hold 以外の間は波を進める</summary>
	void UpdateVisual() override;

private:
	// InkTransition.PS.hlsl の InkParams と 1:1（144 バイト）
	struct ParamsForGPU {
		ScreenWaterSurface::ConstantsForGPU water;
		Vector2 dropCenter;
		float inkRadius;       // 墨の半径（画面の幅に対する割合）。着水前は負
		float edgeSoftness;
		float coverage;        // 1 なら全面黒（取り込んだ画面を読まない）
		float fade;            // 開く側：黒の濃さ 1→0
		float time;            // 遷移の経過秒（ノイズを流す）
		float dropPhase;       // 着水前の雫 0→1（着水後は負）
		float dropStartRadius;
		float dropEndRadius;
		float warpAmount;
		float warpScale;
		float tendrilAmount;
		float tendrilFreq;
		float detailScale;
		float wispAmount;
		float edgeWidth;
		float flowSpeed;
		float shadowStrength;
		float padding0;
		Vector3 thinColor;
		float padding1;
		float toneScale;
		float toneMin;
		float toneDepth;
		float toneDarkStart;
	};

	/// <summary>ルートシグネチャを作る（宣言のみ。今は ScreenWaterSurface::CreateRootSignature を使う）</summary>
	void CreateRootSignature();
	/// <summary>PSO を作る（宣言のみ。今は ScreenWaterSurface::CreatePipelineState を使う）</summary>
	void CreatePipelineState();
	/// <summary>今の状態から墨の定数を作って定数バッファへ書く</summary>
	/// <returns>書いた値（書き込み専用のメモリを読み返さないため）</returns>
	ParamsForGPU WriteParams();
	/// <summary>覆う進みから墨の半径を求める（最初に速く広がって後で粘る）</summary>
	/// <returns>墨の半径（画面の幅を 1 とした長さ）。着水前は負</returns>
	float CalcInkRadius() const;

	ScreenWaterSurface water_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	Microsoft::WRL::ComPtr<ID3D12Resource> paramsResource_;
	ParamsForGPU* paramsData_ = nullptr;

	Settings settings_;
	// 覆う側の進み（0〜1。1 で真っ黒）
	float coverage_ = 0.0f;
	// 開く側の黒の濃さ（1→0）
	float fade_ = 1.0f;
	// 着水の波源をこの遷移で入れたか
	bool impulseFired_ = false;
};
