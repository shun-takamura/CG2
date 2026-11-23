#pragma once
#include "BaseTransition.h"
#include "FeatherRenderer.h"
#include "ScreenWaterSurface.h"
#include "Vector2.h"
#include "Vector3.h"
#include <wrl.h>
#include <d3d12.h>
#include <memory>
#include <random>
#include <vector>

class SpriteInstance;

/// <summary>
/// 羽根の飛翔（リトライ → ステージ）。15_SceneTransition.md T4。着水（FeatherLandTransition）と対。
///   覆う：画面の右下寄り（originUV）に一枚がディゾルブの逆再生でよみがえる → 右下から左上へ吹き上がる突風で飛び立ち、
///        右下の外から羽根が吹き込んで**羽根だけで画面を埋め尽くす**（ここでロード。
///        ロードの間も吹き込み続けて埋めたまま）
///   開く：吹き込みが止まり、残りの羽根が左上へ抜けて晴れると次のシーンになっている
///        （白フェード版は whiteFade で切り替えられる。来週はスタート演出の「白から始まる後半」に任せる案もある）
/// 描く順：シーン＋隙間埋め（ScreenRipple.PS）→ 羽根 →（白フェード版のみ）白。
/// よみがえる所は入口（死亡・ポーズ）に関係なく決まった位置（2026-10-13 ユーザー指示：どの羽根の飛翔も右下から左上へ吹き抜けて画面を覆う）。
/// 飛び立つ所の波紋は入れない（2026-10-11 ユーザー指示）。ScreenRipple.PS の波紋の読み出しは平らな水面なので素通し。
/// </summary>
class FeatherRiseTransition : public BaseTransition {
public:
	struct Settings {
		// 時間
		float reformTime = 0.4f;          // よみがえる（逆ディゾルブ）長さ
		float gustStart = 0.35f;          // 突風が吹き始める時刻
		float liftStart = 0.45f;          // よみがえった一枚が飛び立つ時刻
		float coverTime = 1.5f;           // 覆い終わる（ロードする）時刻
		bool whiteFade = false;           // 白フェードで覆う版（false = 羽根だけで埋め尽くす）
		float whiteStart = 0.9f;          // 白フェード版：白くなり始める時刻
		float whiteTime = 0.6f;           // 白フェード版：真っ白になるまで
		float revealTime = 0.6f;          // 白フェード版：開く側で白が晴れるまで（羽根だけの版は羽根が抜け切ったら終わり）

		// 仮想カメラ（正面で固定）
		float fovY = 0.9f;

		// 風（画面の右下から左上へ）
		Vector2 windDirection = { -1.0f, 0.75f }; // 画面の向き（x = 右、y = 上）
		float windSpeedMin = 3.0f;        // 羽根の飛ぶ速さ（毎秒）
		float windSpeedMax = 5.5f;
		float turbulence = 0.8f;          // 風に直交する向きの揺れ（毎秒）
		float turbulenceFrequency = 3.0f; // その揺れの速さ（ラジアン / 秒）
		float spinSpeed = 6.0f;           // 画面の中でくるくる回る速さの上限（ラジアン / 秒）
		float faceTilt = 0.5f;            // 面をこちらへ向けたまま傾ける量の上限（大きいと細く見えて画面を覆えない）
		float tumbleSpeed = 1.5f;         // その傾きが揺れる速さの上限（ひらひら）

		// 吹き込む羽根
		float spawnRateStart = 120.0f;    // 突風の吹き始めの 1 秒あたりの枚数
		float spawnRateEnd = 1400.0f;     // 覆い終わる頃の枚数
		float nearDepth = 1.5f;           // 奥行き（カメラからの距離）の手前（近いほど大きく見えて画面を埋める）
		float farDepth = 7.0f;            // 同・奥
		float nearBias = 3.0f;            // 奥行きを手前へ寄せる強さ（1 = 均等、大きいほど手前が多い）
		float sizeMin = 0.35f;            // 羽根の長さ（奥）
		float sizeMax = 1.6f;             // 羽根の長さ（手前）
		int burstCount = 80;              // 飛び立つ瞬間、よみがえった一枚のまわりから一緒に舞い上がる枚数
		float burstOffsetX = 0.3f;        // 一緒に舞い上がる羽根を出す所を、よみがえった一枚から左右へずらす量（画面の幅に対する割合。正で右）

		// よみがえる一枚
		Vector2 originUV = { 0.7f, 0.7f }; // よみがえる画面の位置（UV、右下寄り）
		float heroDepth = 4.0f;           // 奥行き
		float heroSize = 0.9f;            // 長さ
		float heroLiftAcceleration = 7.0f;// 飛び立つ加速（毎秒毎秒）

		// 隙間埋め：羽根の後ろに白っぽいふわふわした層を重ね、覆い切れない隙間から後ろが見えないようにする
		bool gapFill = true;
		float fillStart = 0.9f;           // 重ね始める時刻（覆い終わりで濃さ 1）
		float fillOutTime = 0.5f;         // 開く側で薄れ切るまで（羽根が抜けるより先に薄れて、隙間から次のシーンが見えてくる）
		float fillNoiseScale = 2.5f;      // 濃淡の細かさ


		// JSON との行き来（TransitionTuning.h）。項目を足したらここにも足す
		template <class V>
		void Visit(V& v) {
			v("reformTime", reformTime);
			v("gustStart", gustStart);
			v("liftStart", liftStart);
			v("coverTime", coverTime);
			v("whiteFade", whiteFade);
			v("whiteStart", whiteStart);
			v("whiteTime", whiteTime);
			v("revealTime", revealTime);
			v("fovY", fovY);
			v("windDirection", windDirection);
			v("windSpeedMin", windSpeedMin);
			v("windSpeedMax", windSpeedMax);
			v("turbulence", turbulence);
			v("turbulenceFrequency", turbulenceFrequency);
			v("spinSpeed", spinSpeed);
			v("faceTilt", faceTilt);
			v("tumbleSpeed", tumbleSpeed);
			v("spawnRateStart", spawnRateStart);
			v("spawnRateEnd", spawnRateEnd);
			v("nearDepth", nearDepth);
			v("farDepth", farDepth);
			v("nearBias", nearBias);
			v("sizeMin", sizeMin);
			v("sizeMax", sizeMax);
			v("burstCount", burstCount);
			v("burstOffsetX", burstOffsetX);
			v("originUV", originUV);
			v("heroDepth", heroDepth);
			v("heroSize", heroSize);
			v("heroLiftAcceleration", heroLiftAcceleration);
			v("gapFill", gapFill);
			v("fillStart", fillStart);
			v("fillOutTime", fillOutTime);
			v("fillNoiseScale", fillNoiseScale);
		}
	};

	FeatherRiseTransition() = default;
	~FeatherRiseTransition() override;

	/// <summary>羽根の描画・水面・白のスプライト・定数バッファを解放する</summary>
	void Finalize() override;
	/// <summary>シーン＋隙間埋め → 羽根 →（白フェード版のみ）白 の順に描く（真っ白の間は白だけ）</summary>
	void Draw() override;

	/// <summary>トランジションの種類を返す</summary>
	/// <returns>TransitionType::FeatherRise</returns>
	TransitionType GetType() const override { return TransitionType::FeatherRise; }
	/// <summary>トランジション名を返す</summary>
	/// <returns>"FeatherRise"</returns>
	std::string GetName() const override { return "FeatherRise"; }

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
	/// <summary>羽根と水面を最初に戻し、よみがえる一枚を用意する</summary>
	void OnStartCover() override;
	/// <summary>水面・隙間埋めの全画面パス・定数バッファ・羽根の描画（陰を明るく）・白のスプライトを作る</summary>
	void OnInitialize() override;
	/// <summary>覆う秒数（coverTime）が過ぎるのを待つ</summary>
	/// <param name="dt">経過 [秒]（使わない）</param>
	/// <returns>覆い終えたら true（ここでロードする）</returns>
	bool UpdateCover(float dt) override;
	/// <summary>白を薄め、白フェード版は白が消え切るまで、羽根だけの版は残りの羽根が画面から抜け切るまで待つ</summary>
	/// <param name="dt">経過 [秒]（使わない）</param>
	/// <returns>晴れ切ったら true</returns>
	bool UpdateReveal(float dt) override;
	/// <summary>白の濃さを 1 にする</summary>
	void SetFullyCovered() override;
	/// <summary>突風の間は羽根を吹き込ませ、よみがえる一枚・飛ぶ羽根・水面・仮想カメラを進める</summary>
	void UpdateVisual() override;

private:
	// 風に乗って飛ぶ羽根1枚
	struct FlyingFeather {
		Vector3 position;
		Vector3 velocity;
		Vector3 spin;            // 各軸の回る速さ
		float turbulencePhase;
		float flutterPhase;
		float baseBend;
		FeatherRenderer::Feather look;
	};

	// ScreenRipple.PS.hlsl の ScreenRippleParams と 1:1（48 バイト）
	struct RippleParamsForGPU {
		ScreenWaterSurface::ConstantsForGPU water;
		float fill;
		float fillNoiseScale;
		float time;
		float padding0;
	};

	/// <summary>風上（右下）の画面の外から羽根を 1 枚吹き込ませる（風向きの線を均等に並べて角も中央も同じ濃さで覆う）</summary>
	void SpawnFromUpwind();
	/// <summary>面をこちらへ向けた向きと回り方をランダムに決める（画面を覆えるように）</summary>
	/// <param name="f">決める羽根</param>
	void RandomizeFacing(FlyingFeather& f);
	/// <summary>飛び立つ瞬間に、よみがえった一枚のまわりから羽根を一緒に舞い上がらせる</summary>
	void SpawnBurst();
	/// <summary>飛ぶ羽根を風に乗せて揺らしながら進め、風下（左上）の外へ抜けたものを捨てる</summary>
	/// <param name="dt">経過 [秒]</param>
	void UpdateFlying(float dt);
	/// <summary>よみがえる一枚をディゾルブの逆再生でよみがえらせ、風に乗って飛び立たせる</summary>
	/// <param name="dt">経過 [秒]</param>
	void UpdateHero(float dt);
	/// <summary>白の濃さを返す（白フェード版のみ）</summary>
	/// <returns>0〜1。白フェード版でなければ 0</returns>
	float Whiteness() const;
	/// <summary>隙間埋めの濃さを返す</summary>
	/// <returns>0〜1。隙間埋めを使わなければ 0</returns>
	float GapFill() const;
	/// <summary>画面の向きの風を返す</summary>
	/// <returns>正規化済みの向き（z = 0）。調整値がほぼ 0 なら左上向きの既定</returns>
	Vector3 WindDirection() const;
	/// <summary>カメラからの距離 depth の所で、画面の中心から上端までの高さを求める</summary>
	/// <param name="depth">カメラからの距離</param>
	/// <returns>高さ（ワールドの長さ）</returns>
	float VisibleHalfHeight(float depth) const;
	/// <summary>画面 UV を奥行き depth の所のワールドの位置へ直す（カメラは原点から +Z を見る）</summary>
	/// <param name="uv">画面 UV</param>
	/// <param name="depth">奥行き</param>
	/// <returns>ワールドの位置</returns>
	Vector3 ScreenToWorld(const Vector2& uv, float depth) const;

	FeatherRenderer renderer_;
	ScreenWaterSurface water_;
	Settings settings_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rippleRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> ripplePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> rippleParamsResource_;
	RippleParamsForGPU* rippleParamsData_ = nullptr;
	std::unique_ptr<SpriteInstance> whiteSprite_;

	std::vector<FlyingFeather> flying_;
	FlyingFeather hero_{};
	bool heroLifted_ = false;
	std::vector<FeatherRenderer::Feather> drawList_;
	std::mt19937 random_{ 20261012u };
	float spawnAccumulator_ = 0.0f;
	float time_ = 0.0f;
	float fade_ = 1.0f;   // 開く側：白の濃さ 1→0
};
