#pragma once
#include "MeshData.h"
#include "MathUtility.h"
#include <vector>

namespace PrimitiveGenerator {

    /// <summary>XY平面のPlaneを生成（中心が原点、法線はZ+方向）</summary>
    /// <param name="width">幅（X）</param>
    /// <param name="height">高さ（Y）</param>
    /// <param name="divisionsX">X 方向の分割数</param>
    /// <param name="divisionsY">Y 方向の分割数</param>
    /// <param name="color">頂点色</param>
    /// <returns>生成したメッシュ</returns>
    MeshData CreatePlane(
        float width = 1.0f,
        float height = 1.0f,
        uint32_t divisionsX = 1,
        uint32_t divisionsY = 1,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    /// <summary>Boxを生成（中心が原点）</summary>
    /// <param name="size">各軸の大きさ</param>
    /// <param name="color">頂点色</param>
    /// <returns>生成したメッシュ</returns>
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
        float endAngle = kTwoPi;

        bool uvHorizon = true;
        float fadeStart = 0.0f;  // デフォルト：内径からグラデーションを開始
        float fadeEnd = 1.0f;    // デフォルト：外径で終わる
        std::vector<float> outerRadiusPerDivision;
    };

    /// <summary>Ringを生成（XY平面、Z+方向法線、中心が原点）</summary>
    /// <param name="outerRadius">外径</param>
    /// <param name="innerRadius">内径</param>
    /// <param name="divisions">円周の分割数</param>
    /// <param name="innerColor">内側の頂点色</param>
    /// <param name="outerColor">外側の頂点色</param>
    /// <returns>生成したメッシュ</returns>
    MeshData CreateRing(
        float outerRadius = 1.0f,
        float innerRadius = 0.5f,
        uint32_t divisions = 32,
        const Vector4& innerColor = { 1.0f, 1.0f, 1.0f, 1.0f },
        const Vector4& outerColor = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    /// <summary>RingParams版（角度範囲・UV の向き・グラデーション範囲・分割ごとの外径を利用可能）</summary>
    /// <param name="params">リングの生成パラメータ</param>
    /// <returns>生成したメッシュ</returns>
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
        float endAngle   = kTwoPi;
    };

    /// <summary>Cylinderを生成（Y軸方向の筒、上下面なし、中心が原点）</summary>
    /// <param name="topRadius">上端の半径</param>
    /// <param name="bottomRadius">下端の半径</param>
    /// <param name="height">高さ</param>
    /// <param name="divisions">円周の分割数</param>
    /// <param name="topColor">上端の頂点色</param>
    /// <param name="bottomColor">下端の頂点色</param>
    /// <returns>生成したメッシュ</returns>
    MeshData CreateCylinder(
        float topRadius = 1.0f,
        float bottomRadius = 1.0f,
        float height = 3.0f,
        uint32_t divisions = 32,
        const Vector4& topColor = { 1.0f, 1.0f, 1.0f, 1.0f },
        const Vector4& bottomColor = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    /// <summary>CylinderParams版（角度範囲・上下カラー）</summary>
    /// <param name="params">円柱の生成パラメータ</param>
    /// <returns>生成したメッシュ</returns>
    MeshData CreateCylinder(const CylinderParams& params);

    /// <summary>Sphere（UV球）を生成（中心が原点）</summary>
    /// <param name="radius">半径</param>
    /// <param name="divisions">経度・緯度の分割数</param>
    /// <param name="color">頂点色</param>
    /// <returns>生成したメッシュ</returns>
    MeshData CreateSphere(
        float radius = 0.5f,
        uint32_t divisions = 16,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    /// <summary>
    /// Hemisphere（半球＝ドーム）を生成。蓋なしの中空シェル（おわん型の片割れ）。
    /// 上半球：頂点が +Y、開口（リム）が Y=0 平面。中が空なので両面描画推奨。
    /// </summary>
    /// <param name="radius">半径</param>
    /// <param name="divisions">経度・緯度の分割数</param>
    /// <param name="color">頂点色</param>
    /// <returns>生成したメッシュ</returns>
    MeshData CreateHemisphere(
        float radius = 0.5f,
        uint32_t divisions = 16,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    // Frame（額縁）の生成パラメータ：外周 Plane から内側矩形をくり抜いた枠（XY平面、Z+法線、中心が原点）。
    struct FrameParams {
        float   outerWidth  = 1.0f;
        float   outerHeight = 1.0f;
        float   innerWidth  = 0.5f; // くり抜く穴の幅（outerWidth 以下）
        float   innerHeight = 0.5f; // くり抜く穴の高さ（outerHeight 以下）
        Vector4 color       = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    /// <summary>Frame（額縁状の枠）を生成。外周と内側穴の差分を4つの台形でタイル化する</summary>
    /// <param name="params">枠の生成パラメータ</param>
    /// <returns>生成したメッシュ</returns>
    MeshData CreateFrame(const FrameParams& params);

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

    /// <summary>Helix（螺旋チューブ）を生成</summary>
    /// <param name="params">螺旋の生成パラメータ</param>
    /// <returns>生成したメッシュ</returns>
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

    /// <summary>折れ線→交差Plane帯メッシュ（雷の内部実装でも使う共通関数）。両端のフェードは頂点αに焼き込む</summary>
    /// <param name="polyline">通る点の列</param>
    /// <param name="app">幅・板の枚数・フェード・色・UV</param>
    /// <returns>生成したメッシュ</returns>
    MeshData CreateBeamFromPolyline(const std::vector<Vector3>& polyline, const BeamAppearance& app);

    /// <summary>直線ビームを生成（始点→終点を lengthSegments 等分した折れ線で CreateBeamFromPolyline を呼ぶ薄ラッパー）</summary>
    /// <param name="params">ビームの生成パラメータ</param>
    /// <returns>生成したメッシュ</returns>
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

    /// <summary>雷メッシュを生成（本線＋枝を合成した1つの MeshData を返す）</summary>
    /// <param name="params">雷の生成パラメータ</param>
    /// <returns>生成したメッシュ。始終点がほぼ同じなら空</returns>
    MeshData CreateLightningBolt(const LightningBoltParams& params);

}