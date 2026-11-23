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

	/// <summary>中心と半径の球（XY・XZ・YZ の 3 面のリングで描画）</summary>
	/// <param name="center">中心のワールド座標</param>
	/// <param name="radius">半径 [m]</param>
	/// <param name="color">線の色</param>
	/// <param name="segments">1 リングの分割数（最低 4）</param>
	void Sphere(const Vector3& center, float radius, const Vector4& color, int segments = 16);

	/// <summary>軸並行バウンディングボックス（12 本）</summary>
	/// <param name="min">最小点</param>
	/// <param name="max">最大点</param>
	/// <param name="color">線の色</param>
	void AABB(const Vector3& min, const Vector3& max, const Vector4& color);

	/// <summary>中心 + 3軸 + 半サイズ の有向バウンディングボックス（12 本）</summary>
	/// <param name="center">中心のワールド座標</param>
	/// <param name="axes">ローカル軸 3 本（正規化済み）</param>
	/// <param name="halfSize">各軸方向の半分の大きさ</param>
	/// <param name="color">線の色</param>
	void OBB(const Vector3& center, const Vector3 axes[3], const Vector3& halfSize, const Vector4& color);

	/// <summary>
	/// カプセル（ローカル Y 軸沿い）。両端の半球 + 円柱の母線で描画。
	/// center: 中心、axes: ローカル軸（axes[1] が capsule の長手方向）、
	/// height: 円柱部分の長さ（半球は含まない）、radius: 半径。
	/// </summary>
	/// <param name="center">中心のワールド座標</param>
	/// <param name="axes">ローカル軸 3 本（axes[1] が長手方向）</param>
	/// <param name="height">円柱部分の長さ [m]（半球は含まない）</param>
	/// <param name="radius">半径 [m]</param>
	/// <param name="color">線の色</param>
	/// <param name="segments">円の分割数（最低 4）</param>
	void Capsule(const Vector3& center, const Vector3 axes[3],
		float height, float radius, const Vector4& color, int segments = 16);

	/// <summary>3軸方向の小さな十字マーカー</summary>
	/// <param name="pos">中心のワールド座標</param>
	/// <param name="size">中心から各端までの長さ [m]</param>
	/// <param name="color">線の色</param>
	void Cross(const Vector3& pos, float size, const Vector4& color);

	/// <summary>始点から方向 dir に向かって length 分の線</summary>
	/// <param name="origin">始点</param>
	/// <param name="dir">方向（正規化済みなら length がそのまま長さ）</param>
	/// <param name="length">長さ</param>
	/// <param name="color">線の色</param>
	void Ray(const Vector3& origin, const Vector3& dir, float length, const Vector4& color);

	/// <summary>始点～終点の線</summary>
	/// <param name="start">始点</param>
	/// <param name="end">終点</param>
	/// <param name="color">線の色</param>
	void Line(const Vector3& start, const Vector3& end, const Vector4& color);

	/// <summary>中心の高さの XZ 平面の格子</summary>
	/// <param name="center">中心のワールド座標</param>
	/// <param name="size">一辺の長さ [m]</param>
	/// <param name="step">線の間隔 [m]（0 以下なら何もしない）</param>
	/// <param name="color">線の色</param>
	void Grid(const Vector3& center, float size, float step, const Vector4& color);

	/// <summary>Catmull-Rom スプライン（制御点をすべて通る。端の点は重複させる）</summary>
	/// <param name="controlPoints">制御点（2点未満なら何もしない）</param>
	/// <param name="color">線の色</param>
	/// <param name="samplesPerSegment">区間ごとの分割数（最低 1）</param>
	void CatmullRomSpline(const std::vector<Vector3>& controlPoints,
		const Vector4& color, int samplesPerSegment = 16);

} // namespace DebugDraw
