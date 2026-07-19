#pragma once

#include "Matrix4x4.h"
#include "Vector3.h"

/// <summary>
/// 視錐台（6平面）。ビュープロジェクション行列から抽出して当たり判定に使う。
///
/// 用途:
///   - 敵が画面内にいるか（点 or 境界球）の判定
///   - 将来のフラスタムカリング（描画スキップ）
/// どちらも「フラスタムを毎フレーム1回だけ構築 → 多数のオブジェクトを判定」という
/// 使い方になるので、構築（FromViewProjection）と判定（Intersects*）を分けてある。
///
/// 平面は法線を正規化して保持するので、判定結果は**符号付き距離**として意味を持つ。
/// これにより「点＝半径0の球」として同じ式で扱える。
/// </summary>
class Frustum {
public:
	/// <summary>平面。a*x + b*y + c*z + d が符号付き距離（正＝内side）。</summary>
	struct Plane {
		Vector3 normal{ 0.0f, 0.0f, 0.0f };
		float   d = 0.0f;
	};

	enum PlaneIndex { Left, Right, Bottom, Top, Near, Far, PlaneCount };

	/// <summary>
	/// ビュープロジェクション行列から6平面を抽出する。
	/// このプロジェクトの規約（行ベクトル v*M・LH・深度[0,1]）前提。
	/// 規約が変わると静かに壊れるので、変更時は必ず既知の点で検証すること。
	/// </summary>
	static Frustum FromViewProjection(const Matrix4x4& viewProjection);

	/// <summary>境界球が視錐台と交差するか（カリング・画面内判定の本体）。</summary>
	bool IntersectsSphere(const Vector3& center, float radius) const;

	/// <summary>点が視錐台の中か。margin を足すと画面外へ少しはみ出しても内側扱いになる。</summary>
	bool ContainsPoint(const Vector3& point, float margin = 0.0f) const {
		return IntersectsSphere(point, margin);
	}

	/// <summary>指定平面からの符号付き距離（負＝外側）。デバッグ・調整用。</summary>
	float SignedDistance(PlaneIndex index, const Vector3& point) const;

private:
	Plane planes_[PlaneCount]{};
};
