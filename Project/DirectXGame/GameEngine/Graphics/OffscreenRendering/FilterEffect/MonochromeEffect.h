#pragma once
#include "BaseFilterEffect.h"
#include <string>
#include <vector>

/// <summary>
/// 白黒の演出の見た目を決める値（シェーダーの定数バッファの前半と同じ並び）。
/// 明るさはすべて知覚上の値（0〜1）で扱う
/// </summary>
struct MonochromeParams
{
	float monoAmount = 0.0f;     // 0 = 元の色, 1 = 白黒
	float contrast = 1.0f;       // 明暗の強調（1 = そのまま）
	float midpoint = 0.5f;       // コントラストの中心の明るさ
	float threshold = 0.5f;      // 白と黒を分けるしきい値（下げるほど白が多い）
	float softness = 1.0f;       // 0 = くっきり 2 値, 1 = グレーのまま
	float noiseAmount = 0.0f;    // しきい値をノイズで揺らす量
	float noiseScale = 6.0f;     // ノイズの細かさ（画面の高さあたりの数）
	float noiseSpeed = 0.5f;     // ノイズが動く速さ [1/s]
	float edgeStrength = 0.0f;   // 輪郭の黒い線の濃さ（0〜1）
	float edgeThreshold = 0.02f; // 輪郭とみなす深度の差（相対値）
	float edgeWidth = 1.0f;      // 輪郭の線の太さ [px]
	float invert = 0.0f;         // 反転の量（0〜1）
};

/// <summary>白黒の演出の段階（それぞれの見た目の初期値を持つ）</summary>
enum class MonochromeStage
{
	Color,      // 通常の色
	Ink,        // 白黒の 2 値（墨絵）
	StrongGray, // 明暗を強調したグレースケール
	WhiteInk,   // 白が多い 2 値（黒い線や影だけ残る）
	InvertInk,  // WhiteInk の反転（白い線や光だけ残る）
	Count,
};

/// <summary>
/// 白黒の演出の並びの 1 コマ。前のコマの見た目から blendTime 秒でこの見た目へ移り、holdTime 秒とどまる
/// </summary>
struct MonochromeKey
{
	MonochromeParams params;
	float blendTime = 0.0f; // 前の見た目から移る時間 [s]（0 で切り替え）
	float holdTime = 0.0f;  // この見た目にとどまる時間 [s]
};

/// <summary>名前を付けた白黒の演出の並び（JSON に保存してゲームから名前で呼ぶ）</summary>
struct MonochromeSequence
{
	std::string name;
	std::vector<MonochromeKey> keys;
};

/// <summary>
/// 墨絵のような白黒のポストエフェクト（ボスの必殺技など）。
/// 輝度をコントラストで強調 → ノイズで揺らしたしきい値で 2 値化 → 深度の輪郭を黒い線で重ねる → 反転 → 元の色と混ぜる。
/// 並び（MonochromeKey の列）を Play すると時間に沿って見た目を補間し、再生中は自動で有効になる。
/// 時間は PostEffect::Update から実時間で進める（ゲームのスローやヒットストップの影響を受けない）
/// </summary>
class MonochromeEffect : public BaseFilterEffect
{
public:
	/// <summary>Base 経由でも呼べるようにしたダミー実装（未使用。初期化は InitializeDepth）</summary>
	/// <param name="dxCore">未使用</param>
	/// <param name="copyRootSignature">未使用</param>
	/// <param name="effectRootSignature">未使用</param>
	/// <param name="basePsoDesc">未使用</param>
	void Initialize(
		[[maybe_unused]] DirectXCore* dxCore,
		[[maybe_unused]] ID3D12RootSignature* copyRootSignature,
		[[maybe_unused]] ID3D12RootSignature* effectRootSignature,
		[[maybe_unused]] const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	) override {}

	/// <summary>
	/// outline 用ルートシグネチャで Monochrome.PS の PSO と定数バッファを作る
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="outlineRootSignature">PSO に使う outline 用ルートシグネチャ（[0]=t0 色、[1]=t1 深度、[2]=b0、s0=linear、s1=point）</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定</param>
	void InitializeDepth(
		DirectXCore* dxCore,
		ID3D12RootSignature* outlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	);

	/// <summary>今の見た目・経過時間・深度の戻し方を定数バッファへ書き込む</summary>
	void UpdateConstantBuffer() override;
	/// <summary>見た目の調整・段階のボタン・並びの編集と再生・保存を ImGui で表示する</summary>
	void ShowImGui() override;
	/// <summary>再生を止め、見た目を通常の色に戻す（有効/無効は変えない）</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"Monochrome"</returns>
	std::string GetName() const override { return "Monochrome"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }
	/// <summary>輪郭を取るためにシーン深度（t1）を読む</summary>
	/// <returns>常に true</returns>
	bool NeedsDepth() const override { return true; }
	/// <summary>射影行列から、深度を視空間の距離へ戻す係数を取り出す</summary>
	/// <param name="projection">カメラの射影行列</param>
	void SetProjectionMatrix(const Matrix4x4& projection) override;

	/// <summary>
	/// 時間を進める。再生中は並びに沿って見た目を補間し、終わったら最後の見た目で止める
	/// （最後が通常の色で、再生で自動的に有効にしていたなら無効に戻す）
	/// </summary>
	/// <param name="deltaTime">経過時間 [s]（負は 0）</param>
	void Update(float deltaTime);

	// ===== ゲームからの窓口 =====
	/// <summary>並びを今の見た目から再生する。無効なら自動で有効にする</summary>
	/// <param name="keys">再生する並び（空なら何もしない）</param>
	void Play(const std::vector<MonochromeKey>& keys);
	/// <summary>JSON から読んだ、名前の付いた並びを再生する</summary>
	/// <param name="sequenceName">並びの名前</param>
	/// <returns>見つかって再生したら true</returns>
	bool Play(const std::string& sequenceName);
	/// <summary>再生を止めて通常の色に戻し、無効にする</summary>
	void Stop();
	/// <summary>再生中かどうかを返す</summary>
	/// <returns>再生中なら true</returns>
	bool IsPlaying() const { return playing_; }
	/// <summary>再生を始めてからの時間を返す</summary>
	/// <returns>経過時間 [s]</returns>
	float GetPlayTime() const { return playTime_; }
	/// <summary>並びの長さ（移る時間ととどまる時間の合計）を返す</summary>
	/// <param name="keys">並び</param>
	/// <returns>長さ [s]</returns>
	static float GetDuration(const std::vector<MonochromeKey>& keys);

	/// <summary>見た目を直接設定する（再生中なら次の Update で上書きされる）</summary>
	/// <param name="params">見た目</param>
	void SetParams(const MonochromeParams& params) { params_ = params; }
	/// <summary>今の見た目を返す</summary>
	/// <returns>見た目</returns>
	const MonochromeParams& GetParams() const { return params_; }

	/// <summary>段階の見た目の初期値を返す</summary>
	/// <param name="stage">段階</param>
	/// <returns>見た目</returns>
	static MonochromeParams GetStagePreset(MonochromeStage stage);
	/// <summary>段階の初期値で並びの 1 コマを作る</summary>
	/// <param name="stage">段階</param>
	/// <param name="blendTime">前の見た目から移る時間 [s]</param>
	/// <param name="holdTime">とどまる時間 [s]</param>
	/// <returns>1 コマ</returns>
	static MonochromeKey MakeKey(MonochromeStage stage, float blendTime, float holdTime);

	/// <summary>名前の付いた並びを JSON から読む（無ければ見本の並びを 1 つ作る）。Save はこのパスに書く</summary>
	/// <param name="path">JSON のパス</param>
	void LoadSequences(const std::string& path);
	/// <summary>名前の付いた並びを LoadSequences で受け取ったパスへ書き出す（パスが空なら何もしない）</summary>
	void SaveSequences() const;
	/// <summary>名前の付いた並びを探す</summary>
	/// <param name="sequenceName">並びの名前</param>
	/// <returns>見つかった並び。無ければ nullptr</returns>
	const MonochromeSequence* FindSequence(const std::string& sequenceName) const;

private:
	struct ParamsCB
	{
		MonochromeParams params;
		float time = 0.0f;
		float depthA = 0.0f;
		float depthB = 0.0f;
		float padding = 0.0f;
	};

	/// <summary>見本の並びを 1 つ作る（JSON が無いとき）</summary>
	void AddSampleSequence();
	/// <summary>再生時間から今の見た目を求める</summary>
	void EvaluateSequence();

#ifdef _DEBUG
	/// <summary>見た目の値をスライダーで編集する</summary>
	/// <param name="params">編集する見た目</param>
	static void EditParams(MonochromeParams& params);
	/// <summary>並びの選択・コマの編集・再生・保存の ImGui</summary>
	void ShowSequenceEditor();
#endif

	MonochromeParams params_{};          // 今の見た目（定数バッファへ送る）
	float time_ = 0.0f;                  // ノイズを動かす経過時間 [s]
	float depthA_ = 0.0f;                // 射影行列の m[2][2]
	float depthB_ = 0.0f;                // 射影行列の m[3][2]

	// 再生
	std::vector<MonochromeKey> playKeys_; // 再生中の並び
	MonochromeParams playStart_{};        // 再生を始めたときの見た目（最初のコマへ移る元）
	float playTime_ = 0.0f;
	bool playing_ = false;
	bool autoEnabled_ = false;            // 再生で自動的に有効にしたか（終わったら無効に戻す）

	// 名前の付いた並び
	std::vector<MonochromeSequence> sequences_;
	std::string sequencesPath_;
	int editSequence_ = 0;                // ImGui で編集中の並び
};
