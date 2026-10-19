#pragma once
#include "GameScene.h"
#include "VerticalMenu.h"
#include "Hub/HubBoardingSequence.h"
#include "Hub/HubDisembarkSequence.h"
#include "Hub/HubEntrySequence.h"
#include "Hub/CockpitReveal.h"
#include "StageStart/StageStartGeometry.h"
#include "AttractMode.h"
#include "Vector2.h"
#include <memory>

class Camera;
class Skybox;
class Object3DInstance;
class HubMechModel;
class HubTemple;
class HatchSign;
class CockpitLighting;
class CockpitConsole;
class HubLiftDescent;

/// <summary>
/// ハブシーン（神殿。17_HubScene.md）。**ステージ選択（コクピット）もこのシーンの中**（2026-10-13 統合。18 番 ①「C で決まったこと」）。
/// 状態のまとめ役で、中身は Hub/ の部品がする：
///   入場（タイトルから。HubEntrySequence：白フェードが晴れる間に時計の門の扉の前から待機の視点まで歩く。18 番 ⑧）
///   → 待機（足場から機体を見る。メニューは「搭乗」だけ）
///   → 乗り込み（HubBoardingSequence。18 番 ①の OUT）
///   → コクピット（CockpitReveal の時間軸で、表示板 HatchSign が LOCKED → 六角形で外が透ける CockpitLighting
///      → ラジアルブラー → 文字のステージ選択 CockpitConsole。①の IN）
///   → 「HUB に戻る」でシャットダウン（②の OUT）→ 降りる（HubDisembarkSequence。②の IN）→ 待機
///   → 「ステージ1」で機体ごと八角形のリフトで縦穴を降りる（HubLiftDescent。③の OUT、16_StageStart.md F2）→ StagePlay へ差し替え
/// 入口：TITLE から＝待機／シーン名 STAGESELECT（ポーズの「ステージセレクトに戻る」⑩）＝コクピットの起動から／
///   別のシーンから HUB へ（⑦。今はステージからデバッグの F3）＝真っ暗なコクピットに灯りが点いて（CockpitReveal の帰着）から降りる。
/// キャンセルで白フェードしてタイトルへ（18 番 ⑧）。ゲーム終了はタイトルの責務に一本化している。
/// 神殿（17 番 H1・H2）は HubTemple（天球儀の聖堂・鏡の大理石の床）。丸窓から見える空はタイトルの空。
/// 調整値は Resources/Json/Tuning/Hub.json（ImGui の「Hub Tuning」で保存 / 読み直し）。
/// </summary>
class StageStartSkip;

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
		Entering,    // タイトルから入って、時計の門の扉の前から待機の視点まで歩く
		Waiting,     // 足場で待機
		Boarding,    // 乗り込み
		Cockpit,     // コクピット（起動・ステージ選択・シャットダウン）
		Disembarking,  // 降りる（機体に背を向けて乗るときと同じ道を戻り、振り返って待機の視点へ）
		Descending,  // ③ ステージを決めて、機体ごとリフトで縦穴を降りる（コクピットの中の視点のまま）
	};

	// タイトルへ戻る（白フェード）
	void ChangeToTitle();

	void LoadTuning();
	void SaveTuning() const;

	void UpdateWaiting();
	void UpdateCockpit(float dt);
	void UpdateDisembarking(float dt);
	void UpdateEntering(float dt);
	void UpdateDescending(float dt);
	// ③：ステージを決めてリフトの降下を始める
	void StartDescent();

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
	HubEntrySequence entrySequence_;
	CockpitReveal reveal_;
	std::unique_ptr<HatchSign> hatchSign_;
	std::unique_ptr<CockpitLighting> cockpitLighting_;
	std::unique_ptr<CockpitConsole> console_;
	// 神殿（回廊と、手のひらの手前へ伸ばした足場もこの中。待機の視点は足場の上）
	std::unique_ptr<HubTemple> temple_;
	// ③ のリフト・固定台・縦穴（寸法は StageStartGeometry.json）
	StageStartGeometry stageGeometry_;
	std::unique_ptr<HubLiftDescent> liftDescent_;
	// デバッグ：降下の最後に StagePlay へ差し替えるか（ImGui で切ると降り続けて調整できる）
	bool descentChangesScene_ = true;
	VerticalMenu menu_;
	// デモモード（AttractMode）：待機の「搭乗」・ステージ選択の項目を自動で選ぶ
	AttractMenuTimer attractMenu_;
	// 乗り込んでコクピットに入った（デモモードはステージ1へ）。STAGESELECT の入口（ポーズから）なら false（HUB に戻る）
	bool cockpitFromBoarding_ = false;
	// コクピットの計器が回る経過秒
	float cockpitTime_ = 0.0f;
	// ステージへ向かう（ChangeScene を呼んだ）。以後は入力を読まない
	bool leaving_ = false;
	// リフトの降下中の LSHIFT 長押しで雲の上へスキップ（16 番 F5）
	std::unique_ptr<StageStartSkip> skip_;

	// 調整値（Hub.json）
	float whiteFadeSeconds_ = 0.8f;   // タイトルへ戻る白フェードの、白くなるまで
	float whiteHoldSeconds_ = 0.3f;   // 同・白のまま待つ秒数
	float fovYDegrees_ = 60.0f;       // 一人称の縦の画角
	float sunIntensity_ = 1.0f;       // 外の光（乗り込みでハッチが閉じるにつれて弱め、外が透けると戻す）
};
