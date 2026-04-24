#include "PrimitiveGenerator.h"
#include <cmath>

namespace PrimitiveGenerator {

    MeshData CreatePlane(float width, float height, uint32_t divisionsX, uint32_t divisionsY, const Vector4& color) {
        MeshData mesh;

        // 0除算防止（最低1分割）
        if (divisionsX < 1) divisionsX = 1;
        if (divisionsY < 1) divisionsY = 1;

        uint32_t vertexCountX = divisionsX + 1;
        uint32_t vertexCountY = divisionsY + 1;

        // 頂点を生成
        for (uint32_t y = 0; y < vertexCountY; ++y) {
            for (uint32_t x = 0; x < vertexCountX; ++x) {
                MeshVertex v{};
                // 位置（中心が原点のXY平面）
                float px = (static_cast<float>(x) / divisionsX - 0.5f) * width;
                float py = (static_cast<float>(y) / divisionsY - 0.5f) * height;
                v.position = { px, py, 0.0f };
                // UV（左下=(0,1)、右上=(1,0)）
                v.texcoord = {
                    static_cast<float>(x) / divisionsX,
                    1.0f - static_cast<float>(y) / divisionsY
                };
                // 法線（Z+方向）
                v.normal = { 0.0f, 0.0f, 1.0f };
                v.color = color;

                mesh.vertices.push_back(v);
            }
        }

        // インデックスを生成
        for (uint32_t y = 0; y < divisionsY; ++y) {
            for (uint32_t x = 0; x < divisionsX; ++x) {
                uint32_t i0 = y * vertexCountX + x;   // 左下
                uint32_t i1 = i0 + 1;                 // 右下
                uint32_t i2 = i0 + vertexCountX;      // 左上
                uint32_t i3 = i2 + 1;                 // 右上

                // 三角形1: 左下 → 左上 → 右下
                mesh.indices.push_back(i0);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i1);
                // 三角形2: 右下 → 左上 → 右上
                mesh.indices.push_back(i1);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i3);
            }
        }

        return mesh;
    }

    MeshData CreateBox(const Vector3& size, const Vector4& color) {
        MeshData mesh;

        float hx = size.x * 0.5f;
        float hy = size.y * 0.5f;
        float hz = size.z * 0.5f;

        // 各面の定義（頂点4つ＋法線）
        struct FaceDef {
            Vector3 p[4];   // 左下, 右下, 左上, 右上
            Vector3 normal;
        };

        FaceDef faces[6] = {
            // +Z面（手前）
            { {{-hx,-hy, hz}, { hx,-hy, hz}, {-hx, hy, hz}, { hx, hy, hz}}, { 0, 0, 1} },
            // -Z面（奥）
            { {{ hx,-hy,-hz}, {-hx,-hy,-hz}, { hx, hy,-hz}, {-hx, hy,-hz}}, { 0, 0,-1} },
            // +X面（右）
            { {{ hx,-hy, hz}, { hx,-hy,-hz}, { hx, hy, hz}, { hx, hy,-hz}}, { 1, 0, 0} },
            // -X面（左）
            { {{-hx,-hy,-hz}, {-hx,-hy, hz}, {-hx, hy,-hz}, {-hx, hy, hz}}, {-1, 0, 0} },
            // +Y面（上）
            { {{-hx, hy, hz}, { hx, hy, hz}, {-hx, hy,-hz}, { hx, hy,-hz}}, { 0, 1, 0} },
            // -Y面（下）
            { {{-hx,-hy,-hz}, { hx,-hy,-hz}, {-hx,-hy, hz}, { hx,-hy, hz}}, { 0,-1, 0} },
        };

        // UV（各面共通。左下、右下、左上、右上）
        Vector2 uvs[4] = {
            {0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f}
        };

        // 6面ぶん頂点・インデックスを追加
        for (int f = 0; f < 6; ++f) {
            uint32_t baseIdx = static_cast<uint32_t>(mesh.vertices.size());

            for (int i = 0; i < 4; ++i) {
                MeshVertex v{};
                v.position = faces[f].p[i];
                v.texcoord = uvs[i];
                v.normal = faces[f].normal;
                v.color = color;
                mesh.vertices.push_back(v);
            }

            // 面の2三角形
            mesh.indices.push_back(baseIdx + 0);
            mesh.indices.push_back(baseIdx + 2);
            mesh.indices.push_back(baseIdx + 1);
            mesh.indices.push_back(baseIdx + 1);
            mesh.indices.push_back(baseIdx + 2);
            mesh.indices.push_back(baseIdx + 3);
        }

        return mesh;
    }

    MeshData CreateRing(float outerRadius, float innerRadius, uint32_t divisions,
        const Vector4& innerColor, const Vector4& outerColor) {
        MeshData mesh;

        // 最小分割数チェック
        if (divisions < 3) divisions = 3;

        const float radianPerDivide = 2.0f * 3.14159265358979323846f / static_cast<float>(divisions);

        // 頂点を生成（分割ごとに外側・内側の頂点ペアを追加）
        for (uint32_t index = 0; index < divisions; ++index) {
            float sin = std::sin(index * radianPerDivide);
            float cos = std::cos(index * radianPerDivide);
            float sinNext = std::sin((index + 1) * radianPerDivide);
            float cosNext = std::cos((index + 1) * radianPerDivide);
            float u = static_cast<float>(index) / static_cast<float>(divisions);
            float uNext = static_cast<float>(index + 1) / static_cast<float>(divisions);

            // 4頂点（外側2 + 内側2）
            MeshVertex v0{}, v1{}, v2{}, v3{};

            // 外側（上端）
            v0.position = { -sin * outerRadius,  cos * outerRadius, 0.0f };
            v0.texcoord = { u, 0.0f };
            v0.normal = { 0.0f, 0.0f, 1.0f };
            v0.color = outerColor;

            v1.position = { -sinNext * outerRadius, cosNext * outerRadius, 0.0f };
            v1.texcoord = { uNext, 0.0f };
            v1.normal = { 0.0f, 0.0f, 1.0f };
            v1.color = outerColor;

            // 内側（下端）
            v2.position = { -sin * innerRadius, cos * innerRadius, 0.0f };
            v2.texcoord = { u, 1.0f };
            v2.normal = { 0.0f, 0.0f, 1.0f };
            v2.color = innerColor;

            v3.position = { -sinNext * innerRadius, cosNext * innerRadius, 0.0f };
            v3.texcoord = { uNext, 1.0f };
            v3.normal = { 0.0f, 0.0f, 1.0f };
            v3.color = innerColor;

            uint32_t baseIdx = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(v0);
            mesh.vertices.push_back(v1);
            mesh.vertices.push_back(v2);
            mesh.vertices.push_back(v3);

            // 2つの三角形
            // 三角形1: 外側→外側次→内側
            mesh.indices.push_back(baseIdx + 0);
            mesh.indices.push_back(baseIdx + 1);
            mesh.indices.push_back(baseIdx + 2);
            // 三角形2: 内側→外側次→内側次
            mesh.indices.push_back(baseIdx + 2);
            mesh.indices.push_back(baseIdx + 1);
            mesh.indices.push_back(baseIdx + 3);
        }

        return mesh;
    }

    MeshData CreateRing(const RingParams& params) {
        MeshData mesh;

        uint32_t divisions = params.divisions;
        if (divisions < 3) divisions = 3;

        // 角度の正規化
        float startAngle = params.startAngle;
        float endAngle = params.endAngle;
        if (endAngle < startAngle) {
            std::swap(startAngle, endAngle);
        }

        const float totalAngle = endAngle - startAngle;
        const float radianPerDivide = totalAngle / static_cast<float>(divisions);

        // フェード範囲のクランプ
        float fadeStart = params.fadeStart;
        float fadeEnd = params.fadeEnd;
        if (fadeStart < 0.0f) fadeStart = 0.0f;
        if (fadeEnd > 1.0f) fadeEnd = 1.0f;
        if (fadeEnd < fadeStart) fadeEnd = fadeStart;

        // 半径4点：外径(t=1.0)、fadeEnd(t=fadeEnd)、fadeStart(t=fadeStart)、内径(t=0.0)
        // ここでtは「内径から外径への割合」(0.0=内径、1.0=外径)
        struct RadiusRing {
            float t;         // 0.0〜1.0
            Vector4 color;
        };

        // 補間係数でinner→outerを線形補間する関数
        auto lerpColor = [](const Vector4& a, const Vector4& b, float t) {
            return Vector4{
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t
            };
            };

        // 4つのリング定義
        // t=0: 内径（innerColor）
        // t=fadeStart: innerColorのまま
        // t=fadeEnd: outerColorのまま
        // t=1: 外径（outerColor）
        RadiusRing rings[4];
        rings[0].t = 0.0f;       rings[0].color = params.innerColor;
        rings[1].t = fadeStart;  rings[1].color = params.innerColor;
        rings[2].t = fadeEnd;    rings[2].color = params.outerColor;
        rings[3].t = 1.0f;       rings[3].color = params.outerColor;

        // 分割indexに対応する外径を取得
        auto getOuterRadiusAt = [&](uint32_t idx) -> float {
            if (params.outerRadiusPerDivision.empty()) {
                return params.outerRadius;
            }
            // 要素数が足りなければ繰り返し（リピート）で参照
            return params.outerRadiusPerDivision[idx % params.outerRadiusPerDivision.size()];
            };

        // t値 と 分割indexから実際の半径を計算
        auto radiusFromT = [&](float t, uint32_t idx) {
            float outerR = getOuterRadiusAt(idx);
            return params.innerRadius + (outerR - params.innerRadius) * t;
            };

        // 分割ごとに頂点生成
        for (uint32_t index = 0; index < divisions; ++index) {
            float angle = startAngle + index * radianPerDivide;
            float angleNext = startAngle + (index + 1) * radianPerDivide;

            float sin = std::sin(angle);
            float cos = std::cos(angle);
            float sinNext = std::sin(angleNext);
            float cosNext = std::cos(angleNext);

            float step = static_cast<float>(index) / static_cast<float>(divisions);
            float stepNext = static_cast<float>(index + 1) / static_cast<float>(divisions);

            // 4リング × 2頂点 = 8頂点を追加
            uint32_t baseIdx = static_cast<uint32_t>(mesh.vertices.size());

            for (int r = 0; r < 4; ++r) {
                float radiusCurrent = radiusFromT(rings[r].t, index);
                float radiusNext = radiusFromT(rings[r].t, index + 1);  // 次角度の半径
                float vCoord = 1.0f - rings[r].t;

                // 現在角度の頂点
                MeshVertex va{};
                va.position = { -sin * radiusCurrent, cos * radiusCurrent, 0.0f };
                va.normal = { 0.0f, 0.0f, 1.0f };
                va.color = rings[r].color;
                if (params.uvHorizon) {
                    va.texcoord = { step, vCoord };
                } else {
                    va.texcoord = { 1.0f - vCoord, step };
                }
                mesh.vertices.push_back(va);

                // 次角度の頂点
                MeshVertex vb{};
                vb.position = { -sinNext * radiusNext, cosNext * radiusNext, 0.0f };
                vb.normal = { 0.0f, 0.0f, 1.0f };
                vb.color = rings[r].color;
                if (params.uvHorizon) {
                    vb.texcoord = { stepNext, vCoord };
                } else {
                    vb.texcoord = { 1.0f - vCoord, stepNext };
                }
                mesh.vertices.push_back(vb);
            }

            // 3つの帯（ring0-1、ring1-2、ring2-3）それぞれにtriangleを2つずつ
            for (int r = 0; r < 3; ++r) {
                uint32_t outerPair = baseIdx + r * 2;         // 外側ペア
                uint32_t innerPair = baseIdx + (r + 1) * 2;   // 内側ペア

                // 三角形1: outer_a → outer_b → inner_a
                mesh.indices.push_back(outerPair + 0);
                mesh.indices.push_back(outerPair + 1);
                mesh.indices.push_back(innerPair + 0);
                // 三角形2: inner_a → outer_b → inner_b
                mesh.indices.push_back(innerPair + 0);
                mesh.indices.push_back(outerPair + 1);
                mesh.indices.push_back(innerPair + 1);
            }
        }

        return mesh;
    }

    MeshData CreateCylinder(float topRadius, float bottomRadius, float height,
        uint32_t divisions,
        const Vector4& topColor, const Vector4& bottomColor) {
        MeshData mesh;

        if (divisions < 3) divisions = 3;

        const float radianPerDivide = 2.0f * 3.14159265358979323846f / static_cast<float>(divisions);
        const float halfHeight = height * 0.5f;

        for (uint32_t index = 0; index < divisions; ++index) {
            float sin = std::sin(index * radianPerDivide);
            float cos = std::cos(index * radianPerDivide);
            float sinNext = std::sin((index + 1) * radianPerDivide);
            float cosNext = std::cos((index + 1) * radianPerDivide);
            float u = static_cast<float>(index) / static_cast<float>(divisions);
            float uNext = static_cast<float>(index + 1) / static_cast<float>(divisions);

            // 4頂点（上2 + 下2）
            MeshVertex v0{}, v1{}, v2{}, v3{};

            // 上側（Y = +halfHeight）
            v0.position = { -sin * topRadius, halfHeight, cos * topRadius };
            v0.texcoord = { u, 0.0f };
            v0.normal = { -sin, 0.0f, cos };
            v0.color = topColor;

            v1.position = { -sinNext * topRadius, halfHeight, cosNext * topRadius };
            v1.texcoord = { uNext, 0.0f };
            v1.normal = { -sinNext, 0.0f, cosNext };
            v1.color = topColor;

            // 下側（Y = -halfHeight）
            v2.position = { -sin * bottomRadius, -halfHeight, cos * bottomRadius };
            v2.texcoord = { u, 1.0f };
            v2.normal = { -sin, 0.0f, cos };
            v2.color = bottomColor;

            v3.position = { -sinNext * bottomRadius, -halfHeight, cosNext * bottomRadius };
            v3.texcoord = { uNext, 1.0f };
            v3.normal = { -sinNext, 0.0f, cosNext };
            v3.color = bottomColor;

            uint32_t baseIdx = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(v0);
            mesh.vertices.push_back(v1);
            mesh.vertices.push_back(v2);
            mesh.vertices.push_back(v3);

            // 2つの三角形（側面1枚分）
            // 三角形1: 上 → 上次 → 下
            mesh.indices.push_back(baseIdx + 0);
            mesh.indices.push_back(baseIdx + 1);
            mesh.indices.push_back(baseIdx + 2);
            // 三角形2: 下 → 上次 → 下次
            mesh.indices.push_back(baseIdx + 2);
            mesh.indices.push_back(baseIdx + 1);
            mesh.indices.push_back(baseIdx + 3);
        }

        return mesh;
    }
}