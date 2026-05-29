#pragma once
#include "MeshData.h"
#include <vector>

namespace PrimitiveGenerator {

    // XY平面のPlaneを生成（中心が原点、法線はZ+方向）
    MeshData CreatePlane(
        float width = 1.0f,
        float height = 1.0f,
        uint32_t divisionsX = 1,
        uint32_t divisionsY = 1,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    // Boxを生成（中心が原点）
    MeshData CreateBox(
        const Vector3& size = { 1.0f, 1.0f, 1.0f },
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    // Ringの生成パラメータ
    struct RingParams {
        // 基本形状
        float outerRadius = 1.0f;
        float innerRadius = 0.5f;
        uint32_t divisions = 32;

        // 色（内側・外側）
        Vector4 innerColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 outerColor = { 1.0f, 1.0f, 1.0f, 1.0f };

        // 拡張①：角度範囲（ラジアン指定、デフォルトは円全体）
        float startAngle = 0.0f;
        float endAngle = 2.0f * 3.14159265358979323846f;

        bool uvHorizon = true;
        float fadeStart = 0.0f;  // デフォルト：内径からグラデーションを開始
        float fadeEnd = 1.0f;    // デフォルト：外径で終わる
        std::vector<float> outerRadiusPerDivision;
    };

    // Ringを生成（XY平面、Z+方向法線、中心が原点）
    MeshData CreateRing(
        float outerRadius = 1.0f,
        float innerRadius = 0.5f,
        uint32_t divisions = 32,
        const Vector4& innerColor = { 1.0f, 1.0f, 1.0f, 1.0f },
        const Vector4& outerColor = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    // RingParams版（拡張機能を利用可能）
    MeshData CreateRing(const RingParams& params);

    // Cylinderの生成パラメータ
    struct CylinderParams {
        float topRadius = 1.0f;
        float bottomRadius = 1.0f;
        float height = 3.0f;
        uint32_t divisions = 32;

        // 上下別カラー（縦グラデーション）
        Vector4 topColor    = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 bottomColor = { 1.0f, 1.0f, 1.0f, 1.0f };

        // 角度範囲（ラジアン。デフォルトは円全周）
        float startAngle = 0.0f;
        float endAngle   = 2.0f * 3.14159265358979323846f;
    };

    // Cylinderを生成（Y軸方向の筒、上下面なし、中心が原点）
    MeshData CreateCylinder(
        float topRadius = 1.0f,
        float bottomRadius = 1.0f,
        float height = 3.0f,
        uint32_t divisions = 32,
        const Vector4& topColor = { 1.0f, 1.0f, 1.0f, 1.0f },
        const Vector4& bottomColor = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    // CylinderParams版（角度範囲・上下カラー）
    MeshData CreateCylinder(const CylinderParams& params);

    // Sphere（UV球）を生成（中心が原点）
    MeshData CreateSphere(
        float radius = 0.5f,
        uint32_t divisions = 16,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    // Helix（螺旋チューブ）の生成パラメータ
    // Y軸沿いに伸びる開いた管。蓋なし。プレイヤー弾の弾道などを想定。
    struct HelixParams {
        // 螺旋カーブの半径（中心軸からどれだけ離れるか）— 入口/出口で別
        float startHelixRadius = 0.3f;
        float endHelixRadius   = 0.3f;

        // チューブの太さ — 入口/出口で別
        float startTubeRadius  = 0.05f;
        float endTubeRadius    = 0.05f;

        // 軸方向（Y軸沿いに伸びる前提）
        float pitch = 1.0f;   // 1巻あたりのY方向長さ
        float turns = 5.0f;   // 巻数

        // 分割数
        uint32_t circleSegments = 8;    // チューブの円周
        uint32_t lengthSegments = 64;   // 螺旋の長さ方向

        // 上下別カラー（先端と末尾でグラデ）
        Vector4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 endColor   = { 1.0f, 0.3f, 0.0f, 0.2f };
    };

    // Helix（螺旋チューブ）を生成
    MeshData CreateHelix(const HelixParams& params);

    // Beam / Lightning 共通の見た目パラメータ
    struct BeamAppearance {
        float startWidth = 0.3f;
        float endWidth   = 0.3f;
        uint32_t planeCount = 3;            // 軸まわりに均等配置する交差Plane枚数
        float fadeStartLength = 0.2f;       // 始点側のフェード距離（頂点αに焼き込み）
        float fadeEndLength   = 0.2f;       // 終点側のフェード距離
        Vector4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 endColor   = { 1.0f, 1.0f, 1.0f, 1.0f };
        bool   uvWrapByLength = true;       // U方向を長さに比例させる
        float  uvTilesPerUnit = 1.0f;       // uvWrapByLength=true時、1ユニットあたりのUタイル数
    };

    // 直線ビーム（レーザー）の生成パラメータ
    struct BeamParams {
        Vector3 startPos = { 0.0f, 0.0f, 0.0f };
        Vector3 endPos   = { 0.0f, 0.0f, 1.0f };
        BeamAppearance appearance;
        // 始点→終点の自動分割数（両端フェードの補間に最低限の中間頂点が必要）
        uint32_t lengthSegments = 16;
    };

    // 折れ線→交差Plane帯メッシュ（雷の内部実装でも使う共通関数）
    MeshData CreateBeamFromPolyline(const std::vector<Vector3>& polyline, const BeamAppearance& app);

    // 直線ビームを生成（CreateBeamFromPolyline に2点を渡す薄ラッパー）
    MeshData CreateBeam(const BeamParams& params);

    // 雷（ジグザグ折れ線）の生成パラメータ
    // 始点→終点をフラクタル再帰で分割しオフセットしてジグザグを作る。枝分岐あり。
    struct LightningBoltParams {
        Vector3 startPos = { 0.0f, 0.0f, 0.0f };
        Vector3 endPos   = { 0.0f, 0.0f, 1.0f };
        BeamAppearance appearance;

        // フラクタル分割
        uint32_t generations    = 5;      // 分割回数（多いほど細かい）
        float    maxOffsetRatio = 0.15f;  // 始終点距離に対するオフセット比率
        uint32_t randomSeed     = 0;      // 0=毎回ランダム、固定値で再現可

        // 枝分かれ
        float branchProbability = 0.15f;  // 各セグメントで枝を生やす確率（分割後の全セグメントに適用）
        float branchLengthScale = 0.25f;  // 枝の長さ＝ボルト全長 × この値（0.2〜0.4 が見やすい）
        float branchMaxAngle    = 0.6f;   // 本線方向からの最大ずれ角（ラジアン）
        float branchWidthScale  = 0.5f;   // 枝の太さ倍率
        float branchColorScale  = 0.5f;   // 枝の明るさ倍率（α含む）
    };

    // 雷メッシュを生成（本線＋枝を合成した1つの MeshData を返す）
    MeshData CreateLightningBolt(const LightningBoltParams& params);

}