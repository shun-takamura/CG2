#pragma once
#include "Object3DManager.h"
#include "VertexData.h"
#include <fstream>
#include <sstream>
#include "Material.h"
#include <cassert>
#include "DirectXCore.h"
#include "MathUtility.h"
#include "Quaternion.h"
#include "TransformationMatrix.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include "TextureManager.h"
#include "Transform.h"
#include "ModelCore.h"
#include "AnimatedModelInstance.h"
#include "Camera.h"
#include "CameraForGPU.h"
#include "Skeleton.h"
#include "Animation.h"
#include <memory>
#include "SkinCluster.h"
#include "TimeGroup.h"
#include <vector>

// ImGui対応
#include "IImGuiEditable.h"

class Object3DManager;
class PrimitiveMesh;  // unique_ptrで持つので前方宣言でOK
struct SkinCluster;
class SkinningComputeManager;
class SRVManager;

class AnimatedObject3DInstance : public IImGuiEditable {

    //==============================
    // メンバ変数
    //==============================
    std::string name_;

    Object3DManager* object3DManager_ = nullptr;
    AnimatedModelInstance* animatedModelInstance_ = nullptr;
    Camera* camera_ = nullptr;

    SkinningComputeManager* skinningComputeManager_ = nullptr;
    SRVManager* srvManager_ = nullptr;  // Drawで使うので保持

    // バッファリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;

    TransformationMatrix* transformationMatrixData_ = nullptr;
    CameraForGPU* cameraData_ = nullptr;

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Transform transform_;
    Transform cameraTransform_;
    // Update で求めたワールド行列（CPU 側の控え。転送用の CB は書き込み専用メモリなので読まない）
    Matrix4x4 worldMatrix_ = MakeIdentity4x4();
    // hasWorldOverride_ が true の間は transform_ ではなく worldOverride_ を使う（ボーンのソケットに付ける物など）
    bool hasWorldOverride_ = false;
    Matrix4x4 worldOverride_ = MakeIdentity4x4();
    Vector3 emissive_ = { 0.0f, 0.0f, 0.0f };   // SetEmissive の控え（GPU の Material は書き込み専用なので読まない）

    std::string textureFilePath_;
    std::string modelFileName_;
    std::string directoryPath_;  // ロード元 dirPath（シーンJSON保存用。明示的に SetSourcePath で設定）

    //==============================
    // アニメーション関連メンバ変数
    //==============================
    float animationTime_ = 0.0f;       // 現在の再生時刻（秒）
    bool isPlaying_ = true;            // 再生中フラグ
    bool visible_   = true;            // 描画フラグ（false で Draw をスキップ。全構成で有効）
    TimeGroup timeGroup_ = TimeGroup::World;  // Scene::UpdateDynamicAnimated で進める時間（既定はシーンのワールド）
    bool isLoop_ = true;               // ループ再生フラグ
    float playbackSpeed_ = 1.0f;       // 再生速度（1.0が等倍）

    // ----- クロスフェード状態 -----
    // PlayAnimation で旧クリップを prev に snapshot し、fadeDuration_ 秒かけて新クリップへ補間する。
    Animation prevAnimation_{};
    float prevAnimationTime_ = 0.0f;
    bool  hasPrevAnimation_ = false;
    // 旧クリップを切り替えた時のループ・再生速度。ループしないクリップはフェードの間も最後のコマで止めておく
    // （頭へ戻すと、終わった回避・攻撃の出だしが一瞬混ざって見える）
    bool  prevLoop_ = true;
    float prevPlaybackSpeed_ = 1.0f;
    float fadeTimer_ = 0.0f;           // フェード経過秒
    float fadeDuration_ = 0.0f;        // 0 ならフェード未進行
    float defaultFadeTime_ = 0.25f;    // .anim ドロップ時のデフォルト

    //==============================
    // Skeleton関連
    //==============================
    Skeleton skeleton_;
    bool hasSkeleton_ = false;

    SkinCluster skinCluster_;
    bool hasSkinCluster_ = false;
    /// <summary>スキニング用に確保した 4 つの SRV/UAV 番号を SRVManager へ返す（返さないと SRV の枠が尽きる）</summary>
    void FreeSkinClusterDescriptors();

    // このフレームで既にスキニングをDispatch済みか。Update でリセットし、
    // DispatchSkinning で立てる。シャドウパス先行Dispatchとメイン描画の二重実行を防ぐ。
    bool skinningDispatchedThisFrame_ = false;

    // 部位ごとに、ディゾルブの縁を発光パスで光らせるか（SetDissolveSphere で立てる。DrawBloomPass が見る）
    std::vector<uint8_t> dissolveSubmeshes_;
    /// <summary>textureKeyword に合う部位のマテリアルに write を当て、dissolveSubmeshes_ に印を付ける（SetDissolveSphere / Plane / Noise の共通部分）</summary>
    /// <param name="textureKeyword">ベースカラーのテクスチャのパスに含む語（空なら全部の部位）</param>
    /// <param name="write">Material を書き換える関数</param>
    template <class Write>
    void ApplyDissolveToSubmeshes(const std::string& textureKeyword, Write&& write);

#ifdef USE_IMGUI
    // デバッグ描画用
    bool showSkeleton_ = false;
    Vector4 jointSphereColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 jointLineColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float   jointSphereRadius_ = 0.025f;
    std::vector<std::unique_ptr<PrimitiveMesh>> debugSpheres_;
    /// <summary>debugSpheres_ を関節の数だけ用意する（骨の表示を付けた最初のフレームに作る）</summary>
    void EnsureDebugSpheres();
#endif

    //==============================
    // 内部関数
    //==============================
    /// <summary>変換行列 CB（Object3DManager の VS b0。256 バイト境界）を作ってマップする</summary>
    /// <param name="dxCore">デバイスの取得元</param>
    void CreateTransformationMatrixResource(DirectXCore* dxCore);
    /// <summary>カメラ CB（Object3DManager の PS b2）を作ってマップする</summary>
    /// <param name="dxCore">デバイスの取得元</param>
    void CreateCameraResource(DirectXCore* dxCore);

public:
    AnimatedObject3DInstance() = default;
    /// <summary>スキニング用の SRV/UAV 番号を返す</summary>
    ~AnimatedObject3DInstance() override;

    //==============================
    // IImGuiEditable実装
    //==============================
    /// <summary>表示名を返す</summary>
    /// <returns>表示名</returns>
    std::string GetName() const override { return name_; }
    /// <summary>型名を返す</summary>
    /// <returns>"AnimatedObject3D"</returns>
    std::string GetTypeName() const override { return "AnimatedObject3D"; }
    /// <summary>Inspector に Transform・アニメーション・マテリアル・骨の表示の編集 UI を描く</summary>
    void OnImGuiInspector() override;
    /// <summary>ギズモで動かす位置を返す</summary>
    /// <returns>translate へのポインタ</returns>
    Vector3* GetEditableTranslate() override { return &transform_.translate; }
    /// <summary>ギズモ表示用の回転を返す</summary>
    /// <returns>rotate へのポインタ</returns>
    const Vector3* GetEditableRotate() const override { return &transform_.rotate; }

    //==============================
    // セッター
    //==============================
    /// <summary>表示名を設定する</summary>
    /// <param name="name">表示名</param>
    void SetName(const std::string& name) override { name_ = name; }
    /// <summary>描くモデルを差し替える（骨・SkinCluster は作り直さない）</summary>
    /// <param name="animatedModel">モデル</param>
    void SetModel(AnimatedModelInstance* animatedModel) { animatedModelInstance_ = animatedModel; }
    /// <summary>WVP とカメラ CB に使うカメラを設定する</summary>
    /// <param name="camera">カメラ</param>
    void SetCamera(Camera* camera) { camera_ = camera; }
    /// <summary>描画するか設定する（false で Draw 系をスキップ。全構成で有効）</summary>
    /// <param name="v">描くなら true</param>
    void SetVisible(bool v) { visible_ = v; }
    /// <summary>描画するか</summary>
    /// <returns>描くなら true</returns>
    bool GetVisible() const { return visible_; }
    /// <summary>拡大率を設定する</summary>
    /// <param name="scale">軸ごとの拡大率</param>
    void SetScale(const Vector3& scale) { transform_.scale = scale; }
    /// <summary>向きを設定する</summary>
    /// <param name="rotate">オイラー角 [ラジアン]</param>
    void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }
    /// <summary>位置を設定する</summary>
    /// <param name="translate">ワールド座標</param>
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }
    /// <summary>ワールド行列を直接与える（Object3DInstance と同じ。BoneSocket::World() をそのまま渡す等）。解除するまで transform_ は使わない</summary>
    /// <param name="m">ワールド行列</param>
    void SetWorldMatrixOverride(const Matrix4x4& m) { worldOverride_ = m; hasWorldOverride_ = true; }
    /// <summary>ワールド行列の直接指定をやめて transform_ に戻す</summary>
    void ClearWorldMatrixOverride() { hasWorldOverride_ = false; }

    // Material関連
    /// <summary>環境マップを使うかを全部の部位のマテリアルに書く</summary>
    /// <param name="use">使うなら true</param>
    void SetUseEnvironmentMap(bool use);
    /// <summary>環境マップの映り込みの強さを全部の部位のマテリアルに書く</summary>
    /// <param name="coef">係数</param>
    void SetEnvironmentCoefficient(float coef);

    /// <summary>
    /// マテリアルカラー（GPU側 Material::color）を直接書き換える。
    /// 被弾点滅やヒット演出など、ランタイムでティント色を変えたい時に使う。
    /// SetModel 前は no-op。alpha < 1.0f で半透明になる。
    /// </summary>
    /// <param name="color">RGBA（全部の部位に書く）</param>
    void SetMaterialColor(const Vector4& color);

    /// <summary>
    /// 現在のマテリアルカラーを取得（SetModel 前は白を返す）。
    /// </summary>
    /// <returns>先頭のマテリアルの色。モデルやマテリアルが無ければ白</returns>
    Vector4 GetMaterialColor() const;

    /// <summary>
    /// 発光（Material::emissive の rgb。ライティングの後に足す）を全部の部位に書く。被弾の点滅など、
    /// 色を掛けると暗いテクスチャでは色が出ないときに使う。ブルームには載らない（発光パスは描かない）
    /// </summary>
    /// <param name="emissive">発光の RGB</param>
    void SetEmissive(const Vector3& emissive);
    /// <summary>PBR の金属度・粗さを全部の部位に書く（.mat の値を上書きする）</summary>
    /// <param name="metallic">金属度（0〜1）</param>
    /// <param name="roughness">粗さ（0〜1）</param>
    void SetMetallicRoughness(float metallic, float roughness);
    /// <summary>最後に SetEmissive した値（既定 0）</summary>
    /// <returns>発光の RGB</returns>
    const Vector3& GetEmissive() const { return emissive_; }

    /// <summary>
    /// 点からの距離のディゾルブ（Material.h の DissolveSphere。胸の装甲に入口を開ける・羽根を付け根から出す等）を
    /// 部位にかける。textureKeyword が空なら全部の部位、空でなければベースカラーのテクスチャのパスにその語を含む部位だけ
    /// （例 "Chest_"）。縁はディゾルブをかけた部位を DrawBloomPass で発光 RT にも描くと光る。毎フレーム呼んでよい
    /// </summary>
    /// <param name="dissolve">点からの距離のディゾルブの設定</param>
    /// <param name="textureKeyword">かける部位のテクスチャのパスに含む語（空なら全部）</param>
    void SetDissolveSphere(const DissolveSphere& dissolve, const std::string& textureKeyword = "");
    /// <summary>
    /// 高さの面のディゾルブ（Material.h の DissolvePlane、kDissolveRise）を部位にかける。面より下が消え、面を上げると下から上へほどける
    /// （死亡演出のエーテル化）。textureKeyword・縁の光らせ方・毎フレーム呼んでよいのは SetDissolveSphere と同じ
    /// </summary>
    /// <param name="dissolve">高さの面のディゾルブの設定</param>
    /// <param name="textureKeyword">かける部位のテクスチャのパスに含む語（空なら全部）</param>
    void SetDissolvePlane(const DissolvePlane& dissolve, const std::string& textureKeyword = "");
    /// <summary>
    /// ノイズだけのディゾルブ（Material.h の DissolveNoise）を部位にかける。場所の順番なしにまだらに欠ける（羽根の焼け落ち）。
    /// textureKeyword・縁の光らせ方・毎フレーム呼んでよいのは SetDissolveSphere と同じ
    /// </summary>
    /// <param name="dissolve">ノイズのディゾルブの設定</param>
    /// <param name="textureKeyword">かける部位のテクスチャのパスに含む語（空なら全部）</param>
    void SetDissolveNoise(const DissolveNoise& dissolve, const std::string& textureKeyword = "");
    /// <summary>ディゾルブを全部の部位で切る</summary>
    void ClearDissolve();

    //==============================
    // アニメーション制御
    //==============================
    /// <summary>再生を始める（時刻はそのまま）</summary>
    void Play() { isPlaying_ = true; }
    /// <summary>一時停止する</summary>
    void Pause() { isPlaying_ = false; }
    /// <summary>停止して時刻を 0 に戻す</summary>
    void Stop() { isPlaying_ = false; animationTime_ = 0.0f; }
    /// <summary>再生時刻を設定する</summary>
    /// <param name="time">時刻 [秒]</param>
    void SetAnimationTime(float time) { animationTime_ = time; }
    /// <summary>再生時刻を返す</summary>
    /// <returns>時刻 [秒]</returns>
    float GetAnimationTime() const { return animationTime_; }
    /// <summary>ループ再生するか設定する</summary>
    /// <param name="loop">ループするなら true</param>
    void SetLoop(bool loop) { isLoop_ = loop; }
    /// <summary>ループ再生するか</summary>
    /// <returns>ループするなら true</returns>
    bool GetLoop() const { return isLoop_; }
    /// <summary>再生速度を設定する</summary>
    /// <param name="speed">倍率（1.0 が等倍）</param>
    void SetPlaybackSpeed(float speed) { playbackSpeed_ = speed; }
    /// <summary>Scene::UpdateDynamicAnimated でどの時間で進めるかを設定する（自機は Player など）</summary>
    /// <param name="group">時間のグループ（既定 World）</param>
    void SetTimeGroup(TimeGroup group) { timeGroup_ = group; }
    /// <summary>Scene::UpdateDynamicAnimated で進める時間のグループ</summary>
    /// <returns>グループ</returns>
    TimeGroup GetTimeGroup() const { return timeGroup_; }
    /// <summary>再生速度を返す</summary>
    /// <returns>倍率</returns>
    float GetPlaybackSpeed() const { return playbackSpeed_; }
    /// <summary>再生中か（ループしないクリップは終端で false になる）</summary>
    /// <returns>再生中なら true</returns>
    bool IsPlaying() const { return isPlaying_; }

    /// <summary>
    /// 別の .anim へクロスフェード付きで切り替える。
    /// fadeTime > 0 のとき、現在のアニメーションを snapshot して fadeTime 秒かけて補間。
    /// fadeTime = 0 のときは即座に差し替え。
    /// </summary>
    /// <param name="animPath">.anim のパス（空なら何もしない）</param>
    /// <param name="fadeTime">クロスフェードの時間 [秒]</param>
    void PlayAnimation(const std::string& animPath, float fadeTime);

    /// <summary>クロスフェード中か</summary>
    /// <returns>フェード中なら true</returns>
    bool IsFading() const { return hasPrevAnimation_; }
    /// <summary>.anim ドロップ時のフェード時間を返す</summary>
    /// <returns>時間 [秒]</returns>
    float GetDefaultFadeTime() const { return defaultFadeTime_; }
    /// <summary>.anim ドロップ時のフェード時間を設定する</summary>
    /// <param name="t">時間 [秒]（負なら 0）</param>
    void  SetDefaultFadeTime(float t) { defaultFadeTime_ = (t < 0.0f) ? 0.0f : t; }

    //==============================
    // ゲッター
    //==============================
    /// <summary>拡大率を返す</summary>
    /// <returns>拡大率</returns>
    const Vector3& GetScale() const { return transform_.scale; }
    /// <summary>向きを返す</summary>
    /// <returns>オイラー角 [ラジアン]</returns>
    const Vector3& GetRotate() const { return transform_.rotate; }
    /// <summary>位置を返す</summary>
    /// <returns>ワールド座標</returns>
    const Vector3& GetTranslate() const { return transform_.translate; }
    /// <summary>テクスチャのパスを返す</summary>
    /// <returns>パス</returns>
    const std::string& GetTextureFilePath() const { return textureFilePath_; }
    /// <summary>モデルのファイル名を返す（シーン JSON 保存用）</summary>
    /// <returns>ファイル名</returns>
    const std::string& GetModelFileName() const { return modelFileName_; }
    /// <summary>モデルのディレクトリを返す（シーン JSON 保存用）</summary>
    /// <returns>ディレクトリ</returns>
    const std::string& GetDirectoryPath() const { return directoryPath_; }
    /// <summary>シーン JSON 保存用にロード元を覚える</summary>
    /// <param name="dirPath">ディレクトリ</param>
    /// <param name="filename">ファイル名</param>
    void SetSourcePath(const std::string& dirPath, const std::string& filename) {
        directoryPath_ = dirPath;
        modelFileName_ = filename;
    }

    //==============================
    // ボーン（ソケット）アクセス
    //==============================
    /// <summary>骨を持っているか</summary>
    /// <returns>持っていれば true</returns>
    bool HasSkeleton() const { return hasSkeleton_; }
    /// <summary>骨を返す</summary>
    /// <returns>Skeleton</returns>
    const Skeleton& GetSkeleton() const { return skeleton_; }

    /// <summary>
    /// 指定ジョイントのワールド行列を返す（武器追従・エフェクト発生点に使う）。
    /// skeletonSpaceMatrix × モデルのworldMatrix。スケルトン未生成や名前不一致なら
    /// モデル本体の worldMatrix をそのまま返す。Update 後に呼ぶこと。
    /// </summary>
    /// <param name="jointName">ジョイント名</param>
    /// <returns>ジョイントのワールド行列</returns>
    Matrix4x4 GetJointWorldMatrix(const std::string& jointName) const {
        Matrix4x4 world = hasWorldOverride_ ? worldOverride_ : MakeAffineMatrix(transform_);
        if (!hasSkeleton_) return world;
        auto it = skeleton_.jointMap.find(jointName);
        if (it == skeleton_.jointMap.end()) return world;
        return Multiply(skeleton_.joints[it->second].skeletonSpaceMatrix, world);
    }

    /// <summary>
    /// ソケット用：ジョイントのワールド行列からスケールを除去（基底ベクトルを正規化）し、
    /// 回転＋位置のみを返す。mixamo の Armature に焼き込まれた cm→m スケール等を取り除き、
    /// アタッチした武器/エフェクトが極小に潰れるのを防ぐ。サイズはアタッチ側オフセットで決める。
    /// </summary>
    /// <param name="jointName">ジョイント名</param>
    /// <returns>スケールを除いたワールド行列</returns>
    Matrix4x4 GetJointSocketMatrix(const std::string& jointName) const {
        const Matrix4x4 w = GetJointWorldMatrix(jointName);
        const Vector3 bx = Normalize(Vector3{ w.m[0][0], w.m[0][1], w.m[0][2] });
        const Vector3 by = Normalize(Vector3{ w.m[1][0], w.m[1][1], w.m[1][2] });
        const Vector3 bz = Normalize(Vector3{ w.m[2][0], w.m[2][1], w.m[2][2] });
        return Matrix4x4{ {
            { bx.x, bx.y, bx.z, 0.0f },
            { by.x, by.y, by.z, 0.0f },
            { bz.x, bz.y, bz.z, 0.0f },
            { w.m[3][0], w.m[3][1], w.m[3][2], 1.0f },
        } };
    }

    //==============================
    // 初期化・更新・描画
    //==============================
    /// <summary>CB を作り、モデルがあれば骨と SkinCluster を作る（作り直し時は前の SRV 番号を返してから）</summary>
    /// <param name="object3DManager">描画のルートシグネチャ・PSO の取得元</param>
    /// <param name="skinningComputeManager">スキニング CS の設定元</param>
    /// <param name="dxCore">デバイスの取得元</param>
    /// <param name="srvManager">SkinCluster の SRV/UAV を確保する先</param>
    /// <param name="animatedModel">モデル</param>
    /// <param name="name">表示名（空なら "AnimatedObject"）</param>
    void Initialize(Object3DManager* object3DManager,
        SkinningComputeManager* skinningComputeManager,
        DirectXCore* dxCore,
        SRVManager* srvManager,
        AnimatedModelInstance* animatedModel,
        const std::string& name = "");

    /// <summary>アニメーション時刻とクロスフェードを進めて骨・パレットを更新し、変換行列を書く（スキニング済みの印も下ろす）</summary>
    /// <param name="deltaTime">経過時間 [秒]（可変フレーム対応）</param>
    void Update(float deltaTime);  // 引数にdeltaTimeを追加（可変フレーム対応）

    /// <summary>
    /// ComputeShader版でSkinning計算をDispatchする（Drawの前に呼ぶ。1 フレーム 1 回だけ）。
    /// ルート [0]=パレット / [1]=入力頂点 / [2]=影響度 / [3]=出力頂点 UAV / [4]=SkinningInformation
    /// </summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    void DispatchSkinning(DirectXCore* dxCore);

    /// <summary>
    /// スキニングを済ませ、Object3DManager のルートに切り替えて描く。
    /// [1]=VS b0 変換 / [4]=PS b2 カメラ / [7]=PS t1 環境マップ、フォグ・シャドウ・雲・ハイトマップも貼り直す
    /// </summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    void Draw(DirectXCore* dxCore);

    /// <summary>ID Pass 用（Skinning 済み VBV を再利用、Object3DManager の ID PSO を使用。[0]=VS b0 変換 / [1]=PS b0 ID のルート定数）</summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    void DrawIdPass(DirectXCore* dxCore);

    /// <summary>
    /// シャドウパス用：transform を b0 にバインドし、Skinning 済み VBV で深度を書く。
    /// スキニングのDispatchは事前に済んでいる前提（DispatchDynamicAnimatedSkinning）。
    /// </summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    void DrawShadowPass(DirectXCore* dxCore);

    /// <summary>
    /// 発光パス（Object3DInstance::DrawBloomPass のスキニング版）：発光している部位と、ディゾルブをかけている部位
    /// （縁が光る）だけを発光 RT に描く。メイン描画（スキニングの Dispatch）の後に呼ぶ
    /// </summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    /// <param name="bloomScale">発光の倍率（発光パスのルート [2]=PS b1 のルート定数）</param>
    void DrawBloomPass(DirectXCore* dxCore, float bloomScale);

    /// <summary>
    /// 水面の反射 RT へ描く（WaterReflection::Render から呼ばれる）。Object3DInstance::DrawReflection と同じく
    /// 自分の World を使い回し、鏡像の ViewProj（b1）とカメラ（b2）は鏡側の CB を挿す。
    /// スキニングは CS 済みの頂点を使うので、反射用の VS は静的モデルと共通。
    /// </summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    /// <param name="reflectionViewProjAddress">鏡像の ViewProj とクリップ平面の CB（ルート [12]=VS b1）</param>
    /// <param name="reflectionCameraAddress">鏡像のカメラの CB（ルート [4]=PS b2）</param>
    void DrawReflection(DirectXCore* dxCore,
        D3D12_GPU_VIRTUAL_ADDRESS reflectionViewProjAddress,
        D3D12_GPU_VIRTUAL_ADDRESS reflectionCameraAddress);

    /// <summary>
    /// ワールド空間の包む球。バインドポーズの球に余白を足す（腕を伸ばす等で頂点が外に出るため）。
    /// モデルが無い・球が分からない時は false（呼び出し側は常に見える扱いにする）。
    /// </summary>
    /// <param name="center">ワールドの中心の書き込み先</param>
    /// <param name="radius">ワールドの半径の書き込み先</param>
    /// <returns>求められたら true</returns>
    bool GetWorldBoundingSphere(Vector3& center, float& radius) const;

#ifdef USE_IMGUI
    /// <summary>Skeletonのデバッグ描画（関節に球、親子に線。全モデル描画後に呼ぶ）</summary>
    /// <param name="dxCore">未使用</param>
    void DrawSkeletonDebug(DirectXCore* dxCore);
#endif
};