#pragma once

#include <string>
#include <vector>

#include "IImGuiEditable.h"
#include "Vector3.h"

/// <summary>
/// シーンに配置可能な「制御点列を内部所有する」スプラインアクター。
/// 各 SplineCurveActor は自分の制御点だけを持つので、別スプラインと混ざることが構造的に起きない。
/// タグ（PlayerRailSpline / EnemyPathSpline / FloatingPathSpline / CameraPathSpline）で
/// 役割を識別する。
/// 曲線は Catmull-Rom（制御点を通る）。
/// 描画はランタイムで自前のメッシュを持たず、毎フレ DebugDraw に積む方式。
/// </summary>
class SplineCurveActor : public IImGuiEditable {
public:
	SplineCurveActor();
	~SplineCurveActor() override = default;

	//====================
	// IImGuiEditable
	//====================
	std::string GetName() const override { return name_; }
	void SetName(const std::string& n) override { name_ = n; }
	std::string GetTypeName() const override { return "Spline"; }
	void OnImGuiInspector() override;

	// アクター自身に「translate」は持たない（各制御点が世界座標）。
	// ただしギズモ操作の対象になるよう、選択中の制御点 index の translate を返す。
	Vector3* GetEditableTranslate() override;

	//====================
	// 制御点操作
	//====================
	void AddPoint(const Vector3& p) { points_.push_back(p); }
	void RemovePoint(size_t i) { if (i < points_.size()) points_.erase(points_.begin() + i); }
	size_t GetPointCount() const { return points_.size(); }
	const std::vector<Vector3>& GetPoints() const { return points_; }
	std::vector<Vector3>& MutablePoints() { return points_; }

	int GetSelectedIndex() const { return selectedIndex_; }
	void SetSelectedIndex(int i) { selectedIndex_ = i; }

	//====================
	// 曲線評価
	//====================

	/// <summary>
	/// 全体を [0..1] で正規化した位置を返す。Catmull-Rom（制御点を通る）。
	/// 制御点が 0 個なら原点、1 個なら点そのもの、2 個以上で曲線。
	/// </summary>
	Vector3 Sample(float t01) const;

	//====================
	// デバッグ描画
	// シーンの Update から呼ぶか、自前 DrawAll をどこかから呼ぶ運用
	//====================
	void DrawDebug() const;

private:
	std::string name_ = "Spline";
	std::vector<Vector3> points_;
	int selectedIndex_ = 0;  // Inspector で編集対象の制御点 index
};
