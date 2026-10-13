#pragma once
#include "GameScene.h"
#include "VerticalMenu.h"
#include <memory>

class Camera;
class SpriteInstance;

/// <summary>
/// ハブシーン（神殿。17_HubScene.md）。
/// メニューは今は「出撃」だけで、決定でステージ選択（STAGESELECT）へ。キャンセルで白フェードしてタイトルへ（18 番 ⑧）。
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

	std::unique_ptr<Camera> camera_;
	std::unique_ptr<SpriteInstance> background_;
	VerticalMenu menu_;
	// デモモード（AttractMode）の経過秒
	float attractTimer_ = 0.0f;

	// 調整値（Hub.json）
	float whiteFadeSeconds_ = 0.8f;   // タイトルへ戻る白フェードの、白くなるまで
	float whiteHoldSeconds_ = 0.3f;   // 同・白のまま待つ秒数
};
