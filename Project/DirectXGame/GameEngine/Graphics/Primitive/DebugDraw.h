#pragma once

#include <vector>

#include "Vector3.h"
#include "Vector4.h"

/// <summary>
/// LineRenderer の上に張る薄いラッパー。
/// 形状を毎フレ AddLine に展開して、LineRenderer::Draw() でまとめて描く。
/// 使い方:
///   DebugDraw::Sphere({0,0,0}, 1.0f, {1,1,0,1});
///   DebugDraw::Grid({0,0,0}, 20.0f, 1.0f, {0.3f,0.3f,0.3f,1});
/// LineRenderer の容量は 4096 本。形状ごとの本数の目安：
///   Sphere(seg=16): 48本 / AABB: 12本 / OBB: 12本 / Spline(samples=32): 32本
/// </summary>
namespace DebugDraw {

	// 中心と半径の球（3面のリングで描画）
	void Sphere(const Vector3& center, float radius, const Vector4& color, int segments = 16);

	// 軸並行バウンディングボックス
	void AABB(const Vector3& min, const Vector3& max, const Vector4& color);

	// 中心 + 3軸 + 半サイズ の有向バウンディングボックス
	void OBB(const Vector3& center, const Vector3 axes[3], const Vector3& halfSize, const Vector4& color);

	/// <summary>
	/// カプセル（ローカル Y 軸沿い）。両端の半球 + 円柱の母線で描画。
	/// center: 中心、axes: ローカル軸（axes[1] が capsule の長手方向）、
	/// height: 円柱部分の長さ（半球は含まない）、radius: 半径。
	/// </summary>
	void Capsule(const Vector3& center, const Vector3 axes[3],
		float height, float radius, const Vector4& color, int segments = 16);

	// 3軸方向の小さな十字マーカー
	void Cross(const Vector3& pos, float size, const Vector4& color);

	// 始点から方向 dir に向かって length 分の線
	void Ray(const Vector3& origin, const Vector3& dir, float length, const Vector4& color);

	// 始点～終点
	void Line(const Vector3& start, const Vector3& end, const Vector4& color);

	// Y=0 平面の格子（中心 center、一辺 size、間隔 step）
	void Grid(const Vector3& center, float size, float step, const Vector4& color);

	// Catmull-Rom スプライン（制御点をすべて通る）
	// controlPoints は2点以上必要。
	void CatmullRomSpline(const std::vector<Vector3>& controlPoints,
		const Vector4& color, int samplesPerSegment = 16);

} // namespace DebugDraw
