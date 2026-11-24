#include "VoronoiFracture.h"
#include "MathUtility.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <unordered_map>

namespace {
	// 平面の上とみなす距離（メッシュの大きさに対する比）
	constexpr float kPlaneEpsilonRatio = 2e-6f;
	// 切り口の点を同じ点とみなす距離（メッシュの大きさに対する比）
	constexpr float kWeldDistanceRatio = 5e-5f;
	// 途切れた切り口の鎖どうしをつなぎ直す距離（メッシュの大きさに対する比）
	constexpr float kJoinDistanceRatio = 2e-3f;
	// 種点を探す試行回数（種点 1 個あたり）
	constexpr uint32_t kSeedTriesPerSeed = 300;
	// これだけ続けて置けなければ、種点の最小間隔を縮める
	constexpr uint32_t kSeedRelaxInterval = 50;
	// 種点の最小間隔を縮める率
	constexpr float kSeedRelaxRate = 0.85f;
	// 岩の体積を包む箱の体積のこの割合と見積もる（種点の平均の間隔を出すため）
	constexpr float kApproxFillRatio = 0.5f;
	// 中か外かを調べるレイの向き（辺や頂点にちょうど当たりにくい半端な向き）
	const Vector3 kInsideRayDir = { 0.99987f, 0.01370f, 0.00710f };

	inline Vector3 Add(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline Vector3 Sub(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline Vector3 Mul(const Vector3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	inline Vector3 Cross(const Vector3& a, const Vector3& b) {
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
	inline float LengthSq(const Vector3& a) { return Dot(a, a); }

	struct ClipVertex {
		Vector3 p;
		Vector3 n;
		float interior;
	};
	struct Triangle {
		ClipVertex v[3];
	};
	// 切り口の線分（三角形の巡る向きで exit → entry）
	struct Segment {
		Vector3 a;
		Vector3 b;
	};
	struct Point2 {
		float x;
		float y;
	};

	bool LessPosition(const Vector3& a, const Vector3& b) {
		if (a.x != b.x) return a.x < b.x;
		if (a.y != b.y) return a.y < b.y;
		return a.z < b.z;
	}

	/// <summary>辺と平面の交点。辺の向きによらず同じビットの点になるよう、端点を決まった順にしてから補間する</summary>
	ClipVertex Intersect(const ClipVertex& v0, float s0, const ClipVertex& v1, float s1) {
		const ClipVertex* a = &v0;
		const ClipVertex* b = &v1;
		float sa = s0;
		float sb = s1;
		if (LessPosition(v1.p, v0.p)) {
			std::swap(a, b);
			std::swap(sa, sb);
		}
		float t = sa / (sa - sb);
		t = std::clamp(t, 0.0f, 1.0f);
		ClipVertex r;
		r.p = Add(a->p, Mul(Sub(b->p, a->p), t));
		const Vector3 n = Add(Mul(a->n, 1.0f - t), Mul(b->n, t));
		const float len = Length(n);
		r.n = len > 1e-8f ? Mul(n, 1.0f / len) : a->n;
		r.interior = a->interior + (b->interior - a->interior) * t;
		return r;
	}

	bool IsDegenerate(const Vector3& a, const Vector3& b, const Vector3& c, float areaEpsSq) {
		return LengthSq(Cross(Sub(b, a), Sub(c, a))) <= areaEpsSq;
	}

	/// <summary>三角形の集まりを平面で切り、内側（dot(n,p) - d <= eps）だけ残す。切り口の線分も集める</summary>
	void ClipPiece(const std::vector<Triangle>& in, const Vector3& n, float d, float eps, float areaEpsSq,
		std::vector<Triangle>& out, std::vector<Segment>& segments) {
		out.clear();
		segments.clear();
		for (const Triangle& tri : in) {
			float s[3];
			bool outside[3];
			int outsideCount = 0;
			for (int k = 0; k < 3; ++k) {
				s[k] = Dot(n, tri.v[k].p) - d;
				outside[k] = s[k] > eps;
				outsideCount += outside[k] ? 1 : 0;
			}
			if (outsideCount == 0) {
				out.push_back(tri);
				continue;
			}
			if (outsideCount == 3) continue;

			// 内側の頂点と交点を巡る順に並べる（最大 4 角形）
			ClipVertex poly[4];
			int count = 0;
			ClipVertex exitPoint{};
			ClipVertex entryPoint{};
			for (int k = 0; k < 3; ++k) {
				const int k1 = (k + 1) % 3;
				if (!outside[k]) poly[count++] = tri.v[k];
				if (outside[k] != outside[k1]) {
					const ClipVertex x = Intersect(tri.v[k], s[k], tri.v[k1], s[k1]);
					poly[count++] = x;
					if (!outside[k]) {
						exitPoint = x;
					} else {
						entryPoint = x;
					}
				}
			}
			for (int k = 1; k + 1 < count; ++k) {
				if (IsDegenerate(poly[0].p, poly[k].p, poly[k + 1].p, areaEpsSq)) continue;
				out.push_back({ { poly[0], poly[k], poly[k + 1] } });
			}
			segments.push_back({ exitPoint.p, entryPoint.p });
		}
	}

	/// <summary>近い点を探すための格子</summary>
	class WeldGrid {
	public:
		explicit WeldGrid(float cell) : inv_(1.0f / cell) {}
		void Insert(const Vector3& p, uint32_t id) { map_.emplace(Key(Cell(p.x), Cell(p.y), Cell(p.z)), id); }
		template <class F>
		void ForNear(const Vector3& p, F&& f) const {
			const int cx = Cell(p.x);
			const int cy = Cell(p.y);
			const int cz = Cell(p.z);
			for (int dz = -1; dz <= 1; ++dz) {
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						auto range = map_.equal_range(Key(cx + dx, cy + dy, cz + dz));
						for (auto it = range.first; it != range.second; ++it) f(it->second);
					}
				}
			}
		}

	private:
		int Cell(float v) const { return static_cast<int>(std::floor(v * inv_)); }
		static uint64_t Key(int x, int y, int z) {
			constexpr uint64_t kMask = 0x1FFFFF;
			return ((static_cast<uint64_t>(x) & kMask) << 42) | ((static_cast<uint64_t>(y) & kMask) << 21) |
				(static_cast<uint64_t>(z) & kMask);
		}
		float inv_;
		std::unordered_multimap<uint64_t, uint32_t> map_;
	};

	/// <summary>
	/// 切り口の線分をつないで輪にする。閉じなかった輪の数を返す。
	/// 短い線分も捨てずにつなぐ（捨てると短い線分が続く所で隙間が溶接距離を超えて輪が切れる）。
	/// それでも途切れた鎖は、少し広い距離で鎖どうしをつなぎ直し、最後まで閉じない鎖も 3 点以上あれば閉じて塞ぐ
	/// </summary>
	uint32_t BuildLoops(const std::vector<Segment>& segments, float weld, float joinDistance,
		std::vector<std::vector<Vector3>>& loops) {
		loops.clear();
		const float weldSq = weld * weld;
		const float joinSq = joinDistance * joinDistance;
		std::vector<char> used(segments.size(), 0);
		WeldGrid grid(weld * 2.0f);
		for (uint32_t i = 0; i < segments.size(); ++i) grid.Insert(segments[i].a, i);

		auto findNext = [&](const Vector3& p) {
			int best = -1;
			float bestSq = weldSq;
			grid.ForNear(p, [&](uint32_t id) {
				if (used[id]) return;
				const float dSq = LengthSq(Sub(segments[id].a, p));
				if (dSq <= bestSq) {
					bestSq = dSq;
					best = static_cast<int>(id);
				}
			});
			return best;
		};

		// 閉じなかった鎖（点の列。最後の点が鎖の終わり）
		std::vector<std::vector<Vector3>> open;
		for (uint32_t i = 0; i < segments.size(); ++i) {
			if (used[i]) continue;
			used[i] = 1;
			std::vector<Vector3> loop{ segments[i].a };
			Vector3 cur = segments[i].b;
			bool closed = false;
			for (size_t guard = 0; guard <= segments.size(); ++guard) {
				if (loop.size() >= 3 && LengthSq(Sub(cur, loop.front())) <= weldSq) {
					closed = true;
					break;
				}
				const int next = findNext(cur);
				if (next < 0) break;
				used[next] = 1;
				loop.push_back(segments[next].a);
				cur = segments[next].b;
			}
			if (closed) {
				loops.push_back(std::move(loop));
			} else {
				loop.push_back(cur);
				open.push_back(std::move(loop));
			}
		}

		// 途切れた鎖を、終わりに最も近い別の鎖の始まりへつなぐ。自分の始まりへ戻れば閉じる
		uint32_t failures = 0;
		while (!open.empty()) {
			std::vector<Vector3> chain = std::move(open.back());
			open.pop_back();
			bool closed = false;
			for (size_t guard = 0; guard <= open.size() + 1; ++guard) {
				const Vector3& tail = chain.back();
				const float selfSq = LengthSq(Sub(tail, chain.front()));
				int best = -1;
				float bestSq = joinSq;
				for (size_t k = 0; k < open.size(); ++k) {
					const float dSq = LengthSq(Sub(open[k].front(), tail));
					if (dSq < bestSq) {
						bestSq = dSq;
						best = static_cast<int>(k);
					}
				}
				if (chain.size() >= 3 && selfSq <= joinSq && (best < 0 || selfSq <= bestSq)) {
					closed = true;
					break;
				}
				if (best < 0) break;
				chain.insert(chain.end(), open[best].begin(), open[best].end());
				open.erase(open.begin() + best);
			}
			if (!closed) ++failures;
			// 閉じなくても 3 点以上あれば塞ぐ（穴が開いたままより目立たない）
			if (chain.size() >= 3) loops.push_back(std::move(chain));
		}
		return failures;
	}

	float Cross2(const Point2& o, const Point2& a, const Point2& b) {
		return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
	}

	bool StrictlyInside(const Point2& p, const Point2& a, const Point2& b, const Point2& c) {
		return Cross2(a, b, p) > 0.0f && Cross2(b, c, p) > 0.0f && Cross2(c, a, p) > 0.0f;
	}

	/// <summary>切り口の輪を耳刈り法で三角形にして out へ足す（法線は外向き＝+n、断面の印付き）</summary>
	void TriangulateCap(const std::vector<Vector3>& loop, const Vector3& n, float windingSign, float weld,
		std::vector<Triangle>& out) {
		const Vector3 helper = std::fabs(n.x) < 0.9f ? Vector3{ 1.0f, 0.0f, 0.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
		const Vector3 u = Normalize(Cross(helper, n));
		const Vector3 w = Cross(n, u);

		// 重なった点と一直線上の点を除く
		std::vector<Vector3> pts;
		pts.reserve(loop.size());
		for (const Vector3& p : loop) {
			if (!pts.empty() && LengthSq(Sub(p, pts.back())) <= weld * weld) continue;
			pts.push_back(p);
		}
		while (pts.size() >= 2 && LengthSq(Sub(pts.front(), pts.back())) <= weld * weld) pts.pop_back();
		if (pts.size() < 3) return;

		std::vector<Point2> p2(pts.size());
		for (size_t i = 0; i < pts.size(); ++i) p2[i] = { Dot(pts[i], u), Dot(pts[i], w) };

		const float areaEps = weld * weld;
		std::vector<int> idx(pts.size());
		std::iota(idx.begin(), idx.end(), 0);
		for (size_t i = 0; i < idx.size() && idx.size() > 3;) {
			const size_t m = idx.size();
			const Point2& a = p2[idx[(i + m - 1) % m]];
			const Point2& b = p2[idx[i]];
			const Point2& c = p2[idx[(i + 1) % m]];
			if (std::fabs(Cross2(a, b, c)) <= areaEps) {
				idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
				i = 0;
				continue;
			}
			++i;
		}
		if (idx.size() < 3) return;

		// 反時計回り（u, w の平面で）へそろえる
		float area = 0.0f;
		for (size_t i = 0; i < idx.size(); ++i) {
			const Point2& a = p2[idx[i]];
			const Point2& b = p2[idx[(i + 1) % idx.size()]];
			area += a.x * b.y - b.x * a.y;
		}
		if (area < 0.0f) std::reverse(idx.begin(), idx.end());

		auto emit = [&](int ia, int ib, int ic) {
			ClipVertex a{ pts[ia], n, 1.0f };
			ClipVertex b{ pts[ib], n, 1.0f };
			ClipVertex c{ pts[ic], n, 1.0f };
			// 元のメッシュと同じ巻き方（表の向き）にそろえる
			if (Dot(Cross(Sub(b.p, a.p), Sub(c.p, a.p)), n) * windingSign < 0.0f) std::swap(b, c);
			out.push_back({ { a, b, c } });
		};

		size_t guard = idx.size() * idx.size() + 8;
		while (idx.size() > 3 && guard-- > 0) {
			const size_t m = idx.size();
			bool clipped = false;
			for (size_t i = 0; i < m; ++i) {
				const int ip = idx[(i + m - 1) % m];
				const int ic = idx[i];
				const int in = idx[(i + 1) % m];
				if (Cross2(p2[ip], p2[ic], p2[in]) <= areaEps) continue;  // 凹んだ角
				bool containsOther = false;
				for (size_t k = 0; k < m; ++k) {
					const int ik = idx[k];
					if (ik == ip || ik == ic || ik == in) continue;
					if (StrictlyInside(p2[ik], p2[ip], p2[ic], p2[in])) {
						containsOther = true;
						break;
					}
				}
				if (containsOther) continue;
				emit(ip, ic, in);
				idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
				clipped = true;
				break;
			}
			if (!clipped) {
				// 自己交差などで耳が見つからない：残りを扇で塞ぐ
				for (size_t k = 1; k + 1 < idx.size(); ++k) emit(idx[0], idx[k], idx[k + 1]);
				idx.clear();
			}
		}
		if (idx.size() == 3) emit(idx[0], idx[1], idx[2]);
	}

	/// <summary>レイを飛ばして交わった回数の偶奇で、点が閉じたメッシュの中にあるかを調べる</summary>
	bool IsInside(const std::vector<Triangle>& tris, const Vector3& origin) {
		const Vector3 dir = Normalize(kInsideRayDir);
		int hits = 0;
		for (const Triangle& t : tris) {
			const Vector3 e1 = Sub(t.v[1].p, t.v[0].p);
			const Vector3 e2 = Sub(t.v[2].p, t.v[0].p);
			const Vector3 pv = Cross(dir, e2);
			const float det = Dot(e1, pv);
			if (std::fabs(det) < 1e-12f) continue;
			const float invDet = 1.0f / det;
			const Vector3 tv = Sub(origin, t.v[0].p);
			const float bu = Dot(tv, pv) * invDet;
			if (bu < 0.0f || bu > 1.0f) continue;
			const Vector3 qv = Cross(tv, e1);
			const float bv = Dot(dir, qv) * invDet;
			if (bv < 0.0f || bu + bv > 1.0f) continue;
			if (Dot(e2, qv) * invDet > 0.0f) ++hits;
		}
		return (hits & 1) != 0;
	}

	std::vector<Vector3> GenerateSeeds(const std::vector<Triangle>& tris, const Vector3& bmin, const Vector3& bmax,
		const VoronoiFracture::Settings& settings) {
		std::vector<Vector3> seeds;
		const uint32_t count = (std::max)(settings.seedCount, 1u);
		std::mt19937 rng(settings.randomSeed);
		std::uniform_real_distribution<float> rx(bmin.x, bmax.x);
		std::uniform_real_distribution<float> ry(bmin.y, bmax.y);
		std::uniform_real_distribution<float> rz(bmin.z, bmax.z);

		const Vector3 size = Sub(bmax, bmin);
		const float approxVolume = size.x * size.y * size.z * kApproxFillRatio;
		float spacing = settings.minSeedSpacing * std::cbrt(approxVolume / static_cast<float>(count));
		uint32_t failStreak = 0;
		for (uint32_t tries = 0; tries < count * kSeedTriesPerSeed && seeds.size() < count; ++tries) {
			const Vector3 p{ rx(rng), ry(rng), rz(rng) };
			if (!IsInside(tris, p)) continue;
			const float spacingSq = spacing * spacing;
			const bool tooClose = std::any_of(seeds.begin(), seeds.end(),
				[&](const Vector3& s) { return LengthSq(Sub(s, p)) < spacingSq; });
			if (tooClose) {
				if (++failStreak >= kSeedRelaxInterval) {
					spacing *= kSeedRelaxRate;
					failStreak = 0;
				}
				continue;
			}
			failStreak = 0;
			seeds.push_back(p);
		}
		return seeds;
	}

	void AppendTriangles(const std::vector<Triangle>& tris, uint32_t fragment, VoronoiFracture::Result& result) {
		for (const Triangle& t : tris) {
			for (const ClipVertex& v : t.v) {
				result.indices.push_back(static_cast<uint32_t>(result.vertices.size()));
				result.vertices.push_back({ v.p, v.n, v.interior, fragment });
			}
		}
	}
}

bool VoronoiFracture::LoadSourceMesh(const std::string& filePath, SourceMesh& out) {
	out = {};
	Assimp::Importer importer;
	// エンジンの ModelInstance と同じ向きにそろえる（X 反転＋巻き方の反転）
	const aiScene* scene = importer.ReadFile(filePath.c_str(),
		aiProcess_FlipWindingOrder | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
		aiProcess_PreTransformVertices | aiProcess_GenSmoothNormals);
	if (!scene || !scene->HasMeshes()) return false;

	for (uint32_t m = 0; m < scene->mNumMeshes; ++m) {
		const aiMesh* mesh = scene->mMeshes[m];
		const uint32_t base = static_cast<uint32_t>(out.positions.size());
		for (uint32_t i = 0; i < mesh->mNumVertices; ++i) {
			const aiVector3D& p = mesh->mVertices[i];
			out.positions.push_back({ -p.x, p.y, p.z });
			if (mesh->HasNormals()) {
				const aiVector3D& n = mesh->mNormals[i];
				out.normals.push_back({ -n.x, n.y, n.z });
			} else {
				out.normals.push_back({ 0.0f, 1.0f, 0.0f });
			}
		}
		for (uint32_t f = 0; f < mesh->mNumFaces; ++f) {
			const aiFace& face = mesh->mFaces[f];
			if (face.mNumIndices != 3) continue;
			for (uint32_t k = 0; k < 3; ++k) out.indices.push_back(base + face.mIndices[k]);
		}
	}
	return !out.indices.empty();
}

VoronoiFracture::Result VoronoiFracture::Build(const SourceMesh& mesh, const Settings& settings) {
	const auto startTime = std::chrono::steady_clock::now();
	Result result;
	if (mesh.indices.size() < 3 || mesh.positions.empty()) return result;

	const bool hasNormals = mesh.normals.size() == mesh.positions.size();
	std::vector<Triangle> source;
	source.reserve(mesh.indices.size() / 3);
	Vector3 bmin{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
	Vector3 bmax{ -bmin.x, -bmin.y, -bmin.z };
	for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
		Triangle t;
		for (int k = 0; k < 3; ++k) {
			const uint32_t index = mesh.indices[i + k];
			t.v[k].p = mesh.positions[index];
			t.v[k].n = hasNormals ? mesh.normals[index] : Vector3{ 0.0f, 0.0f, 0.0f };
			t.v[k].interior = 0.0f;
			bmin = { (std::min)(bmin.x, t.v[k].p.x), (std::min)(bmin.y, t.v[k].p.y), (std::min)(bmin.z, t.v[k].p.z) };
			bmax = { (std::max)(bmax.x, t.v[k].p.x), (std::max)(bmax.y, t.v[k].p.y), (std::max)(bmax.z, t.v[k].p.z) };
		}
		if (!hasNormals) {
			const Vector3 fn = Normalize(Cross(Sub(t.v[1].p, t.v[0].p), Sub(t.v[2].p, t.v[0].p)));
			for (ClipVertex& v : t.v) v.n = fn;
		}
		source.push_back(t);
	}

	const float extent = (std::max)(Length(Sub(bmax, bmin)), 1e-6f);
	const float planeEps = extent * kPlaneEpsilonRatio;
	const float weld = extent * kWeldDistanceRatio;
	const float joinDistance = extent * kJoinDistanceRatio;
	const float areaEpsSq = (weld * weld) * (weld * weld);

	// 元のメッシュの巻き方（外向きの法線に対して表がどちら回りか）。断面もこれにそろえる
	float windingSum = 0.0f;
	for (const Triangle& t : source) {
		const Vector3 fn = Cross(Sub(t.v[1].p, t.v[0].p), Sub(t.v[2].p, t.v[0].p));
		windingSum += Dot(fn, Add(Add(t.v[0].n, t.v[1].n), t.v[2].n));
	}
	const float windingSign = windingSum >= 0.0f ? 1.0f : -1.0f;

	result.intactFirstIndex = 0;
	AppendTriangles(source, 0, result);
	result.intactIndexCount = static_cast<uint32_t>(result.indices.size());

	result.seeds = GenerateSeeds(source, bmin, bmax, settings);
	const std::vector<Vector3>& seeds = result.seeds;

	std::vector<Triangle> piece;
	std::vector<Triangle> clipped;
	std::vector<Segment> segments;
	std::vector<std::vector<Vector3>> loops;
	std::vector<uint32_t> order;
	for (uint32_t i = 0; i < seeds.size(); ++i) {
		const Vector3& si = seeds[i];
		piece = source;

		order.clear();
		for (uint32_t j = 0; j < seeds.size(); ++j) {
			if (j != i) order.push_back(j);
		}
		std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
			return LengthSq(Sub(seeds[a], si)) < LengthSq(Sub(seeds[b], si));
		});

		auto pieceRadiusSq = [&]() {
			float r = 0.0f;
			for (const Triangle& t : piece) {
				for (const ClipVertex& v : t.v) r = (std::max)(r, LengthSq(Sub(v.p, si)));
			}
			return r;
		};
		float radiusSq = pieceRadiusSq();

		for (uint32_t j : order) {
			const Vector3& sj = seeds[j];
			const Vector3 diff = Sub(sj, si);
			const float len = Length(diff);
			if (len <= planeEps) continue;
			// これより遠い種点の二等分面はもう破片に届かない（近い順に並べてある）
			if (len * len * 0.25f > radiusSq) break;

			// 隣の破片と同じ面（符号だけ逆）になるよう、どちらの種点から見ても同じ計算で平面を作る
			const Vector3 n = Mul(diff, 1.0f / len);
			const float d = Dot(n, Mul(Add(si, sj), 0.5f));

			float maxS = -std::numeric_limits<float>::max();
			for (const Triangle& t : piece) {
				for (const ClipVertex& v : t.v) maxS = (std::max)(maxS, Dot(n, v.p) - d);
			}
			if (maxS <= planeEps) continue;

			ClipPiece(piece, n, d, planeEps, areaEpsSq, clipped, segments);
			if (clipped.empty()) {
				piece.clear();
				break;
			}
			result.capFailures += BuildLoops(segments, weld, joinDistance, loops);
			for (const std::vector<Vector3>& loop : loops) TriangulateCap(loop, n, windingSign, weld, clipped);
			piece.swap(clipped);
			radiusSq = pieceRadiusSq();
		}
		if (piece.empty()) continue;

		// 体積と重心（閉じたメッシュの符号付き四面体の和）
		float sixVolume = 0.0f;
		Vector3 weighted{ 0.0f, 0.0f, 0.0f };
		Vector3 mean{ 0.0f, 0.0f, 0.0f };
		for (const Triangle& t : piece) {
			const float v6 = Dot(t.v[0].p, Cross(t.v[1].p, t.v[2].p)) * windingSign;
			sixVolume += v6;
			weighted = Add(weighted, Mul(Add(Add(t.v[0].p, t.v[1].p), t.v[2].p), v6));
			mean = Add(mean, Add(Add(t.v[0].p, t.v[1].p), t.v[2].p));
		}
		Fragment fragment;
		if (std::fabs(sixVolume) > 1e-12f) {
			fragment.center = Mul(weighted, 1.0f / (4.0f * sixVolume));
			fragment.volume = std::fabs(sixVolume) / 6.0f;
		} else {
			fragment.center = Mul(mean, 1.0f / (3.0f * static_cast<float>(piece.size())));
		}
		for (const Triangle& t : piece) {
			for (const ClipVertex& v : t.v) {
				fragment.radius = (std::max)(fragment.radius, Length(Sub(v.p, fragment.center)));
			}
		}
		fragment.firstIndex = static_cast<uint32_t>(result.indices.size());
		AppendTriangles(piece, static_cast<uint32_t>(result.fragments.size()), result);
		fragment.indexCount = static_cast<uint32_t>(result.indices.size()) - fragment.firstIndex;
		result.fragments.push_back(fragment);
	}

	result.buildMilliseconds = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - startTime).count();
	return result;
}
