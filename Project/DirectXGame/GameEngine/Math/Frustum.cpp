#include "Frustum.h"

#include <cmath>

namespace {
	// 平面の法線の長さがこれ以下なら正規化しない（0 で割らない）
	constexpr float kMinPlaneNormalLength = 1e-8f;

	/// <summary>
	/// 行列の 1 列（a,b,c,d）。行ベクトル規約 v*M では clip.x = v・(M の第0列) … となるので、平面は「列」から作る
	/// </summary>
	struct Col { float a, b, c, d; };

	/// <summary>行列の i 列目を取り出す</summary>
	/// <param name="m">行列</param>
	/// <param name="i">列の番号（0〜3）</param>
	/// <returns>列</returns>
	inline Col GetColumn(const Matrix4x4& m, int i) {
		return { m.m[0][i], m.m[1][i], m.m[2][i], m.m[3][i] };
	}
	/// <summary>列どうしの成分ごとの和</summary>
	/// <param name="x">左辺</param>
	/// <param name="y">右辺</param>
	/// <returns>x + y</returns>
	inline Col Add(const Col& x, const Col& y) { return { x.a + y.a, x.b + y.b, x.c + y.c, x.d + y.d }; }
	/// <summary>列どうしの成分ごとの差</summary>
	/// <param name="x">左辺</param>
	/// <param name="y">右辺</param>
	/// <returns>x - y</returns>
	inline Col Sub(const Col& x, const Col& y) { return { x.a - y.a, x.b - y.b, x.c - y.c, x.d - y.d }; }
}

Frustum Frustum::FromViewProjection(const Matrix4x4& viewProjection) {
	// clip.x = v・col0, clip.y = v・col1, clip.z = v・col2, clip.w = v・col3
	const Col col0 = GetColumn(viewProjection, 0);
	const Col col1 = GetColumn(viewProjection, 1);
	const Col col2 = GetColumn(viewProjection, 2);
	const Col col3 = GetColumn(viewProjection, 3);

	Col raw[PlaneCount];
	raw[Left]   = Add(col0, col3);   // clip.x >= -clip.w
	raw[Right]  = Sub(col3, col0);   // clip.x <=  clip.w
	raw[Bottom] = Add(col1, col3);   // clip.y >= -clip.w
	raw[Top]    = Sub(col3, col1);   // clip.y <=  clip.w
	raw[Near]   = col2;              // clip.z >= 0      （深度[0,1]規約）
	raw[Far]    = Sub(col3, col2);   // clip.z <= clip.w

	Frustum f;
	for (int i = 0; i < PlaneCount; ++i) {
		const float len = std::sqrt(raw[i].a * raw[i].a + raw[i].b * raw[i].b + raw[i].c * raw[i].c);
		// 正規化して初めて「符号付き距離」になる＝球half径との比較が成立する
		const float inv = (len > kMinPlaneNormalLength) ? (1.0f / len) : 0.0f;
		f.planes_[i].normal = { raw[i].a * inv, raw[i].b * inv, raw[i].c * inv };
		f.planes_[i].d      = raw[i].d * inv;
	}
	return f;
}

bool Frustum::IntersectsSphere(const Vector3& center, float radius) const {
	for (int i = 0; i < PlaneCount; ++i) {
		const Plane& p = planes_[i];
		const float dist = p.normal.x * center.x + p.normal.y * center.y + p.normal.z * center.z + p.d;
		// どれか1枚でも外側に半径以上離れていれば交差しない
		if (dist < -radius) return false;
	}
	return true;
}

float Frustum::SignedDistance(PlaneIndex index, const Vector3& point) const {
	const Plane& p = planes_[index];
	return p.normal.x * point.x + p.normal.y * point.y + p.normal.z * point.z + p.d;
}
