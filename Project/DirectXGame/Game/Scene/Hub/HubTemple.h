#pragma once
#include "Vector3.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

class Object3DInstance;
class Object3DManager;
class DirectXCore;
class SRVManager;
class Camera;
class JsonValue;
class WaterSurface;
class WaterReflection;
class AnimatedObject3DInstance;

/// <summary>
/// HUB の神殿（17_HubScene.md §2 / H1・H2）。パルテナの神殿に寄せた虹色の聖堂と、金の天球儀の輪。
/// 形は tools/BlenderPipeline/gen_hub_temple.py の手続き生成（殻・時計の門・門の針と歯車・天球儀の輪 4 本）、
/// 扉はタイトルの扉のモデルを時計の門の通り道（踊り場の上）に置く。
/// +Z 側（ステージ選択の背景・降りるときに見える側）が時計の門。針と歯車は門の面の中でゆっくり回す。
/// 床は WaterSurface を「磨いた大理石」として使う（水深ほぼ 0・波なし・反射率を上げる）。
/// 映り込みは WaterReflection（半分の解像度にして少しぼかす）。映らなかった所は神殿の IBL を引く。
/// 原点＝機体の足元の中央、正面（門・足場の側）は +Z。
/// </summary>
class HubTemple {
public:
	HubTemple();
	~HubTemple();

	/// <summary>殻・時計の門と針と歯車・天球儀の輪・扉を読み込み、IBL・床の映り込み・鏡の床を用意する</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="srvManager">SRV の管理</param>
	void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, SRVManager* srvManager);
	/// <summary>輪・針・歯車を回し、床の時間を進める</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);

	/// <summary>床に映す物を足す（機体など）</summary>
	/// <param name="target">映す物</param>
	void AddReflectionTarget(Object3DInstance* target);
	/// <summary>床に映す物から外す（コクピットの中で機体の本体を隠している間など）</summary>
	/// <param name="target">外す物</param>
	void RemoveReflectionTarget(Object3DInstance* target);
	/// <summary>
	/// アニメーションする物（機体の本体＝自機の新モデル）を床に映す。外す口は無いので、隠すときは
	/// AnimatedObject3DInstance::SetVisible(false)（映り込みの描画も表示フラグを見る）
	/// </summary>
	/// <param name="target">映す物</param>
	void AddReflectionTarget(AnimatedObject3DInstance* target);
	/// <summary>床の反射 RT を描く。シーン RT を貼る前に呼ぶ（呼んだ後は RTV/DSV が反射 RT のままなので貼り直すこと）</summary>
	/// <param name="camera">シーンのカメラ</param>
	void RenderReflection(const Camera& camera);
	/// <summary>神殿の不透明物を描く（Object3D の DrawSetting・BindLights の後）</summary>
	/// <param name="dxCore">DirectX の中核</param>
	void Draw(DirectXCore* dxCore);
	/// <summary>鏡の床を描く。不透明物を描いた後に呼ぶ（ルートシグネチャを替えるので、後で Object3D を描くなら DrawSetting し直す）</summary>
	/// <param name="camera">シーンのカメラ</param>
	void DrawFloor(const Camera& camera);

	/// <summary>
	/// 床に八角形の穴を開ける / 閉じる（③ のリフト。16_StageStart.md §2）。
	/// リフトが床の高さにある間は開けないこと（機体の映り込みが消える）
	/// </summary>
	/// <param name="open">true で開ける</param>
	/// <param name="halfWidth">八角形の半幅 [m]（StageStartGeometry）</param>
	/// <param name="chamfer">八角形の角の面取り [m]（StageStartGeometry）</param>
	void SetFloorHole(bool open, float halfWidth, float chamfer);

	/// <summary>神殿の IBL のパスを返す（無ければタイトルの空の IBL）。Object3DManager::SetEnvironmentTexture に渡す</summary>
	/// <returns>パス</returns>
	const std::string& GetIblPath() const { return iblPath_; }

	/// <summary>調整値（Hub.json の "temple"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	struct Ring {
		std::unique_ptr<Object3DInstance> object;
		Vector3 center;
		float tiltY = 0.0f;           // 度。輪の面の法線（ローカル X）を Y → Z の順に倒す
		float tiltZ = 0.0f;
		float spinDegPerSec = 0.0f;   // 輪の面の中の回転
		float angle = 0.0f;           // 今の回転（度）
	};
	// 時計の門の動く部分（針・歯車）。門の面（XY）の中で Z 軸まわりに回る
	struct GateMover {
		std::unique_ptr<Object3DInstance> object;
		Vector3 offset;               // 門の中心からの位置（z は奥行き。− が手前）
		float scale = 1.0f;           // 歯車は半径 1 のモデルを (scale, scale, 1) に
		float spinDegPerSec = 0.0f;
		float angle = 0.0f;
	};

	/// <summary>タイトルの扉を表の面を神殿の中へ向けて置き、共有のマテリアルからタイトル用の値（ディゾルブ・雲の映り込み・環境光）を外す</summary>
	void ApplyDoor();
	/// <summary>床の調整値（フレネルの F0・反射の強さ・環境光）を鏡の床へ反映する</summary>
	void ApplyFloorParams();

	std::unique_ptr<Object3DInstance> shell_;
	std::unique_ptr<Object3DInstance> gate_;
	std::vector<GateMover> gateMovers_;
	std::array<Ring, 4> rings_;
	std::unique_ptr<Object3DInstance> doorFrame_;
	std::unique_ptr<Object3DInstance> doorLeafL_;
	std::unique_ptr<Object3DInstance> doorLeafR_;
	std::unique_ptr<WaterSurface> floor_;
	std::unique_ptr<WaterReflection> reflection_;
	std::string iblPath_;

	// 調整値（Hub.json の "temple"）
	Vector3 doorPosition_ = { 0.0f, 10.6f, 19.6f };   // 扉の台座の中心（時計の門の通り道、踊り場の上）
	float doorScale_ = 2.0f;      // タイトルの扉は人の大きさ（高さ 3.2m）。神殿に合わせて大きくする
	// 扉の環境光（IBL）の強さ。マテリアルはタイトルと共有なので、タイトルの値（大理石 0.25・金 0.56）を上書きする
	float doorMarbleEnv_ = 1.0f;
	float doorGoldEnv_ = 1.0f;
	float gateSpeedScale_ = 1.0f; // 針と歯車の回る速さの倍率
	float floorF0_ = 0.2f;        // 磨いた大理石の映り込み（垂直に見たときの反射率）。パルテナの床に寄せて強め
	float floorReflection_ = 1.0f;
	float floorAmbient_ = 0.55f;  // 床の環境光
	bool ringsSpin_ = true;
	bool drawRings_ = true;
	bool drawFloor_ = true;
};
