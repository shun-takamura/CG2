#pragma once

#include "Framework.h"
#include "PostEffect.h"
#include "RenderTexture.h"
#include "Config/KeyConfig.h"

class GPUParticleManager;
class GameSession;
class ISceneRunner;
class Scene;

/// <summary>
/// ゲーム（ゲーム固有の処理）
/// SceneManagerとSceneFactoryを使用してシーン管理を行う
/// </summary>
class Game : public Framework {
public:
	/// <summary>ウィンドウのタイトルバーに出す文字列。</summary>
	/// <returns>ウィンドウのタイトル</returns>
	const wchar_t* GetWindowTitle() const override { return L"QR"; }

	/// <summary>Framework にシーン駆動の実体（SceneManager）を渡す（依存性の逆転）。</summary>
	/// <returns>SceneManager のインスタンス</returns>
	ISceneRunner* GetSceneRunner() override;

	/// <summary>自分を GetInstance で返すインスタンスとして登録する</summary>
	Game();

	/// <summary>GetInstance の登録を外す</summary>
	~Game();

	/// <summary>
	/// エンジンの初期化の後、エディタのフック・描画解像度・キーコンフィグ・サウンド・GPU パーティクル・
	/// プレハブ・最初のシーン・ポストエフェクトを用意する（起動時に 1 回）
	/// </summary>
	void Initialize() override;

	/// <summary>EffectManager・GPU パーティクル・ポストエフェクト（Debug はビューポート用 RT も）を GPU 停止前に片付けてからエンジンを終了する</summary>
	void Finalize() override;

	/// <summary>描画解像度の変更を反映し、エンジンとシーンを 1 フレーム進める（F8 でデモモードの切り替え）</summary>
	void Update() override;

	/// <summary>1 フレームの描画パス（影 → シーン → マスク → 歪み → 発光 → 合成と重ね描き → ImGui）を順に実行して画面に出す</summary>
	void Draw() override;

	/// <summary>ポストエフェクト（シーンを描く RenderTexture も持つ）を取得する</summary>
	/// <returns>ポストエフェクト（初期化前は nullptr）</returns>
	static PostEffect* GetPostEffect() { return postEffect_.get(); }

	/// <summary>
	/// 全シーン共通の GPU パーティクル管理を取得する（EffectManager もこれを参照する）。
	/// シーン側で個別に Initialize する必要はない
	/// </summary>
	/// <returns>GPU パーティクル管理（初期化前は nullptr）</returns>
	static GPUParticleManager* GetGPUParticleManager() { return gpuParticleManager_.get(); }

	/// <summary>キーコンフィグのオプション（精密射撃モードの hold/toggle など）を取得する</summary>
	/// <returns>オプション</returns>
	const KeyConfig::Options& GetKeyConfigOptions() const { return keyConfigOptions_; }

private:
	// ----- 1 フレームの描画パス（Draw がこの順に呼ぶ。順番の理由は Game.cpp の各関数のコメント） -----
	/// <summary>影の深度（CSM）を書く。受光側のリソースは Object3DManager に渡す</summary>
	void RenderShadowPass();
	/// <summary>シーン本体を PostEffect の RenderTexture に描く</summary>
	void RenderScenePass();
	/// <summary>強調表示（アウトライン）の対象を idMaskRT に書く</summary>
	void RenderIdMaskPass();
	/// <summary>エフェクトの歪み源を distortionRT に書く（歪み源が無いフレームは飛ばす）</summary>
	void RenderDistortionPass();
	/// <summary>光らせる物だけを発光 RT（bloomRT）に書く（発光源が無いフレームは飛ばす）</summary>
	void RenderBloomPass();
	/// <summary>ここまでの RT をポストエフェクトで合成し、その上に合成後に重ねる物を描く（Release は Swapchain、Debug はビューポート用 RT へ）</summary>
	void RenderPostEffectAndOverlay();

	/// <summary>描画解像度いっぱいのビューポート／シザーで RT（と任意の DSV）をバインドする</summary>
	/// <param name="rtv">バインドする RT</param>
	/// <param name="dsv">バインドする DSV（nullptr なら深度なし）</param>
	void BindRenderSizeTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const D3D12_CPU_DESCRIPTOR_HANDLE* dsv);

	// 起動時の描画解像度（UI の仮想解像度 1600×900 とは別。--render-res で上書き）
	static constexpr int32_t kDefaultRenderWidth = 1920;
	static constexpr int32_t kDefaultRenderHeight = 1080;

	// ポストエフェクト（RenderTextureも内部で管理）
	static std::unique_ptr<PostEffect> postEffect_;

	// 全シーン共通の GPU パーティクル管理
	static std::unique_ptr<GPUParticleManager> gpuParticleManager_;

	// キーコンフィグのオプション
	KeyConfig::Options keyConfigOptions_;

	// 1 回の起動の間シーンをまたいで残るプレイの状態（エーテル・装備・スコアなど）。SceneManager が各シーンへ渡す
	std::unique_ptr<GameSession> gameSession_;

	// ブルームの掛け率。トランジションが画面を覆う間は 0 へ、開く間は 1 へ戻す
	// （覆いはシーンの絵の中に描かれ、ブルームはその上から足されるので、覆いの上に光が残ってしまう）
	float bloomFade_ = 1.0f;
	static constexpr float kBloomFadeSeconds = 0.25f;

#ifdef _DEBUG
	/// <summary>
	/// ゲーム型に触るエディタウィンドウ／パネルを ImGuiManager へ追加登録する。
	/// エンジン側はゲームを名指しできないため、配線はこちらから行う（依存性の逆転）。
	/// </summary>
	void RegisterGameEditorWindows();

	/// <summary>ポストエフェクト合成後に重ねる物を viewportRT に描く（Debug はビューポート表示用 RT へ出力するため）</summary>
	/// <param name="scene">今のシーン</param>
	void DrawOverlayOnViewportTexture(Scene* scene);

	// Debug ビルド専用: ImGui ViewportWindow に表示する PostEffect 適用後の最終出力
	std::unique_ptr<RenderTexture> viewportRenderTexture_;
#endif
};
