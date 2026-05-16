#include "DebugDraw.h"

#include "LineRenderer.h"

#include <cmath>

namespace {
	constexpr float kPi = 3.14159265358979323846f;

	inline Vector3 Add(const Vector3& a, const Vector3& b) {
		return { a.x + b.x, a.y + b.y, a.z + b.z };
	}
	inline Vector3 Sub(const Vector3& a, const Vector3& b) {
		return { a.x - b.x, a.y - b.y, a.z - b.z };
	}
	inline Vector3 Scale(const Vector3& a, float s) {
		return { a.x * s, a.y * s, a.z * s };
	}
	inline Vector3 Lerp(const Vector3& a, const Vector3& b, float t) {
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
	}
}

namespace DebugDraw {

	void Line(const Vector3& start, const Vector3& end, const Vector4& color) {
		LineRenderer::GetInstance()->AddLine(start, end, color);
	}

	void Sphere(const Vector3& center, float radius, const Vector4& color, int segments) {
		if (segments < 4) segments = 4;
		auto* lr = LineRenderer::GetInstance();
		const float step = 2.0f * kPi / static_cast<float>(segments);

		// XY平面のリング
		for (int i = 0; i < segments; ++i) {
			float a0 = step * i;
			float a1 = step * (i + 1);
			Vector3 p0{ center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius, center.z };
			Vector3 p1{ center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius, center.z };
			lr->AddLine(p0, p1, color);
		}
		// XZ平面のリング
		for (int i = 0; i < segments; ++i) {
			float a0 = step * i;
			float a1 = step * (i + 1);
			Vector3 p0{ center.x + std::cos(a0) * radius, center.y, center.z + std::sin(a0) * radius };
			Vector3 p1{ center.x + std::cos(a1) * radius, center.y, center.z + std::sin(a1) * radius };
			lr->AddLine(p0, p1, color);
		}
		// YZ平面のリング
		for (int i = 0; i < segments; ++i) {
			float a0 = step * i;
			float a1 = step * (i + 1);
			Vector3 p0{ center.x, center.y + std::cos(a0) * radius, center.z + std::sin(a0) * radius };
			Vector3 p1{ center.x, center.y + std::cos(a1) * radius, center.z + std::sin(a1) * radius };
			lr->AddLine(p0, p1, color);
		}
	}

	void AABB(const Vector3& min, const Vector3& max, const Vector4& color) {
		// 8頂点
		Vector3 c[8] = {
			{min.x, min.y, min.z}, {max.x, min.y, min.z},
			{max.x, max.y, min.z}, {min.x, max.y, min.z},
			{min.x, min.y, max.z}, {max.x, min.y, max.z},
			{max.x, max.y, max.z}, {min.x, max.y, max.z},
		};
		auto* lr = LineRenderer::GetInstance();
		// 底面・上面・縦
		const int edges[12][2] = {
			{0,1},{1,2},{2,3},{3,0}, // 底面
			{4,5},{5,6},{6,7},{7,4}, // 上面
			{0,4},{1,5},{2,6},{3,7}, // 縦
		};
		for (auto& e : edges) {
			lr->AddLine(c[e[0]], c[e[1]], color);
		}
	}

	void OBB(const Vector3& center, const Vector3 axes[3], const Vector3& halfSize, const Vector4& color) {
		Vector3 hx = Scale(axes[0], halfSize.x);
		Vector3 hy = Scale(axes[1], halfSize.y);
		Vector3 hz = Scale(axes[2], halfSize.z);

		// 8頂点を構築 (符号の組み合わせ ±hx ±hy ±hz)
		Vector3 c[8];
		for (int i = 0; i < 8; ++i) {
			float sx = (i & 1) ? 1.0f : -1.0f;
			float sy = (i & 2) ? 1.0f : -1.0f;
			float sz = (i & 4) ? 1.0f : -1.0f;
			Vector3 p = center;
			p = Add(p, Scale(hx, sx));
			p = Add(p, Scale(hy, sy));
			p = Add(p, Scale(hz, sz));
			c[i] = p;
		}
		auto* lr = LineRenderer::GetInstance();
		// インデックスは bit パターン: 0=---, 1=+--, 2=-+-, 3=++-, 4=--+, 5=+-+, 6=-++, 7=+++
		const int edges[12][2] = {
			{0,1},{2,3},{4,5},{6,7}, // X方向
			{0,2},{1,3},{4,6},{5,7}, // Y方向
			{0,4},{1,5},{2,6},{3,7}, // Z方向
		};
		for (auto& e : edges) {
			lr->AddLine(c[e[0]], c[e[1]], color);
		}
	}

	void Cross(const Vector3& pos, float size, const Vector4& color) {
		auto* lr = LineRenderer::GetInstance();
		lr->AddLine({ pos.x - size, pos.y, pos.z }, { pos.x + size, pos.y, pos.z }, color);
		lr->AddLine({ pos.x, pos.y - size, pos.z }, { pos.x, pos.y + size, pos.z }, color);
		lr->AddLine({ pos.x, pos.y, pos.z - size }, { pos.x, pos.y, pos.z + size }, color);
	}

	void Ray(const Vector3& origin, const Vector3& dir, float length, const Vector4& color) {
		Vector3 end = Add(origin, Scale(dir, length));
		LineRenderer::GetInstance()->AddLine(origin, end, color);
	}

	void Grid(const Vector3& center, float size, float step, const Vector4& color) {
		if (step <= 0.0f) return;
		auto* lr = LineRenderer::GetInstance();
		const float half = size * 0.5f;
		const int lines = static_cast<int>(size / step) + 1;
		for (int i = 0; i < lines; ++i) {
			float offset = -half + step * static_cast<float>(i);
			// X方向の線
			lr->AddLine(
				{ center.x - half, center.y, center.z + offset },
				{ center.x + half, center.y, center.z + offset },
				color);
			// Z方向の線
			lr->AddLine(
				{ center.x + offset, center.y, center.z - half },
				{ center.x + offset, center.y, center.z + half },
				color);
		}
	}

	void CatmullRomSpline(const std::vector<Vector3>& controlPoints,
		const Vector4& color, int samplesPerSegment) {
		const size_t n = controlPoints.size();
		if (n < 2) return;
		if (samplesPerSegment < 1) samplesPerSegment = 1;

		// セグメントごとに 4 制御点を取り出してサンプリング。端は重複させる。
		auto At = [&](int idx) -> const Vector3& {
			if (idx < 0) return controlPoints[0];
			if (idx >= static_cast<int>(n)) return controlPoints[n - 1];
			return controlPoints[idx];
		};

		auto* lr = LineRenderer::GetInstance();
		for (size_t seg = 0; seg + 1 < n; ++seg) {
			const Vector3& p0 = At(static_cast<int>(seg) - 1);
			const Vector3& p1 = At(static_cast<int>(seg));
			const Vector3& p2 = At(static_cast<int>(seg) + 1);
			const Vector3& p3 = At(static_cast<int>(seg) + 2);

			Vector3 prev = p1;
			for (int i = 1; i <= samplesPerSegment; ++i) {
				float t = static_cast<float>(i) / static_cast<float>(samplesPerSegment);
				float t2 = t * t;
				float t3 = t2 * t;
				// Catmull-Rom (tension = 0.5)
				Vector3 cur;
				cur.x = 0.5f * ((2.0f * p1.x) +
					(-p0.x + p2.x) * t +
					(2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
					(-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
				cur.y = 0.5f * ((2.0f * p1.y) +
					(-p0.y + p2.y) * t +
					(2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
					(-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
				cur.z = 0.5f * ((2.0f * p1.z) +
					(-p0.z + p2.z) * t +
					(2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 +
					(-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3);
				lr->AddLine(prev, cur, color);
				prev = cur;
			}
		}
	}

} // namespace DebugDraw
