#pragma once
#include "GameScene.h"
#include "VerticalMenu.h"
#include "Hub/HubBoardingSequence.h"
#include <memory>

class Camera;
class SpriteInstance;
class Skybox;
class Object3DInstance;
class HubMechModel;

/// <summary>
/// ハブシーン（神殿。17_HubScene.md）。
/// 回廊（高さ 10.6m）から HUB 用の機体を見る一人称。メニューは今は「出撃」だけで、
/// 決定で乗り込み（18 番 ①の OUT）を演じ、真っ暗になったらステージ選択（STAGESELECT）へ。
/// キャンセルで白フェードしてタイトルへ（18 番 ⑧）。
/// 神殿のモデル（17 番 H1）ができるまで、床と回廊は石畳の板・背景はタイトルの空で仮に置く。
/// ゲーム終了はタイトルの責務に一本化している。
/// 調整値は Resources/Json/Tuning/Hub.json（ImGui の「Hub Tuning」で保存 / 読み直し）。
/// </summary>
class HubScene : public GameScene {
public:
	HubScene();
	~HubScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override;

	/// <summary>調整値の ImGui（Game.cpp の「Hub Tuning」窓から呼ぶ）</summary>
	void OnImGuiTuning();

private:
	// タイトルへ戻る（白フェード）
	void ChangeToTitle();

	void LoadTuning();
	void SaveTuning() const;

	// 乗り込みの今の見え方をカメラ・ハッチ・光へ反映する（黒い板は Draw で濃さを決める）
	void ApplyBoardingPose();

	std::unique_ptr<Camera> camera_;
	std::unique_ptr<Skybox> skybox_;
	std::unique_ptr<HubMechModel> mech_;
	HubBoardingSequence boarding_;
	// 仮の床（地上）と回廊。神殿のモデルができたら差し替える
	std::unique_ptr<Object3DInstance> groundFloor_;
	std::unique_ptr<Object3DInstance> galleryFloor_;
	// 閉じ切りで真っ暗にする黒い板
	std::unique_ptr<SpriteInstance> blackout_;
	VerticalMenu menu_;
	// デモモード（AttractMode）の経過秒
	float attractTimer_ = 0.0f;

	// 調整値（Hub.json）
	float whiteFadeSeconds_ = 0.8f;   // タイトルへ戻る白フェードの、白くなるまで
	float whiteHoldSeconds_ = 0.3f;   // 同・白のまま待つ秒数
	float fovYDegrees_ = 60.0f;       // 一人称の縦の画角
	float sunIntensity_ = 1.0f;       // 外の光（乗り込みでハッチが閉じるにつれて弱める）
};
