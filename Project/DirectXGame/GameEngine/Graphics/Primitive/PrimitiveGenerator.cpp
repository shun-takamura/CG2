#include "PrimitiveGenerator.h"
#include <cmath>
#include <random>
#include <chrono>

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

    MeshData CreateCylinder(const CylinderParams& params) {
        MeshData mesh;

        uint32_t divisions = params.divisions;
        if (divisions < 3) divisions = 3;

        const float halfHeight = params.height * 0.5f;
        const float angleRange = params.endAngle - params.startAngle;
        const float radianPerDivide = angleRange / static_cast<float>(divisions);

        for (uint32_t index = 0; index < divisions; ++index) {
            float angle0 = params.startAngle + index * radianPerDivide;
            float angle1 = params.startAngle + (index + 1) * radianPerDivide;
            float sin = std::sin(angle0);
            float cos = std::cos(angle0);
            float sinNext = std::sin(angle1);
            float cosNext = std::cos(angle1);
            float u = static_cast<float>(index) / static_cast<float>(divisions);
            float uNext = static_cast<float>(index + 1) / static_cast<float>(divisions);

            // 4頂点（上2 + 下2）
            MeshVertex v0{}, v1{}, v2{}, v3{};

            // 上側（Y = +halfHeight）
            v0.position = { -sin * params.topRadius, halfHeight, cos * params.topRadius };
            v0.texcoord = { u, 0.0f };
            v0.normal = { -sin, 0.0f, cos };
            v0.color = params.topColor;

            v1.position = { -sinNext * params.topRadius, halfHeight, cosNext * params.topRadius };
            v1.texcoord = { uNext, 0.0f };
            v1.normal = { -sinNext, 0.0f, cosNext };
            v1.color = params.topColor;

            // 下側（Y = -halfHeight）
            v2.position = { -sin * params.bottomRadius, -halfHeight, cos * params.bottomRadius };
            v2.texcoord = { u, 1.0f };
            v2.normal = { -sin, 0.0f, cos };
            v2.color = params.bottomColor;

            v3.position = { -sinNext * params.bottomRadius, -halfHeight, cosNext * params.bottomRadius };
            v3.texcoord = { uNext, 1.0f };
            v3.normal = { -sinNext, 0.0f, cosNext };
            v3.color = params.bottomColor;

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

    // 既存シグネチャは Params 版にデリゲート
    MeshData CreateCylinder(float topRadius, float bottomRadius, float height,
        uint32_t divisions,
        const Vector4& topColor, const Vector4& bottomColor) {
        CylinderParams params;
        params.topRadius = topRadius;
        params.bottomRadius = bottomRadius;
        params.height = height;
        params.divisions = divisions;
        params.topColor = topColor;
        params.bottomColor = bottomColor;
        return CreateCylinder(params);
    }

    MeshData CreateSphere(float radius, uint32_t divisions, const Vector4& color) {
        MeshData mesh;
        if (divisions < 3) divisions = 3;

        const float kPi = 3.14159265358979323846f;
        const uint32_t latDivisions = divisions;          // 緯度方向（縦）
        const uint32_t lonDivisions = divisions * 2;      // 経度方向（横）

        // 頂点を生成（lat=0が北極、lat=latDivisionsが南極）
        for (uint32_t lat = 0; lat <= latDivisions; ++lat) {
            float theta = kPi * static_cast<float>(lat) / static_cast<float>(latDivisions);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            for (uint32_t lon = 0; lon <= lonDivisions; ++lon) {
                float phi = 2.0f * kPi * static_cast<float>(lon) / static_cast<float>(lonDivisions);
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                MeshVertex v{};
                v.position = {
                    radius * sinTheta * cosPhi,
                    radius * cosTheta,
                    radius * sinTheta * sinPhi
                };
                v.normal = { sinTheta * cosPhi, cosTheta, sinTheta * sinPhi };
                v.texcoord = {
                    static_cast<float>(lon) / lonDivisions,
                    static_cast<float>(lat) / latDivisions
                };
                v.color = color;
                mesh.vertices.push_back(v);
            }
        }

        // インデックスを生成
        for (uint32_t lat = 0; lat < latDivisions; ++lat) {
            for (uint32_t lon = 0; lon < lonDivisions; ++lon) {
                uint32_t i0 = lat * (lonDivisions + 1) + lon;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + (lonDivisions + 1);
                uint32_t i3 = i2 + 1;

                mesh.indices.push_back(i0);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i1);

                mesh.indices.push_back(i1);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i3);
            }
        }
        return mesh;
    }

    MeshData CreateHemisphere(float radius, uint32_t divisions, const Vector4& color) {
        MeshData mesh;
        if (divisions < 3) divisions = 3;

        const float kPi = 3.14159265358979323846f;
        const uint32_t latDivisions = divisions;          // 緯度方向（縦）：頂点[0]→赤道[latDivisions]
        const uint32_t lonDivisions = divisions * 2;      // 経度方向（横）：全周

        // 上半球のみ：theta は 0(頂点 +Y) 〜 π/2(赤道 Y=0)。蓋は張らない＝中空シェル（おわん型）。
        for (uint32_t lat = 0; lat <= latDivisions; ++lat) {
            float theta = (kPi * 0.5f) * static_cast<float>(lat) / static_cast<float>(latDivisions);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            for (uint32_t lon = 0; lon <= lonDivisions; ++lon) {
                float phi = 2.0f * kPi * static_cast<float>(lon) / static_cast<float>(lonDivisions);
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                MeshVertex v{};
                v.position = {
                    radius * sinTheta * cosPhi,
                    radius * cosTheta,
                    radius * sinTheta * sinPhi
                };
                v.normal = { sinTheta * cosPhi, cosTheta, sinTheta * sinPhi };
                v.texcoord = {
                    static_cast<float>(lon) / lonDivisions,
                    static_cast<float>(lat) / latDivisions
                };
                v.color = color;
                mesh.vertices.push_back(v);
            }
        }

        // インデックス（CreateSphere と同じ巻き順＝外向き法線）
        for (uint32_t lat = 0; lat < latDivisions; ++lat) {
            for (uint32_t lon = 0; lon < lonDivisions; ++lon) {
                uint32_t i0 = lat * (lonDivisions + 1) + lon;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + (lonDivisions + 1);
                uint32_t i3 = i2 + 1;

                mesh.indices.push_back(i0);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i1);

                mesh.indices.push_back(i1);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i3);
            }
        }
        return mesh;
    }

    MeshData CreateBeamFromPolyline(const std::vector<Vector3>& polyline, const BeamAppearance& app) {
        MeshData mesh;
        if (polyline.size() < 2) return mesh;

        const float kPi = 3.14159265358979323846f;
        const uint32_t planeCount = app.planeCount < 1 ? 1 : app.planeCount;

        auto length3 = [](const Vector3& v) {
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            };
        auto normalize3 = [&](const Vector3& v) -> Vector3 {
            float len = length3(v);
            if (len < 1e-6f) return { 1.0f, 0.0f, 0.0f };
            return { v.x / len, v.y / len, v.z / len };
            };
        auto cross3 = [](const Vector3& a, const Vector3& b) -> Vector3 {
            return {
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            };
            };
        auto lerpV4 = [](const Vector4& a, const Vector4& b, float t) {
            return Vector4{
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t
            };
            };

        // 各頂点ごとの累積長
        const size_t n = polyline.size();
        std::vector<float> cumLen(n, 0.0f);
        for (size_t i = 1; i < n; ++i) {
            cumLen[i] = cumLen[i - 1] + length3({
                polyline[i].x - polyline[i - 1].x,
                polyline[i].y - polyline[i - 1].y,
                polyline[i].z - polyline[i - 1].z });
        }
        const float totalLen = cumLen.back();
        if (totalLen < 1e-6f) return mesh;

        // 各点での接線とその直交基底（right, up）を事前計算
        std::vector<Vector3> tangents(n), rights(n), ups(n);
        for (size_t i = 0; i < n; ++i) {
            Vector3 t;
            if (i == 0) {
                t = { polyline[1].x - polyline[0].x, polyline[1].y - polyline[0].y, polyline[1].z - polyline[0].z };
            } else if (i == n - 1) {
                t = { polyline[i].x - polyline[i - 1].x, polyline[i].y - polyline[i - 1].y, polyline[i].z - polyline[i - 1].z };
            } else {
                t = { polyline[i + 1].x - polyline[i - 1].x, polyline[i + 1].y - polyline[i - 1].y, polyline[i + 1].z - polyline[i - 1].z };
            }
            tangents[i] = normalize3(t);
            Vector3 up = (std::abs(tangents[i].y) > 0.99f) ? Vector3{ 1.0f, 0.0f, 0.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
            rights[i] = normalize3(cross3(tangents[i], up));
            ups[i] = cross3(rights[i], tangents[i]);
        }

        // Plane を軸まわりに均等配置（pi/planeCount 刻みで180度未満を埋める：両面で360度カバー）
        for (uint32_t p = 0; p < planeCount; ++p) {
            float angleRot = kPi * static_cast<float>(p) / static_cast<float>(planeCount);
            float cosA = std::cos(angleRot);
            float sinA = std::sin(angleRot);

            uint32_t baseIdx = static_cast<uint32_t>(mesh.vertices.size());

            for (size_t i = 0; i < n; ++i) {
                // 軸まわり回転後のオフセット方向
                Vector3 offsetDir = {
                    rights[i].x * cosA + ups[i].x * sinA,
                    rights[i].y * cosA + ups[i].y * sinA,
                    rights[i].z * cosA + ups[i].z * sinA
                };
                // 面法線（接線 × オフセット = 面に垂直）
                Vector3 faceNormal = normalize3(cross3(tangents[i], offsetDir));

                float t = cumLen[i] / totalLen;
                float w = app.startWidth + (app.endWidth - app.startWidth) * t;
                Vector4 col = lerpV4(app.startColor, app.endColor, t);

                // 両端フェード（αに焼き込み）
                float fadeAlpha = 1.0f;
                if (app.fadeStartLength > 1e-6f && cumLen[i] < app.fadeStartLength) {
                    fadeAlpha *= cumLen[i] / app.fadeStartLength;
                }
                float distFromEnd = totalLen - cumLen[i];
                if (app.fadeEndLength > 1e-6f && distFromEnd < app.fadeEndLength) {
                    fadeAlpha *= distFromEnd / app.fadeEndLength;
                }
                col.w *= fadeAlpha;

                float u = app.uvWrapByLength ? (cumLen[i] * app.uvTilesPerUnit) : t;

                MeshVertex v0{}, v1{};
                float halfW = w * 0.5f;
                v0.position = {
                    polyline[i].x + offsetDir.x * halfW,
                    polyline[i].y + offsetDir.y * halfW,
                    polyline[i].z + offsetDir.z * halfW
                };
                v0.normal = faceNormal;
                v0.color = col;
                v0.texcoord = { u, 0.0f };

                v1.position = {
                    polyline[i].x - offsetDir.x * halfW,
                    polyline[i].y - offsetDir.y * halfW,
                    polyline[i].z - offsetDir.z * halfW
                };
                v1.normal = faceNormal;
                v1.color = col;
                v1.texcoord = { u, 1.0f };

                mesh.vertices.push_back(v0);
                mesh.vertices.push_back(v1);
            }

            // セグメントごとに2三角形
            for (size_t i = 0; i < n - 1; ++i) {
                uint32_t i0 = baseIdx + static_cast<uint32_t>(i) * 2;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + 2;
                uint32_t i3 = i0 + 3;

                mesh.indices.push_back(i0);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i1);
                mesh.indices.push_back(i1);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i3);
            }
        }

        return mesh;
    }

    namespace {
        // 2つの MeshData を結合（v2 のインデックスをオフセット）
        void AppendMesh(MeshData& dst, const MeshData& src) {
            uint32_t baseIdx = static_cast<uint32_t>(dst.vertices.size());
            dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
            dst.indices.reserve(dst.indices.size() + src.indices.size());
            for (uint32_t i : src.indices) {
                dst.indices.push_back(i + baseIdx);
            }
        }

        // 線分 (a,b) のフラクタル分割。生成された折れ線を out へ追加（始点を含む）
        // depth=0 で end を含めて push する。
        void SubdivideSegment(const Vector3& a, const Vector3& b,
                              float offsetAmount, uint32_t depth,
                              std::mt19937& rng,
                              std::vector<Vector3>& out) {
            if (depth == 0) {
                out.push_back(b);
                return;
            }
            Vector3 dir = { b.x - a.x, b.y - a.y, b.z - a.z };
            float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
            if (len < 1e-6f) {
                out.push_back(b);
                return;
            }
            // 線分に垂直な平面内でランダム方向を選ぶ
            Vector3 nDir = { dir.x / len, dir.y / len, dir.z / len };
            Vector3 up = (std::abs(nDir.y) > 0.99f) ? Vector3{ 1.0f, 0.0f, 0.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
            Vector3 right = {
                nDir.y * up.z - nDir.z * up.y,
                nDir.z * up.x - nDir.x * up.z,
                nDir.x * up.y - nDir.y * up.x
            };
            float rLen = std::sqrt(right.x*right.x + right.y*right.y + right.z*right.z);
            if (rLen < 1e-6f) { out.push_back(b); return; }
            right = { right.x / rLen, right.y / rLen, right.z / rLen };
            Vector3 up2 = {
                right.y * nDir.z - right.z * nDir.y,
                right.z * nDir.x - right.x * nDir.z,
                right.x * nDir.y - right.y * nDir.x
            };
            std::uniform_real_distribution<float> distAngle(0.0f, 2.0f * 3.14159265358979323846f);
            std::uniform_real_distribution<float> distOffset(-offsetAmount, offsetAmount);
            float theta = distAngle(rng);
            float mag = distOffset(rng);
            Vector3 offsetVec = {
                (right.x * std::cos(theta) + up2.x * std::sin(theta)) * mag,
                (right.y * std::cos(theta) + up2.y * std::sin(theta)) * mag,
                (right.z * std::cos(theta) + up2.z * std::sin(theta)) * mag
            };
            Vector3 mid = {
                (a.x + b.x) * 0.5f + offsetVec.x,
                (a.y + b.y) * 0.5f + offsetVec.y,
                (a.z + b.z) * 0.5f + offsetVec.z
            };
            float nextOffset = offsetAmount * 0.5f; // 毎世代で半分
            SubdivideSegment(a, mid, nextOffset, depth - 1, rng, out);
            SubdivideSegment(mid, b, nextOffset, depth - 1, rng, out);
        }

        // 始点→終点のフラクタル折れ線を生成（始点を含み、終点を含む）
        std::vector<Vector3> GenerateFractalPolyline(const Vector3& start, const Vector3& end,
                                                     float maxOffset, uint32_t generations,
                                                     std::mt19937& rng) {
            std::vector<Vector3> out;
            out.push_back(start);
            SubdivideSegment(start, end, maxOffset, generations, rng, out);
            return out;
        }
    }

    MeshData CreateLightningBolt(const LightningBoltParams& params) {
        MeshData mesh;

        // RNG（seed=0 なら現在時刻、それ以外は固定）
        uint32_t seed = params.randomSeed;
        if (seed == 0) {
            seed = static_cast<uint32_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
        }
        std::mt19937 rng(seed);

        // 始終点距離 → maxOffset
        Vector3 d = { params.endPos.x - params.startPos.x,
                      params.endPos.y - params.startPos.y,
                      params.endPos.z - params.startPos.z };
        float totalLen = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
        if (totalLen < 1e-6f) return mesh;
        float maxOffset = totalLen * params.maxOffsetRatio;

        // 本線
        std::vector<Vector3> mainPoly =
            GenerateFractalPolyline(params.startPos, params.endPos, maxOffset, params.generations, rng);

        MeshData mainMesh = CreateBeamFromPolyline(mainPoly, params.appearance);
        AppendMesh(mesh, mainMesh);

        // 枝：本線の隣接点 (i, i+1) で確率的に分岐
        // 枝の終点 = mid + Rotate(dir) * lengthScale * |segment|
        if (params.branchProbability > 0.0f && mainPoly.size() >= 2) {
            std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
            std::uniform_real_distribution<float> distAngle(-params.branchMaxAngle, params.branchMaxAngle);

            // 枝用のappearance（細く・暗く）
            BeamAppearance branchApp = params.appearance;
            branchApp.startWidth *= params.branchWidthScale;
            branchApp.endWidth   *= params.branchWidthScale;
            branchApp.startColor.x *= params.branchColorScale;
            branchApp.startColor.y *= params.branchColorScale;
            branchApp.startColor.z *= params.branchColorScale;
            branchApp.startColor.w *= params.branchColorScale;
            branchApp.endColor.x *= params.branchColorScale;
            branchApp.endColor.y *= params.branchColorScale;
            branchApp.endColor.z *= params.branchColorScale;
            branchApp.endColor.w *= params.branchColorScale;

            for (size_t i = 0; i + 1 < mainPoly.size(); ++i) {
                if (dist01(rng) > params.branchProbability) continue;

                Vector3 a = mainPoly[i];
                Vector3 b = mainPoly[i + 1];
                Vector3 seg = { b.x - a.x, b.y - a.y, b.z - a.z };
                float segLen = std::sqrt(seg.x*seg.x + seg.y*seg.y + seg.z*seg.z);
                if (segLen < 1e-6f) continue;

                // 軸まわりにランダム角度で回転（本線方向から少しズラす）
                Vector3 nSeg = { seg.x / segLen, seg.y / segLen, seg.z / segLen };
                Vector3 upRef = (std::abs(nSeg.y) > 0.99f) ? Vector3{ 1.0f, 0.0f, 0.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
                Vector3 axR = {
                    nSeg.y * upRef.z - nSeg.z * upRef.y,
                    nSeg.z * upRef.x - nSeg.x * upRef.z,
                    nSeg.x * upRef.y - nSeg.y * upRef.x
                };
                float arLen = std::sqrt(axR.x*axR.x + axR.y*axR.y + axR.z*axR.z);
                if (arLen < 1e-6f) continue;
                axR = { axR.x / arLen, axR.y / arLen, axR.z / arLen };
                Vector3 axU = {
                    axR.y * nSeg.z - axR.z * nSeg.y,
                    axR.z * nSeg.x - axR.x * nSeg.z,
                    axR.x * nSeg.y - axR.y * nSeg.x
                };

                // 軸まわり回転 + 軸からの傾き
                float tilt = distAngle(rng);
                float roll = std::uniform_real_distribution<float>(0.0f, 2.0f * 3.14159265358979323846f)(rng);
                Vector3 tiltDir = {
                    nSeg.x * std::cos(tilt) + (axR.x * std::cos(roll) + axU.x * std::sin(roll)) * std::sin(tilt),
                    nSeg.y * std::cos(tilt) + (axR.y * std::cos(roll) + axU.y * std::sin(roll)) * std::sin(tilt),
                    nSeg.z * std::cos(tilt) + (axR.z * std::cos(roll) + axU.z * std::sin(roll)) * std::sin(tilt)
                };
                // 枝の長さは「ボルト全長 × branchLengthScale」を基準にする。
                // 分割後の単一セグメントだと短すぎて見えないため。
                float branchLen = totalLen * params.branchLengthScale;
                Vector3 branchEnd = {
                    b.x + tiltDir.x * branchLen,
                    b.y + tiltDir.y * branchLen,
                    b.z + tiltDir.z * branchLen
                };

                // 枝にもフラクタル（世代は本線より浅く）
                uint32_t branchGen = params.generations > 1 ? params.generations - 1 : 1;
                float branchMaxOffset = branchLen * params.maxOffsetRatio;
                std::vector<Vector3> branchPoly =
                    GenerateFractalPolyline(b, branchEnd, branchMaxOffset, branchGen, rng);

                MeshData branchMesh = CreateBeamFromPolyline(branchPoly, branchApp);
                AppendMesh(mesh, branchMesh);
            }
        }

        return mesh;
    }

    MeshData CreateBeam(const BeamParams& params) {
        const uint32_t segs = params.lengthSegments < 1 ? 1 : params.lengthSegments;
        std::vector<Vector3> polyline(segs + 1);
        for (uint32_t i = 0; i <= segs; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(segs);
            polyline[i] = {
                params.startPos.x + (params.endPos.x - params.startPos.x) * t,
                params.startPos.y + (params.endPos.y - params.startPos.y) * t,
                params.startPos.z + (params.endPos.z - params.startPos.z) * t,
            };
        }
        return CreateBeamFromPolyline(polyline, params.appearance);
    }

    MeshData CreateHelix(const HelixParams& params) {
        MeshData mesh;

        const float kPi = 3.14159265358979323846f;

        uint32_t circleSegments = params.circleSegments < 3 ? 3 : params.circleSegments;
        uint32_t lengthSegments = params.lengthSegments < 1 ? 1 : params.lengthSegments;

        const float totalHeight = params.pitch * params.turns;
        const float yOffset = -totalHeight * 0.5f; // 中心が原点
        const float angularSpeed = 2.0f * kPi * params.turns; // dθ/dt（t∈[0,1]）

        // 線形補間ヘルパ
        auto lerpf = [](float a, float b, float t) { return a + (b - a) * t; };
        auto lerpV4 = [](const Vector4& a, const Vector4& b, float t) {
            return Vector4{
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t
            };
            };

        auto normalize3 = [](const Vector3& v) -> Vector3 {
            float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (len < 1e-6f) return { 1.0f, 0.0f, 0.0f };
            return { v.x / len, v.y / len, v.z / len };
            };
        auto cross3 = [](const Vector3& a, const Vector3& b) -> Vector3 {
            return {
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            };
            };

        // (lengthSegments+1) × (circleSegments+1) のグリッドで頂点生成
        const uint32_t ringVertCount = circleSegments + 1;

        for (uint32_t i = 0; i <= lengthSegments; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(lengthSegments);
            float theta = angularSpeed * t;
            float sinT = std::sin(theta);
            float cosT = std::cos(theta);

            float helixR = lerpf(params.startHelixRadius, params.endHelixRadius, t);
            float tubeR  = lerpf(params.startTubeRadius,  params.endTubeRadius,  t);
            Vector4 col  = lerpV4(params.startColor, params.endColor, t);

            // 螺旋の中心点 P(t) = (helixR*cosθ, totalHeight*t + yOffset, helixR*sinθ)
            Vector3 center = {
                helixR * cosT,
                totalHeight * t + yOffset,
                helixR * sinT
            };

            // 接線 dP/dt
            // dHelixR/dt = (end - start)
            float dHelixR = (params.endHelixRadius - params.startHelixRadius);
            Vector3 tangent = {
                dHelixR * cosT - helixR * sinT * angularSpeed,
                totalHeight,
                dHelixR * sinT + helixR * cosT * angularSpeed
            };
            Vector3 T = normalize3(tangent);

            // 安定したフレーム：Yワールドが接線とほぼ平行なときはXに切り替え
            Vector3 up = (std::abs(T.y) > 0.99f) ? Vector3{ 1.0f, 0.0f, 0.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
            Vector3 N = normalize3(cross3(T, up));
            Vector3 B = cross3(N, T); // 既に正規直交

            // 円周頂点
            for (uint32_t j = 0; j <= circleSegments; ++j) {
                float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(circleSegments);
                float sinP = std::sin(phi);
                float cosP = std::cos(phi);

                // 外向き法線
                Vector3 normal = {
                    N.x * cosP + B.x * sinP,
                    N.y * cosP + B.y * sinP,
                    N.z * cosP + B.z * sinP
                };

                MeshVertex v{};
                v.position = {
                    center.x + tubeR * normal.x,
                    center.y + tubeR * normal.y,
                    center.z + tubeR * normal.z
                };
                v.normal = normal;
                v.color  = col;
                // U=長さ方向（スクロールで「流れる」演出）、V=円周方向
                v.texcoord = {
                    t,
                    static_cast<float>(j) / static_cast<float>(circleSegments)
                };
                mesh.vertices.push_back(v);
            }
        }

        // インデックス生成（グリッドを2三角形ずつ繋ぐ）
        for (uint32_t i = 0; i < lengthSegments; ++i) {
            for (uint32_t j = 0; j < circleSegments; ++j) {
                uint32_t i0 = i * ringVertCount + j;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + ringVertCount;
                uint32_t i3 = i2 + 1;

                // 表側：外向き法線で見える側
                mesh.indices.push_back(i0);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i1);

                mesh.indices.push_back(i1);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i3);
            }
        }

        return mesh;
    }
}