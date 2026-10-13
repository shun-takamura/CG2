#pragma once
#include "GameScene.h"
#include "VerticalMenu.h"
#include "Hub/HubBoardingSequence.h"
#include "Hub/HubDisembarkSequence.h"
#include "Hub/CockpitReveal.h"
#include "AttractMode.h"
#include "Vector2.h"
#include <memory>

class Camera;
class Skybox;
class Object3DInstance;
class HubMechModel;
class HatchSign;
class CockpitLighting;
class CockpitConsole;

/// <summary>
/// ハブシーン（神殿。17_HubScene.md）。**ステージ選択（コクピット）もこのシーンの中**（2026-10-13 統合。18 番 ①「C で決まったこと」）。
/// 状態のまとめ役で、中身は Hub/ の部品がする：
///   待機（回廊から機体を見る。メニューは「搭乗」だけ）
///   → 乗り込み（HubBoardingSequence。18 番 ①の OUT）
///   → コクピット（CockpitReveal の時間軸で、表示板 HatchSign が LOCKED → 六角形で外が透ける CockpitLighting
///      → ラジアルブラー → 文字のステージ選択 CockpitConsole。①の IN）
///   → 「HUB に戻る」でシャットダウン（②の OUT）→ 降りる（HubDisembarkSequence。②の IN）→ 待機
/// 入口：TITLE から＝待機／シーン名 STAGESELECT（ポーズの「ステージセレクトに戻る」⑩）＝コクピットの起動から／
///   別のシーンから HUB へ（⑦。今はステージからデバッグの F3）＝真っ暗なコクピットに灯りが点いて（CockpitReveal の帰着）から降りる。
/// キャンセルで白フェードしてタイトルへ（18 番 ⑧）。ゲーム終了はタイトルの責務に一本化している。
/// 神殿のモデル（17 番 H1）ができるまで、床と回廊は石畳の板・背景はタイトルの空で仮に置く。
/// 調整値は Resources/Json/Tuning/Hub.json（ImGui の「Hub Tuning」で保存 / 読み直し）。
/// </summary>
class HubScene : public GameScene {
public:
	enum class Entry {
		Temple,      // 回廊で待機から（タイトルから）
		Cockpit,     // コクピットの起動から（シーン名 STAGESELECT）
		Disembark,   // 真っ暗なコクピットから灯りを点けて降りる（⑦。SceneFactory が前のシーンで決める）
	};

	explicit HubScene(Entry entry = Entry::Temple);
	~HubScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override;

	bool HasBloomSources() const override;
	void DrawBloomSources() override;

	/// <summary>調整値の ImGui（Game.cpp の「Hub Tuning」窓から呼ぶ）</summary>
	void OnImGuiTuning();

private:
	enum class State {
		Waiting,     // 回廊で待機
		Boarding,    // 乗り込み
		Cockpit,     // コクピット（起動・ステージ選択・シャットダウン）
		Disembarking,  // 降りる（機体に背を向けて乗るときと同じ道を戻り、振り返って待機の視点へ）
	};

	// タイトルへ戻る（白フェード）
	void ChangeToTitle();

	void LoadTuning();
	void SaveTuning() const;

	void UpdateWaiting();
	void UpdateCockpit(float dt);
	void UpdateDisembarking(float dt);

	// 今の見え方をカメラ・ハッチ・光・コクピットへ反映する
	void ApplyPose();
	void EnterCockpit();
	// ⑦：閉じたコクピットの真っ暗から灯りを点け、終わったら降りる
	void EnterArrive();
	Vector2 GetScreenSize() const;
	// 発光（コクピットの部位・表示板・UI の文字）を出してよいか（トランジションの上に光が乗らないよう、遷移中は出さない）
	bool IsGlowAllowed() const;

	Entry entry_ = Entry::Temple;
	State state_ = State::Waiting;

	std::unique_ptr<Camera> camera_;
	std::unique_ptr<Skybox> skybox_;
	std::unique_ptr<HubMechModel> mech_;
	HubBoardingSequence boarding_;
	HubDisembarkSequence disembark_;
	CockpitReveal reveal_;
	std::unique_ptr<HatchSign> hatchSign_;
	std::unique_ptr<CockpitLighting> cockpitLighting_;
	std::unique_ptr<CockpitConsole> console_;
	// 仮の床（地上）・回廊・足場。神殿のモデルができたら差し替える
	std::unique_ptr<Object3DInstance> groundFloor_;
	std::unique_ptr<Object3DInstance> galleryFloor_;
	std::unique_ptr<Object3DInstance> bridgeFloor_;   // 回廊から手のひらの手前へ伸ばす足場（待機の視点はこの上）
	VerticalMenu menu_;
	// デモモード（AttractMode）：待機の「搭乗」・ステージ選択の項目を自動で選ぶ
	AttractMenuTimer attractMenu_;
	// 乗り込んでコクピットに入った（デモモードはステージ1へ）。STAGESELECT の入口（ポーズから）なら false（HUB に戻る）
	bool cockpitFromBoarding_ = false;
	// コクピットの計器が回る経過秒
	float cockpitTime_ = 0.0f;
	// ステージへ向かう（ChangeScene を呼んだ）。以後は入力を読まない
	bool leaving_ = false;

	// 調整値（Hub.json）
	float whiteFadeSeconds_ = 0.8f;   // タイトルへ戻る白フェードの、白くなるまで
	float whiteHoldSeconds_ = 0.3f;   // 同・白のまま待つ秒数
	float fovYDegrees_ = 60.0f;       // 一人称の縦の画角
	float sunIntensity_ = 1.0f;       // 外の光（乗り込みでハッチが閉じるにつれて弱め、外が透けると戻す）
};
