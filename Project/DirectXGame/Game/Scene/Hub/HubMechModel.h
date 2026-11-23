#pragma once
#include "Vector3.h"
#include <memory>
#include <string>

class Object3DInstance;
class Object3DManager;
class AnimatedModelInstance;
class AnimatedObject3DInstance;
class SkinningComputeManager;
class SRVManager;
class CockpitModel;
class DirectXCore;
class JsonValue;

/// <summary>
/// HUB 用の機体（17_HubScene.md §5.1）。本体と、コクピット・上のハッチ（CockpitModel）をまとめて置く。
/// 原点＝足元の中央、正面はエンジンの +Z。ハッチはコクピットの軸（X 軸に平行）まわりに回る。
/// HUB（乗り込み・降りる）とステージ選択（コクピットの内側）で使い回す。
/// コクピットだけは StagePlay のスタート演出でも使うので Cockpit/CockpitModel に分けてある（16_StageStart.md §4.1）。
/// 2026-10-18 本体を自機の新モデル（PlayerModel、スキニング）に替えた。開くハッチが無いので、乗り込み・降りるでは
/// 胸の装甲を光のディゾルブ（ステージ選択と同じ六角形を胸の前に。kDissolveHexHole）で消して入口を開ける。ハッチ（CockpitModel）は中の扉として残す
/// </summary>
class HubMechModel {
public:
	/// <summary>右手の動き（PlayerModel のクリップ。rig_player_mech.py の BODY_CLIPS）</summary>
	enum class ArmClip {
		Offer,     // 直立 → 手を胸の前へ差し出す（時刻 0 は直立＝Stand と同じ）。最後のコマで止まる
		Retract,   // 差し出した手を戻して直立へ
	};

	/// <summary>機体の見え方（乗り込み・降りるの時間軸が時刻から決める）</summary>
	struct Pose {
		ArmClip arm = ArmClip::Offer;
		float armTime = 0.0f;     // クリップの時刻 [秒]（長さを超えたら最後のコマ）
		float chestOpen = 0.0f;   // 胸の入口の開き具合（0 = 閉じている、1 = 開き切り）
	};

	HubMechModel();
	~HubMechModel();

	/// <summary>本体（自機の新モデル、スキニング）とコクピット・ハッチを読み込み、今の位置・見え方を反映する</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="skinningComputeManager">スキニングの計算の管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="srvManager">SRV の管理</param>
	void Initialize(Object3DManager* object3DManager, SkinningComputeManager* skinningComputeManager,
		DirectXCore* dxCore, SRVManager* srvManager);
	/// <summary>位置・本体の表示・腕のクリップと時刻・胸の入口を反映して、本体とコクピットを更新する</summary>
	void Update();
	/// <summary>コクピットとハッチ、本体を描く（コクピットを先に）</summary>
	/// <param name="dxCore">DirectX の中核</param>
	void Draw(DirectXCore* dxCore);
	/// <summary>発光パス：コクピットとハッチの「光る分」と、胸の入口の光る縁を発光 RT に描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="bloomScale">発光 RT に書くときの倍率</param>
	/// <param name="includeAll">true ならコクピットの発光していない部位も描く（六角形のディゾルブの発光帯を光らせる間）</param>
	void DrawBloom(DirectXCore* dxCore, float bloomScale, bool includeAll);

	/// <summary>本体を返す（神殿の鏡の床に映すため。HubTemple）</summary>
	/// <returns>本体。Initialize 前は nullptr</returns>
	AnimatedObject3DInstance* GetBody() const { return body_.get(); }
	/// <summary>コクピットを返す（コクピットの中の見た目＝灯り・発光・六角形を部位のマテリアルで変えるため。CockpitLighting）</summary>
	/// <returns>コクピット（Initialize 後に呼ぶこと）</returns>
	CockpitModel& GetCockpitModel() const { return *cockpit_; }
	/// <summary>コクピットの内装を返す</summary>
	/// <returns>コクピットの内装</returns>
	Object3DInstance* GetCockpit() const;
	/// <summary>ハッチを返す</summary>
	/// <returns>ハッチ</returns>
	Object3DInstance* GetHatch() const;

	/// <summary>ハッチの開き具合を設定する</summary>
	/// <param name="ratio">1 = 開いて待っている、0 = 閉じ切り</param>
	void SetHatchOpenRatio(float ratio);
	/// <summary>ハッチの開き具合を返す</summary>
	/// <returns>1 = 開いて待っている、0 = 閉じ切り</returns>
	float GetHatchOpenRatio() const;

	/// <summary>腕のクリップと時刻・胸の入口の開き具合を設定する。反映は Update</summary>
	/// <param name="pose">見え方</param>
	void SetPose(const Pose& pose) { pose_ = pose; }

	/// <summary>
	/// 本体を描かない（コクピットとハッチは描く）。コクピットの中にいる間は全天周のモニターに自分の機体が映り込まないよう隠す。
	/// 床の映り込みも描かない（AnimatedObject3DInstance の表示フラグを見る）。
	/// StagePlay の一人称も本体を描かないので、③ の切れ目の前後で見え方がそろう
	/// </summary>
	/// <param name="hidden">true で本体を隠す</param>
	void SetBodyHidden(bool hidden) { bodyHidden_ = hidden; }
	/// <summary>本体を隠しているかを返す</summary>
	/// <returns>隠していれば true</returns>
	bool IsBodyHidden() const { return bodyHidden_; }

	/// <summary>機体の原点（足元の中央）を返す。乗り込みのカメラのキーはこの原点からの相対で持つ</summary>
	/// <returns>ワールド位置</returns>
	const Vector3& GetPosition() const { return position_; }
	/// <summary>機体の原点を設定する（③ のリフトの降下で機体ごと下げる。16_StageStart.md）。反映は Update</summary>
	/// <param name="position">ワールド位置</param>
	void SetPosition(const Vector3& position) { position_ = position; }

	/// <summary>調整値（Hub.json の "mech"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	/// <summary>腕のクリップを（変わったときだけ）読み込み、止めたまま pose_ の時刻にする</summary>
	void ApplyArm();
	/// <summary>胸の入口の開き具合を、胸の装甲の六角形のディゾルブにする（0 なら消す）</summary>
	void ApplyChest();

	std::unique_ptr<AnimatedModelInstance> bodyModel_;
	std::unique_ptr<AnimatedObject3DInstance> body_;
	std::unique_ptr<CockpitModel> cockpit_;

	Vector3 position_ = { 0.0f, 0.0f, 0.0f };
	bool bodyHidden_ = false;
	Pose pose_{};
	// 今読み込んでいるクリップ（切り替えるときだけ .anim を読む）
	bool armLoaded_ = false;
	ArmClip loadedArm_ = ArmClip::Offer;

	// 調整値（Hub.json の "mech"。ハッチの開いた角度 hatchOpenDegrees は CockpitModel が持つ）
	bool drawBody_ = true;   // デバッグ：本体を隠してコクピットとハッチだけ見る
	// 本体の拡大率。カタパルト（StageStartPart の mechScale）と同じ 18m（＝ 18 / 1.694）
	float bodyScale_ = 10.625f;
	// 胸の入口（機体の原点からの相対。コクピットの入口 z 1.565 の前の胸の装甲を、六角形のマスで縦長の楕円に消す。
	// 2026-10-18 ユーザー：点からの距離の球だと装甲がめり込んで見えたので、ステージ選択の六角形と同じ見た目に）
	Vector3 chestCenter_ = { 0.0f, 12.4f, 2.3f };
	float chestRadius_ = 1.25f;          // 開き切りで消えている広がり [m]
	float chestStretchY_ = 1.35f;        // 縦の伸び（入口は 1.4×1.8m の縦長）
	float chestEdgeWidth_ = 0.35f;       // 光る帯の幅 [m]
	Vector3 chestEdgeColor_ = { 0.0f, 1.538f, 2.4f };   // エーテルの水色 (0, 0.641, 1.0) × 2.4（19 番 §1.3・§4.3）
	float chestCellSize_ = 0.35f;        // 六角形の大きさ [m]
	float chestCellJitter_ = 0.3f;       // マスごとのばらつき [m]
	float chestBloomScale_ = 1.0f;       // 光るマスを発光 RT に書く倍率
};
