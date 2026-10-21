#pragma once
#include "BoneSocket.h"
#include "Vector3.h"
#include <memory>
#include <vector>

class Object3DManager;
class SkinningComputeManager;
class DirectXCore;
class SRVManager;
class JsonValue;
class AnimatedModelInstance;
class AnimatedObject3DInstance;

/// <summary>
/// 自機の羽根（別の飛行ユニット。PlayerWings、クリップ Deploy / Stow）。体（ゲームの自機 player_）の mixamorig:Spine2 に付ける。
/// 収納中は見えない（2026-10-17 ユーザー決定）。展開は付け根から現れ（Deploy＋点からの距離のディゾルブ kDissolveSphereGrow で
/// Spine2 を中心に半径を広げる）、収納は先から消える（半径を縮める）。ディゾルブの縁は発光パスに描いて光らせる。
/// 雲の上（16_StageStart.md S4、F6）で展開し、レールの間は付けたまま。地上のボス戦では収納する。
/// シューティングの死亡演出では開いた形のまま焼け落ちる（Burn。ノイズのディゾルブでまだらに欠け、縁はオレンジ。19 番 §1.1）。
/// 調整値は StagePlay.json の "playerWings"。
/// </summary>
class PlayerWingUnit {
public:
	enum class State { Hidden, Deploying, Deployed, Stowing, Burning };

	PlayerWingUnit();
	~PlayerWingUnit();

	/// <summary>羽根のモデルを読む（ファイルが無ければ何もしない）</summary>
	void Initialize(Object3DManager* object3DManager, SkinningComputeManager* skinningComputeManager, DirectXCore* dxCore,
		SRVManager* srvManager);

	/// <summary>delay 秒後に展開を始める（たたんだ形・半径 0 から）</summary>
	void Deploy(float delay = 0.0f);
	/// <summary>たたみながら先から消える</summary>
	void Stow();
	/// <summary>開いた形のまま seconds 秒で焼け落ちる（死亡演出）。焼け切ったら Hidden</summary>
	void Burn(float seconds);
	/// <summary>すぐ見えなくする</summary>
	void Hide();
	/// <summary>すぐ開いた形で見せる（デバッグ）</summary>
	void ShowDeployed();
	State GetState() const { return state_; }
	/// <summary>羽根の関節のワールド位置を out に入れる（焼け落ちの粒子を出す所）。見えていなければ空</summary>
	void CollectJointPositions(std::vector<Vector3>& out) const;
	/// <summary>体の点滅の発光（被弾・回避無敵）。羽根の下地に足す</summary>
	void SetBlinkGlow(const Vector3& glow) { blinkGlow_ = glow; }

	/// <summary>体のボーンに付けて行列・ディゾルブを作る。体の Update（スケルトン）の後、カメラが決まった後に呼ぶ</summary>
	void Update(AnimatedObject3DInstance* body, float deltaTime);
	void Draw(DirectXCore* dxCore);
	/// <summary>ディゾルブの縁を光らせている間</summary>
	bool HasBloom() const;
	void DrawBloom(DirectXCore* dxCore);

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	bool IsDrawn() const;
	void ApplyDissolve(float radiusRatio);
	void ApplyBurn(float progress);

	std::unique_ptr<AnimatedModelInstance> model_;
	std::unique_ptr<AnimatedObject3DInstance> wings_;
	BoneSocket socket_;
	AnimatedObject3DInstance* body_ = nullptr;   // 最後に付けた体（参照のみ）

	State state_ = State::Hidden;
	float stateTime_ = 0.0f;
	float delay_ = 0.0f;          // Deploy の待ち（残り）
	float burnSeconds_ = 1.6f;    // Burn で受け取った長さ
	bool dissolving_ = false;     // このフレームにディゾルブをかけている（縁を光らせる）

	// 調整値
	float dissolveInSeconds_ = 1.0f;     // 付け根から先まで現れるまで（Deploy は 1.35 秒）
	float dissolveOutSeconds_ = 0.8f;    // 先から付け根まで消えるまで（Stow は 1.0 秒）
	float radiusMax_ = 1.4f;             // 羽根の全部が入る半径（体の大きさ 1 のとき）[m]。体の拡大率を掛ける
	Vector3 rootOffset_ = { 0.0f, 0.0f, 0.0f };   // ディゾルブの中心＝Spine2 からのずらし（体の大きさ 1、ワールドの向き）[m]
	float edgeWidth_ = 0.06f;            // 光る縁の幅（体の大きさ 1）[m]
	Vector3 edgeColor_ = { 1.6f, 2.2f, 3.0f };
	float noiseScale_ = 9.0f;
	float noiseAmplitude_ = 0.05f;
	float bloomScale_ = 1.0f;
	// 材質の上書き。羽根は金属度 0.45 で青空の cubemap を鏡のように映して真っ青に見えた → 環境光を 0.35 倍にしたら
	// 空からの光ごと消えて黒くなった（2026-10-19 実機）。環境光はそのままで金属度を下げ、白い羽根が光を拡散するようにする
	// → それでも青かった（空の cubemap は水平線より下も濃い青）ので、自機と同じく IBL を切って平行光源で照らし、弱い白の下地を足す
	// → 雲の上の IBL を下が雲の白の物にしたので IBL 1・元の質感・下地なしに戻した（上書きは切替で残す）
	// → 体が上から見ると天頂の青を映すのは IBL を白一色にしても直らず金属感が消えた。羽根だけ環境光を受けにくくする：
	//   金属度 0（空を鏡のように映さない）・環境光 0.3・下地の白 0.1（2026-10-19 ユーザー了承）
	bool overrideMaterial_ = true;
	float metallic_ = 0.0f;
	float roughness_ = 0.6f;
	float environmentCoefficient_ = 0.3f;
	// → それでも平行光源 1 では暗い。下地（発光）は陰影と模様を白で平らにするので 0 に戻し、地の色を明るくする
	//   （テクスチャの色に掛ける倍率。羽根のテクスチャは平均 0.5 の灰色。陰影と模様は残る。2026-10-19 ユーザー了承）
	float albedoGain_ = 3.2f;   // 2026-10-19 ユーザー：1.6 → 3.2
	// → それでも暗いので下地（発光）を少し足す（2026-10-19 ユーザー指示）。上げすぎると陰影と模様が平らになる
	float baseGlow_ = 0.08f;
	Vector3 blinkGlow_ = { 0.0f, 0.0f, 0.0f };

	// 焼け落ち（死亡演出。19 番 §1.1）。縁の色は PlayerFeatherBurn.json の dissolveEdgeColor とそろえる
	Vector3 burnEdgeColor_ = { 1.0f, 0.45f, 0.08f };
	float burnEdgeIntensity_ = 3.0f;    // 縁の色に掛ける（1 を超えてブルームに乗る）
	float burnEdgeWidth_ = 0.15f;       // 縁の幅（ノイズ値 0..1 の単位）
	float burnNoiseScale_ = 3.0f;       // ノイズの細かさ（体の大きさ 1 のとき）[1/m]
	float burnGlow_ = 0.25f;            // 燃えている間に下地へ足すオレンジ（焼け始めから強まる）
};
