#pragma once
#include "Vector2.h"
#include "Water/RippleSimulation.h"
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>

class DirectXCore;
class SRVManager;
class RenderTexture;

/// <summary>
/// 画面を上から見下ろした水面として扱うための共通部品（墨・羽根のトランジションで使う）。
///   ・そのフレームのシーン RT を専用テクスチャへコピーする（PS の t0 で読む）
///   ・波紋（RippleSimulation）を持つ（PS の t1 で高さを読む）
///   ・シーン RT へ全画面で描き戻すときの RTV / ビューポートの設定と戻し
/// シェーダ側は ScreenWater.hlsli（屈折・波の明暗）を include し、定数バッファの先頭に
/// ConstantsForGPU と同じ並びの値を置く。
/// 長さは「画面の幅 = 1」で測る（縦横比で円がつぶれないように）。
/// </summary>
class ScreenWaterSurface {
public:
	struct Settings {
		float simSize = 2.0f;                  // 波を解く範囲の一辺（画面の幅の何倍か。広いほど速い波を出せるが粗くなる）
		float waveSpeed = 0.6f;                // 波の速さ（画面の幅 / 秒。安定のため内部で上限あり）
		float damping = 0.992f;                // 1刻みごとの減衰
		float stepsPerSecond = 240.0f;         // 固定刻みの回数（1フレーム最大4刻み）
		float refraction = 4.0f;               // 波の傾きで画面をずらす量
		float highlight = 0.6f;                // 波の斜面の明暗（光の来る側が明るく、反対側が暗い）
		Vector2 lightDirection = { -0.6f, -0.8f }; // 光の来る向き（画面 UV、左上から）

		// JSON との行き来（TransitionTuning.h）。項目を足したらここにも足す
		template <class V>
		void Visit(V& v) {
			v("simSize", simSize);
			v("waveSpeed", waveSpeed);
			v("damping", damping);
			v("stepsPerSecond", stepsPerSecond);
			v("refraction", refraction);
			v("highlight", highlight);
			v("lightDirection", lightDirection);
		}
	};

	// ScreenWater.hlsli を使うシェーダの定数バッファ先頭と 1:1（32 バイト）
	struct ConstantsForGPU {
		Vector2 rippleCenter;   // 波の範囲の中心（画面の幅 = 1 の座標）
		float rippleSize;       // 波の範囲の一辺
		float refraction;
		Vector2 lightDirection; // 光の来る向き（正規化済み）
		float highlight;
		float aspect;           // 画面の 幅 / 高さ
	};

	/// <summary>シーン RT の写し（描画解像度）と波紋のシミュレーションを作る</summary>
	/// <param name="dxCore">GPU リソースの作成に使う</param>
	/// <param name="srvManager">写しと波の高さの SRV を取る</param>
	/// <param name="screenWidth">画面幅（縦横比に使う）</param>
	/// <param name="screenHeight">画面高さ</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, float screenWidth, float screenHeight);
	/// <summary>写しのテクスチャと SRV を解放する</summary>
	void Finalize();

	/// <summary>水面を平らに戻し、波の範囲の中心を画面の中心へ戻す（遷移の始まりに呼ぶ）</summary>
	void Reset();

	/// <summary>画面 UV の点に波源を入れる</summary>
	/// <param name="uv">波源の位置（画面 UV）</param>
	/// <param name="radius">半径（画面の幅 = 1）</param>
	/// <param name="strength">深さ（画面の幅 = 1）</param>
	void AddImpulse(const Vector2& uv, float radius, float strength);

	// ===== 3D の水面として使う（羽根の着水：斜めに見下ろす水面）=====
	// 波は画面ではなく水面のワールド XZ で解く。中心を決めると、以後は ConstantsForGPU の
	// rippleCenter / rippleSize もワールドの値になる（シェーダは自前で XZ → 高さマップ UV を求める）

	/// <summary>波の範囲の中心をワールド XZ で決める（呼ぶまでは画面の中心）</summary>
	/// <param name="centerXZ">中心のワールド XZ</param>
	void SetRippleWorldCenter(const Vector2& centerXZ);

	/// <summary>ワールド XZ の点に波源を入れる</summary>
	/// <param name="positionXZ">波源のワールド XZ</param>
	/// <param name="radius">半径（ワールドの長さ）</param>
	/// <param name="strength">深さ（ワールドの長さ）</param>
	void AddImpulseWorld(const Vector2& positionXZ, float radius, float strength);

	/// <summary>調整値を波紋へ反映して波の時間を進める（実際の計算は Dispatch）</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);

	/// <summary>
	/// 描画の前準備：シーン RT をコピーし（captureScene のとき）、波を進めてから、
	/// シーン RT を DSV なしで描き先にしてビューポートを合わせる。SRV ヒープもここで貼る。
	/// 描画解像度が変わっていたら写しを作り直す
	/// </summary>
	/// <param name="cmd">描画のコマンドリスト</param>
	/// <param name="sceneTarget">シーン RT</param>
	/// <param name="captureScene">true ならこのフレームのシーンを写す（全面を覆っている間は false でよい）</param>
	void BeginDraw(ID3D12GraphicsCommandList* cmd, RenderTexture* sceneTarget, bool captureScene);

	/// <summary>後に続く描画のために、シーン RT と DSV を貼り直す</summary>
	/// <param name="cmd">描画のコマンドリスト</param>
	/// <param name="sceneTarget">シーン RT</param>
	void EndDraw(ID3D12GraphicsCommandList* cmd, RenderTexture* sceneTarget);

	// ===== 全画面パスの作成（このクラスを使うシェーダは同じ形のルートシグネチャになる） =====
	// ルートパラメータ：[0] = PS b0（定数バッファ） / [1] = t0 シーンのコピー / [2] = t1 波の高さ / [3..] = t2 以降（呼んだ側のテクスチャ）
	// サンプラ：s0 = クランプ・線形 / s1 = ラップ・線形
	static constexpr UINT kRootParams = 0;
	static constexpr UINT kRootSceneCopy = 1;
	static constexpr UINT kRootRipple = 2;
	static constexpr UINT kRootExtraTextures = 3;

	/// <summary>全画面パスのルートシグネチャを作る</summary>
	/// <param name="extraTextureCount">t2 以降に呼んだ側が使うテクスチャの枚数</param>
	/// <returns>ルートシグネチャ</returns>
	Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(UINT extraTextureCount) const;

	/// <summary>全画面パスの PSO を作る（頂点は PostProcess.VS の全画面三角形）</summary>
	/// <param name="rootSignature">CreateRootSignature で作ったルートシグネチャ</param>
	/// <param name="pixelShaderPath">ピクセルシェーダのパス</param>
	/// <param name="alphaBlend">true なら通常の α ブレンド、false なら上書き</param>
	/// <returns>PSO</returns>
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipelineState(
		ID3D12RootSignature* rootSignature, const wchar_t* pixelShaderPath, bool alphaBlend) const;

	/// <summary>t0（シーンのコピー）と t1（波の高さ）を貼る</summary>
	/// <param name="cmd">描画のコマンドリスト</param>
	void BindTextures(ID3D12GraphicsCommandList* cmd) const;

	/// <summary>シーンの写しの SRV の番号を返す</summary>
	/// <returns>SRV の番号。未作成なら UINT32_MAX</returns>
	uint32_t GetSceneCopySrvIndex() const { return sceneCopySrvIndex_; }
	/// <summary>波の高さの SRV を返す</summary>
	/// <returns>GPU ディスクリプタハンドル</returns>
	D3D12_GPU_DESCRIPTOR_HANDLE GetRippleSrvHandle() const { return ripple_.GetHeightSrvHandle(); }
	/// <summary>シェーダの定数バッファの先頭に置く水面の値を作る</summary>
	/// <returns>ScreenWater.hlsli と同じ並びの値</returns>
	ConstantsForGPU GetConstants() const;

	/// <summary>画面 UV を画面の幅 = 1 の座標へ直す</summary>
	/// <param name="uv">画面 UV</param>
	/// <returns>画面の幅 = 1 の座標（y は縦横比で割る）</returns>
	Vector2 ToWidthSpace(const Vector2& uv) const;
	/// <summary>画面の縦横比を返す</summary>
	/// <returns>幅 / 高さ</returns>
	float GetAspect() const;

	/// <summary>水面の調整値を返す（書き換える時はコピーして SetSettings で戻す）</summary>
	/// <returns>今の値</returns>
	const Settings& GetSettings() const { return settings_; }
	/// <summary>水面の調整値をまとめて差し替える</summary>
	/// <param name="value">新しい値</param>
	void SetSettings(const Settings& value) { settings_ = value; }

	/// <summary>水面の調整値を ImGui で調整する（呼んだ側の窓の中に並べる）</summary>
	void OnImGui();

	// シーン RT と同じ形式（PSO の RTV 形式にも使う）
	static constexpr DXGI_FORMAT kSceneFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

private:
	/// <summary>シーン RT と同じ大きさ・形式の写しのテクスチャと SRV を作る（シーン RT をそのまま CopyResource するため。大きさが変われば作り直す）</summary>
	/// <param name="width">幅 [px]</param>
	/// <param name="height">高さ [px]</param>
	void CreateCopyTexture(uint32_t width, uint32_t height);
	/// <summary>調整値と波の範囲の中心（画面の中心か SetRippleWorldCenter の XZ）を波紋のシミュレーションへ渡す</summary>
	void ApplyRippleSettings();

	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	float screenWidth_ = 0.0f;
	float screenHeight_ = 0.0f;

	RippleSimulation ripple_;

	// シーン RT のコピー先（PS で読む）
	Microsoft::WRL::ComPtr<ID3D12Resource> sceneCopy_;
	uint32_t sceneCopyWidth_ = 0;
	uint32_t sceneCopyHeight_ = 0;
	uint32_t sceneCopySrvIndex_ = UINT32_MAX;

	Settings settings_;

	// SetRippleWorldCenter を呼んだら、波の範囲の中心は画面ではなくこの XZ
	bool useWorldCenter_ = false;
	Vector2 worldCenter_{};
};
