#include "SplineCurveActor.h"

#include "Primitive/DebugDraw.h"
#include "Components/Gameplay.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>

namespace {
	// 制御点インデックスを clamp 風にアクセス（端は重複させる）
	const Vector3& Clamped(const std::vector<Vector3>& pts, int i) {
		if (i < 0) return pts.front();
		if (i >= static_cast<int>(pts.size())) return pts.back();
		return pts[i];
	}
}

SplineCurveActor::SplineCurveActor() {
	// デフォルトで3点だけ用意（ユーザがすぐに編集できるように）
	points_ = {
		{ -3.0f, 0.0f, 0.0f },
		{  0.0f, 1.0f, 3.0f },
		{  3.0f, 0.0f, 0.0f },
	};
}

Vector3* SplineCurveActor::GetEditableTranslate() {
	// Inspector のギズモ操作対象として、現在選択中の制御点を返す
	if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(points_.size())) {
		return nullptr;
	}
	return &points_[static_cast<size_t>(selectedIndex_)];
}

Vector3 SplineCurveActor::Sample(float t01) const {
	const size_t n = points_.size();
	if (n == 0) return { 0, 0, 0 };
	if (n == 1) return points_[0];
	if (n == 2) {
		// 線形補間
		float t = std::clamp(t01, 0.0f, 1.0f);
		return {
			points_[0].x + (points_[1].x - points_[0].x) * t,
			points_[0].y + (points_[1].y - points_[0].y) * t,
			points_[0].z + (points_[1].z - points_[0].z) * t,
		};
	}

	// Catmull-Rom: n-1 セグメント。t01 をどのセグメントに落とすか
	float t = std::clamp(t01, 0.0f, 1.0f);
	float scaled = t * static_cast<float>(n - 1);
	int seg = std::min(static_cast<int>(std::floor(scaled)), static_cast<int>(n) - 2);
	float tLocal = scaled - static_cast<float>(seg);

	const Vector3& p0 = Clamped(points_, seg - 1);
	const Vector3& p1 = Clamped(points_, seg);
	const Vector3& p2 = Clamped(points_, seg + 1);
	const Vector3& p3 = Clamped(points_, seg + 2);

	float t2 = tLocal * tLocal;
	float t3 = t2 * tLocal;

	Vector3 r;
	r.x = 0.5f * ((2.0f * p1.x) +
		(-p0.x + p2.x) * tLocal +
		(2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
		(-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
	r.y = 0.5f * ((2.0f * p1.y) +
		(-p0.y + p2.y) * tLocal +
		(2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
		(-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
	r.z = 0.5f * ((2.0f * p1.z) +
		(-p0.z + p2.z) * tLocal +
		(2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 +
		(-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3);
	return r;
}

void SplineCurveActor::DrawDebug() const {
	if (!IsVisibleInEditor()) return;
	if (points_.empty()) return;

	float r, g, b, a;
	GetTagColor(Gameplay::Of(this).GetTag(), r, g, b, a);
	const Vector4 curveColor{ r, g, b, 1.0f };
	const Vector4 pointColor{ r * 1.2f, g * 1.2f, b * 1.2f, 1.0f };
	const Vector4 selColor{ 1.0f, 1.0f, 0.0f, 1.0f }; // 選択中は黄

	// 曲線本体
	DebugDraw::CatmullRomSpline(points_, curveColor, 24);

	// 各制御点を小さなクロスで可視化
	for (size_t i = 0; i < points_.size(); ++i) {
		const Vector4& c = (static_cast<int>(i) == selectedIndex_) ? selColor : pointColor;
		DebugDraw::Cross(points_[i], 0.3f, c);
	}
}

void SplineCurveActor::OnImGuiInspector() {
#ifdef USE_IMGUI
	ImGui::Text("Points: %d", static_cast<int>(points_.size()));

	if (ImGui::Button("Add Point")) {
		// 末尾の延長線上 or 原点に追加
		Vector3 newP = { 0, 0, 0 };
		if (points_.size() >= 2) {
			const Vector3& a = points_[points_.size() - 2];
			const Vector3& b = points_[points_.size() - 1];
			newP = { b.x + (b.x - a.x), b.y + (b.y - a.y), b.z + (b.z - a.z) };
		} else if (!points_.empty()) {
			newP = { points_.back().x + 1.0f, points_.back().y, points_.back().z };
		}
		points_.push_back(newP);
		selectedIndex_ = static_cast<int>(points_.size()) - 1;
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear All")) {
		points_.clear();
		selectedIndex_ = 0;
	}

	ImGui::Separator();

	// 制御点リスト（選択 + 編集 + 削除）
	for (int i = 0; i < static_cast<int>(points_.size()); ++i) {
		ImGui::PushID(i);

		bool isSel = (i == selectedIndex_);
		char label[32];
		std::snprintf(label, sizeof(label), "P%d##sel", i);
		if (ImGui::Selectable(label, isSel, ImGuiSelectableFlags_None, ImVec2(40, 0))) {
			selectedIndex_ = i;
		}
		ImGui::SameLine();
		ImGui::DragFloat3("##xyz", &points_[i].x, 0.05f);
		ImGui::SameLine();
		if (ImGui::Button("x")) {
			points_.erase(points_.begin() + i);
			if (selectedIndex_ >= static_cast<int>(points_.size())) {
				selectedIndex_ = static_cast<int>(points_.size()) - 1;
			}
			ImGui::PopID();
			break;
		}

		ImGui::PopID();
	}
#endif
}
