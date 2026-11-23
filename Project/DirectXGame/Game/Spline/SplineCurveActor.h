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
	/// <summary>すぐ編集できるよう既定の制御点を 3 つ置いて作る</summary>
	SplineCurveActor();
	~SplineCurveActor() override = default;

	//====================
	// IImGuiEditable
	//====================
	/// <summary>表示名を取得する</summary>
	/// <returns>名前</returns>
	std::string GetName() const override { return name_; }
	/// <summary>表示名を設定する</summary>
	/// <param name="n">名前</param>
	void SetName(const std::string& n) override { name_ = n; }
	/// <summary>型名を取得する</summary>
	/// <returns>"Spline"</returns>
	std::string GetTypeName() const override { return "Spline"; }
	/// <summary>制御点の追加・選択・編集・削除を Inspector に出す</summary>
	void OnImGuiInspector() override;

	/// <summary>
	/// ギズモ操作の対象になるよう、選択中の制御点の位置を返す（アクター自身は translate を持たず、各制御点が世界座標）
	/// </summary>
	/// <returns>選択中の制御点の位置。制御点が無い・選択が範囲外なら nullptr</returns>
	Vector3* GetEditableTranslate() override;

	//====================
	// 制御点操作
	//====================
	/// <summary>末尾に制御点を足す</summary>
	/// <param name="p">制御点の位置（ワールド）</param>
	void AddPoint(const Vector3& p) { points_.push_back(p); }
	/// <summary>制御点を消す（範囲外なら何もしない）</summary>
	/// <param name="i">消す制御点の番号</param>
	void RemovePoint(size_t i) { if (i < points_.size()) points_.erase(points_.begin() + i); }
	/// <summary>制御点の数を取得する</summary>
	/// <returns>制御点の数</returns>
	size_t GetPointCount() const { return points_.size(); }
	/// <summary>制御点の列を取得する（読むだけ）</summary>
	/// <returns>制御点の列</returns>
	const std::vector<Vector3>& GetPoints() const { return points_; }
	/// <summary>制御点の列を取得する（書き換えられる）</summary>
	/// <returns>制御点の列</returns>
	std::vector<Vector3>& MutablePoints() { return points_; }

	/// <summary>Inspector で選択中の制御点の番号を取得する</summary>
	/// <returns>番号</returns>
	int GetSelectedIndex() const { return selectedIndex_; }
	/// <summary>Inspector で選択する制御点の番号を設定する</summary>
	/// <param name="i">番号</param>
	void SetSelectedIndex(int i) { selectedIndex_ = i; }

	//====================
	// 曲線評価
	//====================

	/// <summary>
	/// 全体を [0..1] で正規化した位置を返す。Catmull-Rom（制御点を通る）。
	/// 制御点が 0 個なら原点、1 個なら点そのもの、2 個以上で曲線。
	/// </summary>
	/// <param name="t01">全体を 0..1 で正規化した位置</param>
	/// <returns>曲線上の位置（ワールド）</returns>
	Vector3 Sample(float t01) const;

	//====================
	// デバッグ描画
	// シーンの Update から呼ぶか、自前 DrawAll をどこかから呼ぶ運用
	//====================
	/// <summary>曲線と制御点（小さなクロス）を DebugDraw に積む</summary>
	void DrawDebug() const;

private:
	std::string name_ = "Spline";
	std::vector<Vector3> points_;
	int selectedIndex_ = 0;  // Inspector で編集対象の制御点 index
};
