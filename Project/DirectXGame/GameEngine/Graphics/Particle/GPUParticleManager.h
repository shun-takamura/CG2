#pragma once
#include "DirectXCore.h"
#include "SRVManager.h"
#include"Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "BillboardMode.h"
#include "TimeGroup.h"
#include <wrl.h>
#include <d3d12.h>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <utility>
#include <array>

struct Material;

class Camera;

// GPU Particle 管理クラス
// - グループ名でN個のパーティクルプールを管理
// - 各グループ 1024個のParticleをDEFAULT heapに保持
// - 初期化/Emit/Update は ComputeShader で行い、描画はStructuredBufferをVSで参照
class GPUParticleManager
{
public:
    static const uint32_t kMaxParticles = 1024;

    // ブレンドモード（EffectDef の int 値と互換。None=0, Normal=1, Add=2, Subtract=3, Multiply=4, Screen=5）
    enum BlendMode {
        kBlendModeNone,
        kBlendModeNormal,
        kBlendModeAdd,
        kBlendModeSubtract,
        kBlendModeMultiply,
        kBlendModeScreen,
        kCountOfBlendMode
    };

    // プレビュー専用グループ名のプレフィックス。EffectInstance がプレビュー時に
    // グループ名へ付加し、シーン用とは物理バッファを分離する（隔離の要）。
    static constexpr const char* kPreviewPrefix = "$preview$";
    /// <summary>名前がプレビュー専用グループ（kPreviewPrefix 付き）かどうか</summary>
    /// <param name="name">グループ名</param>
    /// <returns>プレフィックスで始まれば true</returns>
    static bool IsPreviewName(const std::string& name);

    /// <summary>Init/Emit/Update の CS パイプライン、ブレンドモード別＋Bloom の描画 PSO、板の頂点、既定マテリアル（white1x1 も確保）を作る</summary>
    /// <param name="dxCore">デバイスとコマンドリストの取得元</param>
    /// <param name="srvManager">粒子バッファの SRV/UAV を確保する先</param>
    void Initialize(DirectXCore* dxCore, SRVManager* srvManager);
    /// <summary>全グループ（遅延解放待ちも含む）と共通のパイプライン・リソースを解放する</summary>
    void Finalize();

    // ===== グループ管理 =====
    /// <summary>
    /// 新しいパーティクルグループを生成。同名が既にあれば何もしない。
    /// </summary>
    /// <param name="name">グループ名（kPreviewPrefix 付きならプレビュー専用）</param>
    /// <param name="texturePath">粒子のテクスチャのパス</param>
    void CreateGroup(const std::string& name, const std::string& texturePath);
    /// <summary>グループを即座に解放する（GPU で使用中でないこと）。無ければ何もしない</summary>
    /// <param name="name">グループ名</param>
    void RemoveGroup(const std::string& name);
    /// <summary>グループがあるか</summary>
    /// <param name="name">グループ名</param>
    /// <returns>あれば true</returns>
    bool HasGroup(const std::string& name) const;

    /// <summary>粒子の色の付け方（BurstEmit の colorMode。EffectDef の colorMode と同じ値）</summary>
    enum ColorMode : uint32_t {
        kColorModeRandom = 0,   // 粒子ごとにランダムな色
        kColorModeFixed  = 1,   // startColor → endColor（グラデーションのキーがあればそれ）で補間
    };
    /// <summary>粒子の初速の向き（SetEmitterVelocity の mode。EffectDef の velocityMode と同じ値）</summary>
    enum VelocityMode : int {
        kVelocityRandom      = 0,   // 全方向ランダム
        kVelocityDirectional = 1,   // 方向固定（baseVelocity の向き）
        kVelocityRadial      = 2,   // 発生の中心から外向き（速さは baseVelocity.x）
        kVelocityTangent     = 3,   // 接線（公転の初速。速さは baseVelocity.x）
        kVelocityConverge    = 4,   // 収束（発生位置を velocity に保持して、後で中心へ引き寄せる）
    };

    // ===== 発射API =====
    /// <summary>
    /// 1回だけバースト発射（次フレームの Emit CS で N個を一括生成）
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="position">発生の中心のワールド座標</param>
    /// <param name="count">発生数</param>
    /// <param name="radius">発生位置の散らばり半径 [m]</param>
    void BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius = 0.5f);

    /// <summary>
    /// 色指定付きのバースト発射。colorMode=0でRandom（startColor/endColor無視）、=1でstartColor→endColor補間
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="position">発生の中心のワールド座標</param>
    /// <param name="count">発生数</param>
    /// <param name="radius">発生位置の散らばり半径 [m]</param>
    /// <param name="colorMode">0=Random / 1=Fixed</param>
    /// <param name="startColor">寿命開始時の色（Fixed のとき）</param>
    /// <param name="endColor">寿命終了時の色（Fixed のとき）</param>
    void BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius,
                   uint32_t colorMode, const Vector4& startColor, const Vector4& endColor);

    /// <summary>
    /// 色 + サイズ範囲指定のバースト。uniformScale=true で幅=高さ（Xレンジを共用）。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="position">発生の中心のワールド座標</param>
    /// <param name="count">発生数</param>
    /// <param name="radius">発生位置の散らばり半径 [m]</param>
    /// <param name="colorMode">0=Random / 1=Fixed</param>
    /// <param name="startColor">寿命開始時の色（Fixed のとき）</param>
    /// <param name="endColor">寿命終了時の色（Fixed のとき）</param>
    /// <param name="scaleMin">最小サイズ（幅, 高さ）</param>
    /// <param name="scaleMax">最大サイズ（幅, 高さ）</param>
    /// <param name="uniformScale">幅=高さにするなら true</param>
    void BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius,
                   uint32_t colorMode, const Vector4& startColor, const Vector4& endColor,
                   const Vector2& scaleMin, const Vector2& scaleMax, bool uniformScale);

    /// <summary>
    /// 粒子寿命まで指定するバージョン。Effect の totalDuration でクランプしたいとき等に使う。
    /// 実際の発射は次の Update で emit フラグを立てたあとの Emit CS（CPU/GPU のレース回避）。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="position">発生の中心のワールド座標</param>
    /// <param name="count">発生数</param>
    /// <param name="radius">発生位置の散らばり半径 [m]</param>
    /// <param name="colorMode">0=Random / 1=Fixed</param>
    /// <param name="startColor">寿命開始時の色（Fixed のとき）</param>
    /// <param name="endColor">寿命終了時の色（Fixed のとき）</param>
    /// <param name="scaleMin">最小サイズ（幅, 高さ）</param>
    /// <param name="scaleMax">最大サイズ（幅, 高さ）</param>
    /// <param name="uniformScale">幅=高さにするなら true</param>
    /// <param name="particleLifeTime">各粒子の寿命 [秒]（最小 0.0001）</param>
    /// <param name="lifeScaleStart">寿命開始時のサイズ倍率</param>
    /// <param name="lifeScaleEnd">寿命終了時のサイズ倍率</param>
    /// <param name="rotRandomRange">出現時ランダム初期姿勢の各軸の最大角 [ラジアン]</param>
    /// <param name="rotateSpeed">各軸の角速度 [rad/s]</param>
    void BurstEmit(const std::string& name, const Vector3& position, uint32_t count, float radius,
                   uint32_t colorMode, const Vector4& startColor, const Vector4& endColor,
                   const Vector2& scaleMin, const Vector2& scaleMax, bool uniformScale,
                   float particleLifeTime,
                   float lifeScaleStart = 1.0f, float lifeScaleEnd = 1.0f,
                   const Vector3& rotRandomRange = { 0.0f, 0.0f, 0.0f },
                   const Vector3& rotateSpeed = { 0.0f, 0.0f, 0.0f });

    /// <summary>
    /// 連続発射のON/OFFと頻度設定
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="enabled">連続発射するなら true</param>
    /// <param name="frequency">発射の間隔 [秒]</param>
    /// <param name="countPerEmit">1 回の発生数</param>
    /// <param name="radius">発生位置の散らばり半径 [m]</param>
    void SetContinuousEmit(const std::string& name, bool enabled, float frequency = 0.5f, uint32_t countPerEmit = 10, float radius = 1.0f);
    /// <summary>エミッタの位置を設定する</summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="translate">発生の中心のワールド座標</param>
    void SetEmitterTranslate(const std::string& name, const Vector3& translate);

    /// <summary>
    /// 初速モードを設定。mode: 0=全方向ランダム(従来) / 1=baseVelocity固定 / 2=放射(中心から外、baseVelocity.x=速さ)。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="baseVelocity">固定方向の初速（mode=2 では x が速さ）</param>
    /// <param name="jitter">速度ゆらぎ量</param>
    /// <param name="mode">初速モード</param>
    void SetEmitterVelocity(const std::string& name, const Vector3& baseVelocity, float jitter, int mode = kVelocityDirectional);

    /// <summary>
    /// 発生形状を設定。mode: 0=Sphere(従来) / 1=Ring（normal まわりの円周 + thickness の散らばり）。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="mode">0=Sphere / 1=Ring</param>
    /// <param name="ringNormal">Ring 平面の法線</param>
    /// <param name="ringThickness">Ring の太さ（円周まわりの散らばり）</param>
    void SetEmitterShape(const std::string& name, int mode, const Vector3& ringNormal, float ringThickness);

    /// <summary>
    /// 周回（orbit）を設定。enabled なら粒子を center まわりに axis で angularSpeed[rad/s] 回す（外に出さない）。
    /// center は毎フレ更新（プレイヤー追従など）して良い。
    /// radiusEnable なら周回半径を寿命に沿って 1 → radiusEndRatio 倍へ変える（進み方は SetGroupMotionLUT）。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="enabled">周回させるなら true</param>
    /// <param name="center">周回の中心のワールド座標（動いた分だけ粒子も一緒に動かす）</param>
    /// <param name="spinAxis">帯上を流れる回転の軸</param>
    /// <param name="spinSpeed">帯上を流れる速度 [rad/s]</param>
    /// <param name="tumbleAxis">帯自体の回転の軸</param>
    /// <param name="tumbleSpeed">帯自体の回転速度 [rad/s]</param>
    /// <param name="radiusEnable">周回半径を寿命に沿って変えるなら true</param>
    /// <param name="radiusEndRatio">最終円の半径 / 生成時の円の半径</param>
    void SetGroupOrbit(const std::string& name, bool enabled, const Vector3& center,
                       const Vector3& spinAxis, float spinSpeed,
                       const Vector3& tumbleAxis, float tumbleSpeed,
                       bool radiusEnable = false, float radiusEndRatio = 1.0f);

    /// <summary>
    /// 収束（移動をカーブで制御）を設定。enable で各粒子を spawn 位置から center へ寄せる。
    /// 進み方は SetGroupMotionLUT（0=spawn, 1=center）。
    /// 併せて velocityMode=4（emit 時に spawn 位置を保持）にしておくこと。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="enable">収束させるなら true</param>
    /// <param name="center">収束先のワールド座標</param>
    void SetGroupConverge(const std::string& name, bool enable, const Vector3& center);

    /// <summary>
    /// 移動カーブ LUT を設定。lut32 はカーブ(0..1)を 32 サンプルに焼いた配列。
    /// converge 時は収束の進み、orbit 時は半径変化の進みとして使われる（両者は排他）。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="lut32">0〜1 のカーブを 32 サンプルに焼いた配列（float4×8 に詰めて CB へ）</param>
    void SetGroupMotionLUT(const std::string& name, const float lut32[32]);

    /// <summary>
    /// 多色グラデーションを設定。locations(0..1) と colors の組を最大 kMaxGradientKeys 個。
    /// 2個未満なら無効化（粒子の start/end 2色補間に戻る）。CPU 側で location 昇順にソートして渡す。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="keys">（位置 0〜1, 色）の組。location 昇順で渡す。kMaxGradientKeys を超えた分は捨てる</param>
    void SetEmitterGradient(const std::string& name, const std::vector<std::pair<float, Vector4>>& keys);

    /// <summary>
    /// 生存中のシームレスな色変化（Hue 回転）を設定。enable で寿命補間色の色相を時間に沿って回す。
    /// speed=1秒あたりの回転数（1.0=毎秒1周）。randomPhase で粒子ごとに位相をばらす（虹色ドリフト）。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="enable">色相を回すなら true</param>
    /// <param name="speed">1 秒あたりの回転数</param>
    /// <param name="randomPhase">粒子ごとに位相をばらすなら true</param>
    void SetGroupHueShift(const std::string& name, bool enable, float speed, bool randomPhase);

    /// <summary>
    /// グループの「粒子ごとの寿命ディゾルブ」を設定。各粒子が自分の寿命比率(0..1)に応じて
    /// In(出現:[0,inEnd]) / Out(消滅:[outStart,1]) でマスク discard される。maskPath 空 or enable=false で無効。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="enable">ディゾルブを使うなら true</param>
    /// <param name="maskPath">マスクのパス（PS t1。空なら white1x1）</param>
    /// <param name="inEnable">出現のディゾルブを使うなら true</param>
    /// <param name="inEnd">この寿命比率までに出現し終える</param>
    /// <param name="outEnable">消滅のディゾルブを使うなら true</param>
    /// <param name="outStart">この寿命比率から消え始める</param>
    /// <param name="edgeEnable">燃える縁を出すなら true</param>
    /// <param name="edgeColor">縁の色</param>
    /// <param name="edgeWidth">縁の幅（閾値の単位）</param>
    void SetGroupDissolve(const std::string& name, bool enable, const std::string& maskPath,
                          bool inEnable, float inEnd, bool outEnable, float outStart,
                          bool edgeEnable, const Vector4& edgeColor, float edgeWidth);

    /// <summary>
    /// 既存グループのテクスチャを差し替える（パスが変われば SRV を貼り直す）。
    /// グループ生成時のテクスチャを後から変更したいとき（エディタの D&D 等）に使う。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="texturePath">新しいテクスチャのパス（空なら何もしない）</param>
    void SetGroupTexture(const std::string& name, const std::string& texturePath);

    // ===== ブレンド / ビルボード / TimeGroup =====
    /// <summary>
    /// グループの描画ブレンドモードを設定（None=0..Screen=5）。加算では黒系粒子が映らないため Normal 等を選ぶ。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="mode">ブレンドモード（範囲外は Add）</param>
    void SetGroupBlendMode(const std::string& name, int mode);
    /// <summary>グループのビルボードの種類を設定する</summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="mode">ビルボードの種類</param>
    void SetGroupBillboardMode(const std::string& name, BillboardMode mode);
    /// <summary>グループの時間の進み方（EngineTime のスケール）を選ぶ</summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="group">時間グループ</param>
    void SetGroupTimeGroup(const std::string& name, TimeGroup group);

    // ===== Bloom =====
    /// <summary>
    /// グループを Bloom 対象にする。有効なグループは DrawBloom / DrawBloomPreview で発光 RT に
    /// 「通常描画と同じ見た目 × intensity」を加算で描く。
    /// </summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="enable">発光させるなら true</param>
    /// <param name="intensity">発光の強さ（PS b0 のマテリアル色に入れる。1 を超えてよい）</param>
    void SetGroupBloom(const std::string& name, bool enable, float intensity);
    /// <summary>水面の反射 RT に描くか（既定 true）</summary>
    /// <param name="name">グループ名（無ければ何もしない）</param>
    /// <param name="reflect">反射に描くなら true</param>
    void SetGroupReflect(const std::string& name, bool reflect);
    /// <summary>Bloom 対象のグループが（シーン用 or プレビュー用に）1つでもあるか</summary>
    /// <param name="preview">true ならプレビュー用、false ならシーン用を調べる</param>
    /// <returns>GPU で初期化済みの Bloom 対象グループがあれば true</returns>
    bool HasBloomGroup(bool preview) const;
    /// <summary>Bloom 対象のシーン用グループを発光 RT に描く（シミュレーションはしない。Draw の後に呼ぶ）</summary>
    void DrawBloom();
    /// <summary>DrawBloom のプレビュー版（DrawPreview の後に呼ぶ）</summary>
    void DrawBloomPreview();

    // ===== 毎フレーム =====
    /// <summary>
    /// シーン用グループのみを更新する（プレビュー用グループはスキップ）。
    /// カメラから VP・ビルボード行列を作って PerView に書き、emit フラグと PerFrame を確定する。
    /// </summary>
    /// <param name="camera">シーンのカメラ（nullptr なら前回の行列のまま）</param>
    /// <param name="deltaTime">実経過時間 [秒]（各グループの TimeGroup で EngineTime 経由にスケールされる）</param>
    void Update(const Camera* camera, float deltaTime);
    /// <summary>シーン用グループのみをシミュレート＋描画する（プレビュー用は DrawPreview 側）</summary>
    void Draw();

    // ===== プレビュー（エディタ専用・シーンから独立） =====
    /// <summary>
    /// プレビュー用グループの emit/PerFrame をエディタの unscaled delta で進める。
    /// 併せて前フレームに遅延予約したプレビューグループを安全に解放する。
    /// </summary>
    /// <param name="deltaTime">エディタのタイムラインで進める時間 [秒]</param>
    void UpdatePreviewSim(float deltaTime);

    /// <summary>
    /// keepNames に無いプレビュー用グループを遅延解放キューへ移す（次フレーム頭で実解放）。
    /// 「今プレビュー中のエフェクトが使うグループ」だけを残し、working set を有界化する。
    /// </summary>
    /// <param name="keepNames">残すグループ名（プレフィックス込み）</param>
    void RecyclePreviewGroups(const std::vector<std::string>& keepNames);

    /// <summary>プレビュー用 PerView（カメラ）を全グループに書く（メインの Update とは独立）</summary>
    /// <param name="viewMatrix">プレビューカメラのビュー行列</param>
    /// <param name="viewProjectionMatrix">プレビューカメラのビュープロジェクション行列</param>
    /// <param name="cameraPos">プレビューカメラの位置</param>
    void UpdatePreviewView(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjectionMatrix, const Vector3& cameraPos);

    /// <summary>プレビュー用グループを独立シミュレート＋描画（プレビュー用 PerView を使う）</summary>
    void DrawPreview();

    /// <summary>
    /// 水面の反射 RT へ全グループ（プレビュー以外・reflect=true）を描く。シミュレーションはせず、
    /// 前フレームに進めた粒子をそのまま描く（反射のパスはシーン描画の最初で、まだ今フレームの Draw 前のため）。
    /// 鏡像の View でビルボードを組むので、反射の中でも粒子は画面を向く。
    /// </summary>
    /// <param name="mirroredView">水面で折り返したビュー行列</param>
    /// <param name="mirroredViewProjection">水面で折り返したビュープロジェクション行列</param>
    /// <param name="mirroredCameraPos">水面で折り返したカメラ位置</param>
    void DrawReflection(const Matrix4x4& mirroredView, const Matrix4x4& mirroredViewProjection, const Vector3& mirroredCameraPos);

    /// <summary>ImGui デバッグUI（グループごとのビルボード・TimeGroup・連続発射・エミッタ・即バースト）</summary>
    void OnImGui();

private:
    // C++ <-> HLSL でメンバ並びを一致させる
    struct ParticleCS
    {
        Vector3 translate;
        Vector3 scale;
        float lifeTime;
        Vector3 velocity;
        float currentTime;
        Vector4 color;       // 現在色（Update CS が毎フレーム計算）
        Vector4 startColor;  // 補間始点
        Vector4 endColor;    // 補間終点
        Vector2 lifeScale;   // 寿命に沿ったサイズ倍率（x=開始, y=終了）
        Vector4 orientation; // 3D姿勢クオータニオン（ビルボードNone時に使用）
        Vector3 angularVel;  // 各軸の角速度（rad/s）
    };

    struct PerView
    {
        Matrix4x4 viewProjection;
        Matrix4x4 billboardMatrix;     // Full ビルボード用
        Vector3   cameraPosition;      // YAxis ビルボード用
        uint32_t  billboardMode;       // 0=None, 1=Full, 2=YAxis
    };

    struct EmitterSphere
    {
        Vector3 translate;
        float radius;
        uint32_t count;
        float frequency;
        float frequencyTime;
        uint32_t emit;
        uint32_t colorMode;     // 0=Random, 1=Fixed
        Vector3 baseVelocity;   // velocityMode!=0 のときの初速方向（16-byte align も兼ねる）
        Vector4 startColor;
        Vector4 endColor;
        Vector2 scaleMin;       // 80..88
        Vector2 scaleMax;       // 88..96
        uint32_t uniformScale;  // 96..100
        float particleLifeTime; // 100..104（emit時に各粒子に設定される寿命）
        float velocityMode;     // 0=ランダム(従来), 1=baseVelocity+ジッタ
        float velocityJitter;   // velocityMode!=0 のときの速度ゆらぎ量
        float shapeMode;        // 0=Sphere(従来), 1=Ring
        Vector3 ringNormal;     // Ring の法線
        float ringThickness;    // Ring の太さ
        float lifeScaleStart;   // 寿命開始時のサイズ倍率
        float lifeScaleEnd;     // 寿命終了時のサイズ倍率
        float pad2;             // 16B境界合わせ
        Vector4 rotRandomRange; // .xyz=出現時ランダム初期姿勢の各軸最大角（rad）
        Vector4 rotateSpeed;    // .xyz=各軸の角速度（rad/s）
    };

    static const uint32_t kMaxGradientKeys = 8;
    // 多色グラデーション（Update CS の b1）。keyCount>=2 で有効、それ未満は粒子の start/end 2色補間。
    struct ParticleGradient
    {
        uint32_t keyCount = 0;
        // 旧 pad[3] を Hue 回転（生存中のシームレス色変化）に転用（CB サイズ不変）。
        float    hueEnable      = 0.0f; // 0/1
        float    hueSpeed       = 0.0f; // 1秒あたりの回転数（1.0=毎秒1周）
        float    hueRandomPhase = 0.0f; // 0/1（粒子ごとに位相をばらす）
        Vector4  keyColor[kMaxGradientKeys] = {};
        Vector4  keyLoc[kMaxGradientKeys]   = {}; // .x に位置(0..1)。昇順
    };

    // 周回（orbit）。Update CS の b2。spin=帯上を流れる(法線軸) / tumble=帯自体の回転(別軸)。
    struct ParticleOrbit
    {
        float   enabled = 0.0f;
        float   spinSpeed = 0.0f;
        float   tumbleSpeed = 0.0f;
        float   radiusEndRatio = 1.0f; // 最終円の半径 / 生成時の円の半径
        Vector3 center = { 0.0f, 0.0f, 0.0f };
        float   pad1 = 0.0f;
        Vector3 spinAxis = { 0.0f, 0.0f, 1.0f };
        float   pad2 = 0.0f;
        Vector3 tumbleAxis = { 0.0f, 1.0f, 0.0f };
        float   pad3 = 0.0f;
        // 収束（移動をカーブで制御：spawn位置→convergeCenter）。enable で velocity/orbit より優先。
        float   convergeEnable = 0.0f; // 0/1
        // 周回中心が前回の Update から動いた量。旧 pad4..6 の位置（CB レイアウト不変）。
        // 粒子を中心と一緒に平行移動させ、エミッタが動いてもリングが崩れないようにする。
        Vector3 centerDelta = { 0.0f, 0.0f, 0.0f };
        Vector3 convergeCenter = { 0.0f, 0.0f, 0.0f };
        float   radiusEnable = 0.0f; // 0/1。周回半径を寿命に沿って変える
        // 移動カーブ LUT（32サンプル 0..1。4成分=4サンプル）。converge=収束カーブ / orbit=半径カーブ（排他なので共用）
        Vector4 motionLUT[8] = {};
    };

    struct PerFrame
    {
        float time;
        float deltaTime;
        float pad[2];
    };

    // ディゾルブ（描画 PS の b2、粒子ごとの寿命比率ベース）。GPUParticle.PS.hlsl と一致させること。
    struct DissolveParticle
    {
        int   enable = 0;       // 0=無効 / 1=有効
        int   inEnable = 0;     // 出現 0/1
        int   outEnable = 0;    // 消滅 0/1
        int   edgeEnable = 0;   // アウトライン 0/1
        float inEnd = 0.3f;     // 寿命比率: ここまでに出現完了
        float outStart = 0.7f;  // 寿命比率: ここから消え始める
        float edgeWidth = 0.05f;
        float pad0 = 0.0f;
        Vector4 edgeColor = { 1.0f, 0.4f, 0.1f, 1.0f };
    };

    // 1グループ分のリソース束
    struct GPUParticleGroup
    {
        // パーティクル本体（DEFAULT heap）
        Microsoft::WRL::ComPtr<ID3D12Resource> particleResource;
        uint32_t particleUavIndex = 0;
        uint32_t particleSrvIndex = 0;
        bool initializedOnGPU = false;

        // FreeList
        Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource;
        uint32_t freeListIndexUavIndex = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource;
        uint32_t freeListUavIndex = 0;

        // Emitter CB
        Microsoft::WRL::ComPtr<ID3D12Resource> emitterResource;
        EmitterSphere* emitterData = nullptr;

        // Gradient CB（Update CS b1。多色グラデーション）
        Microsoft::WRL::ComPtr<ID3D12Resource> gradientResource;
        ParticleGradient* gradientData = nullptr;

        // Orbit CB（Update CS b2。周回運動）
        Microsoft::WRL::ComPtr<ID3D12Resource> orbitResource;
        ParticleOrbit* orbitData = nullptr;
        Vector3 lastOrbitCenter = { 0.0f, 0.0f, 0.0f }; // centerDelta 算出用（前回 Update 時の center）
        bool    hasLastOrbitCenter = false;

        // PerFrame CB（TimeGroup によって dt が異なるため per-group）
        Microsoft::WRL::ComPtr<ID3D12Resource> perFrameResource;
        PerFrame* perFrameData = nullptr;

        // PerView CB（メイン用）
        Microsoft::WRL::ComPtr<ID3D12Resource> perViewResource;
        PerView* perViewData = nullptr;

        // PerView CB（水面の反射用。DrawReflection で鏡像の視点を書く）
        Microsoft::WRL::ComPtr<ID3D12Resource> perViewReflectionResource;
        PerView* perViewReflectionData = nullptr;
        bool reflect = true;

        // PerView CB（プレビュー用、同じ粒子をプレビュー RT に別カメラで描画する）
        Microsoft::WRL::ComPtr<ID3D12Resource> perViewPreviewResource;
        PerView* perViewPreviewData = nullptr;

        // テクスチャ
        std::string textureFilePath;
        uint32_t textureSrvIndex = 0;

        // ディゾルブ（描画 PS b2）＋マスク(t1)
        Microsoft::WRL::ComPtr<ID3D12Resource> dissolveResource;
        DissolveParticle* dissolveData = nullptr;
        std::string dissolveMaskPath;
        uint32_t dissolveMaskSrvIndex = 0;
        bool     hasDissolveMask = false;

        // Bloom（描画 PS b0 をグループ専用の強度入りマテリアルに差し替えて発光 RT へ描く）
        bool      bloomEnable = false;
        Microsoft::WRL::ComPtr<ID3D12Resource> bloomMaterialResource;
        Material* bloomMaterialData = nullptr;

        // 状態
        BlendMode     blendMode    = kBlendModeAdd; // 描画ブレンド（既定 Add＝後方互換）
        BillboardMode billboardMode = BillboardMode::Full;
        TimeGroup     timeGroup     = TimeGroup::World;
        bool          continuousEnabled = false;
        float         elapsedTime = 0.0f;

        // バースト要求（Update で CB.emit に転写）。
        // CPU 側で持つ理由：CB は persistent-mapped で、GPU が Emit CS を実行する前に CPU が書き換えるとレースになる。
        bool          pendingBurst = false;

        // プレビュー専用グループ（名前が kPreviewPrefix 付き）。シーン用とは
        // 更新経路（unscaled delta）も描画経路（プレビュー PerView）も分ける。
        bool          isPreview = false;
    };

    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;

    // 共通パイプライン
    Microsoft::WRL::ComPtr<ID3D12RootSignature> initRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> initPSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> emitRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> emitPSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> updateRootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> drawRootSig_;
    // 描画のルートシグネチャのルートパラメータの番号（CreateDrawPipeline と描画で同じ番号を使う）
    enum DrawRootParam : UINT {
        kDrawRootParticles,      // VS t0：粒子の StructuredBuffer（テーブル）
        kDrawRootPerView,        // VS b0：ビュー射影など
        kDrawRootMaterial,       // PS b0：マテリアル
        kDrawRootTexture,        // PS t0：テクスチャ（テーブル）
        kDrawRootDissolve,       // PS b2：ディゾルブ
        kDrawRootDissolveMask,   // PS t1：ディゾルブマスク（テーブル。未設定は white1x1）
        kDrawRootCount,
    };
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> drawPSOs_;
    // Bloom Pass 用（発光 RT に加算で描く。drawRootSig_ を共用）
    Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomPSO_;

    // 共通リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

    // ディゾルブマスク未設定時に t1 へバインドする既定テクスチャ（white1x1）
    uint32_t whiteSrvIndex_ = 0;

    // 描画時に必要な共通行列（毎フレーム算出）
    Matrix4x4 viewProjectionMatrix_ = {};
    Matrix4x4 fullBillboardMatrix_ = {};
    Vector3   cameraPosition_ = { 0.0f, 0.0f, 0.0f };

    // グループ群
    std::unordered_map<std::string, GPUParticleGroup> groups_;

    // 遅延解放待ちのプレビューグループ（フレーム途中の GPU 使用中バッファ解放を避けるため
    // RecyclePreviewGroups で一旦ここへ退避し、次フレーム頭の UpdatePreviewSim で実解放する）。
    std::vector<GPUParticleGroup> previewGroupPendingDelete_;

    // 内部ヘルパ
    /// <summary>初期化 CS（InitializeParticle.CS）のパイプラインを作る。ルート [0]=u0 粒子 / [1]=u1 FreeListIndex / [2]=u2 FreeList（すべてテーブル）</summary>
    void CreateInitializePipeline();
    /// <summary>発射 CS（EmitParticle.CS）のパイプラインを作る。ルート [0]=u0 粒子 / [1]=u1 FreeListIndex / [2]=u2 FreeList（テーブル）、[3]=b0 Emitter / [4]=b1 PerFrame（CBV）</summary>
    void CreateEmitPipeline();
    /// <summary>更新 CS（UpdateParticle.CS）のパイプラインを作る。ルート [0]=u0 粒子 / [1]=u1 FreeListIndex / [2]=u2 FreeList（テーブル）、[3]=b0 PerFrame / [4]=b1 Gradient / [5]=b2 Orbit（CBV）</summary>
    void CreateUpdatePipeline();
    /// <summary>
    /// 描画のルートシグネチャ・ブレンドモード別 PSO・Bloom 用 PSO（R16G16B16A16_FLOAT へ加算）を作る。
    /// ルートの並びは DrawRootParam（VS t0 粒子 SRV / VS b0 PerView / PS b0 Material / PS t0 テクスチャ /
    /// PS b2 Dissolve / PS t1 ディゾルブマスク）、s0=リニア・ラップ
    /// </summary>
    void CreateDrawPipeline();
    /// <summary>原点中心・1×1 の板（2 三角形・6 頂点）の頂点バッファを作る</summary>
    void CreateVertexData();
    /// <summary>描画 PS b0 に渡す既定マテリアル（白・ライティングなし）を作り、未設定マスク用の white1x1 を読む</summary>
    void CreateMaterial();

    /// <summary>1 グループ分の粒子・FreeList バッファ（UAV/SRV）と各 CB を作ってマップし、既定値を入れる</summary>
    /// <param name="g">作る先のグループ</param>
    /// <param name="texturePath">粒子のテクスチャのパス</param>
    void CreateGroupResources(GPUParticleGroup& g, const std::string& texturePath);
    /// <summary>1 グループ分の CB をアンマップして解放し、確保した SRV/UAV スロットを返す（GPU 使用中の解放を避けるため呼び出し側で遅延させる）</summary>
    /// <param name="g">解放するグループ</param>
    void ReleaseGroupResources(GPUParticleGroup& g);

    /// <summary>初期化 CS を 1 グループ分ディスパッチする（粒子と FreeList を初期状態にする）</summary>
    /// <param name="g">対象のグループ</param>
    void DispatchInitializeCS(GPUParticleGroup& g);
    /// <summary>発射 CS を 1 グループ分ディスパッチする（emit フラグが立っていれば Emitter の数だけ生成）</summary>
    /// <param name="g">対象のグループ</param>
    void DispatchEmitCS(GPUParticleGroup& g);
    /// <summary>更新 CS を 1 グループ分ディスパッチする（移動・周回・色・寿命切れの回収）</summary>
    /// <param name="g">対象のグループ</param>
    void DispatchUpdateCS(GPUParticleGroup& g);

    /// <summary>1グループ分の emit フラグ確定 + PerFrame(dt) 書き込み（Update / UpdatePreviewSim 共用）。周回中心の移動量もここで出す</summary>
    /// <param name="g">対象のグループ</param>
    /// <param name="dt">進める時間 [秒]</param>
    void UpdateGroupSim(GPUParticleGroup& g, float dt);
    /// <summary>1グループを Init/Emit/Update CS でシミュレートし、指定 PerView CB で描画（Draw / DrawPreview 共用）</summary>
    /// <param name="g">対象のグループ</param>
    /// <param name="perViewCB">VS b0 に渡す PerView（メイン用かプレビュー用）</param>
    void SimulateAndDrawGroup(GPUParticleGroup& g, ID3D12Resource* perViewCB);
    /// <summary>1グループを Bloom PSO で発光 RT へ描く（シミュレーション済みの粒子をそのまま使う。未初期化・Bloom 無効なら描かない）</summary>
    /// <param name="g">対象のグループ</param>
    /// <param name="perViewCB">VS b0 に渡す PerView（メイン用かプレビュー用）</param>
    void DrawGroupBloom(GPUParticleGroup& g, ID3D12Resource* perViewCB);

    /// <summary>粒子バッファのリソース状態を遷移させるバリアを積む</summary>
    /// <param name="g">対象のグループ</param>
    /// <param name="before">今の状態</param>
    /// <param name="after">遷移後の状態</param>
    void TransitionParticle(GPUParticleGroup& g, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
};
