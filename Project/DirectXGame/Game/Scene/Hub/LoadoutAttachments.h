#pragma once
#include "BoneSocket.h"
#include <memory>

class AnimatedObject3DInstance;
class BeamShieldRenderer;
class Camera;
class DirectXCore;
class FlightUnitPart;
class Object3DInstance;
class Object3DManager;
class PlayerWingUnit;
class SkinningComputeManager;
class SRVManager;
struct MainWeaponDef;
struct ShieldDef;

/// <summary>
/// HUB の機体（HubMechModel の本体）に、いま付けている装備（LoadoutState）を付けて見せる（20_Loadout.md の装備画面）。
///  - メイン武器（右手）・盾（左手。ビームシールドならビームも）：ステージ（StagePlayScene）と同じく手のボーンへ握りのずらしで付ける
///  - 飛行ユニット：FlightUnitPart をモデルだけ使う（技は動かさない。窓口はここの中の預かり物 PreviewHost）
///  - 初期の羽根（Wings）：PlayerWingUnit を開いた形で
/// 付ける所のずらしは「骨のスケールを除いたメートル」なので、18m の機体には体の拡大率を掛けて付ける。
/// 付け替えたら Rebuild（モデルの差し替え）。描画は持ち主（HubScene）が Draw / DrawTransparent / DrawBloom を呼ぶ。
/// </summary>
class LoadoutAttachments {
public:
	/// <summary>ID マスクに描く部分（付け替えのノイズは替えた部分だけにかける）</summary>
	enum class Part {
		All,          // 武器・盾・背中のユニット・羽根
		MainWeapon,
		Shield,
		FlightUnit,   // 背中のユニットと羽根
	};

	LoadoutAttachments();
	~LoadoutAttachments();

	void Initialize(Object3DManager* object3DManager, SkinningComputeManager* skinningComputeManager,
		DirectXCore* dxCore, SRVManager* srvManager, Camera* camera);
	/// <summary>LoadoutState に合わせて付け直す（付け替えた直後に呼ぶ）</summary>
	void Rebuild();
	/// <summary>body のスケルトンを更新した後に呼ぶ。dt は羽根・ビームの時間</summary>
	void Update(AnimatedObject3DInstance* body, float deltaTime);

	/// <summary>不透明（武器・盾・背中のユニット・羽根）</summary>
	void Draw(DirectXCore* dxCore);
	/// <summary>半透明（ビームシールド）。不透明物の後。ルートシグネチャを替える</summary>
	void DrawTransparent(const Camera& camera);
	bool HasBloom() const;
	void DrawBloom(DirectXCore* dxCore, const Camera& camera);
	/// <summary>ID マスクへ（ノイズを機体だけ・機体以外・替えた部分だけにかけるため）</summary>
	void DrawIdPass(DirectXCore* dxCore, Part part);

	/// <summary>飛行ユニット（装備画面が付けていない系統の値も読む）</summary>
	const FlightUnitPart* GetFlightUnit() const { return flightUnit_.get(); }
	/// <summary>
	/// 飛行ユニットの調整（付ける所の位置・向き。FlightUnit.json に Save）。ステージと同じ ImGui で、
	/// 敵・レティクルが無い所でモデルを止めて合わせられる。系統を替えたら付け直す
	/// </summary>
	void OnImGuiFlightUnit();

private:
	class PreviewHost;
	bool IsShieldBeamShown() const;

	DirectXCore* dxCore_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	Camera* camera_ = nullptr;
	AnimatedObject3DInstance* body_ = nullptr;   // 最後に Update で受け取った体（参照のみ）

	std::unique_ptr<Object3DInstance> weapon_;
	std::unique_ptr<Object3DInstance> shield_;
	std::unique_ptr<BeamShieldRenderer> beamShield_;
	BoneSocket weaponSocket_;
	BoneSocket shieldSocket_;
	// Rebuild の時点の定義（付け替えはノイズの山で入れ替えるので、LoadoutState が先に変わっても握りは今のモデルのまま）
	const MainWeaponDef* weaponDef_ = nullptr;
	const ShieldDef* shieldDef_ = nullptr;
	bool weaponShown_ = false;
	bool shieldShown_ = false;

	std::unique_ptr<PreviewHost> host_;
	std::unique_ptr<FlightUnitPart> flightUnit_;
	std::unique_ptr<PlayerWingUnit> wings_;
};
