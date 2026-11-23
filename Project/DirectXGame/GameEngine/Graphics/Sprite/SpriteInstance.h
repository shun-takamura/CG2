#pragma once
#include "Vector3.h"
#include "Vector2.h"
#include "SpriteManager.h"
#include "SpriteMaterialData.h" 
#include "SpriteVertexData.h"
#include "Transform.h"
#include "TransformationMatrix.h"
#include "TextureManager.h"
#include "ConvertString.h"

#ifdef USE_IMGUI
#include "IImGuiEditable.h"
#endif

class SpriteManager;

class SpriteInstance
#ifdef USE_IMGUI
    : public IImGuiEditable
#endif
{
    std::string name_;
    std::string textureFilePath_;

    Vector2 position_ = { 0.0f, 0.0f };
    Vector2 anchorPoint_ = { 0.0f, 0.0f };

    bool isFlipX_ = false;
    bool isFlipY_ = false;

    Vector2 textureLeftTop_ = { 0.0f, 0.0f };
    Vector2 textureSize_ = { 100.0f, 100.0f };

    /// <summary>切り出し範囲とスプライトの大きさを、テクスチャの元の大きさに合わせる</summary>
    void AdjustTextureSize();

    float rotation_ = 0.0f;
    Vector2 size_ = { 320.0f, 180.0f };

    Transform transform{
        {1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}
    };

    SpriteManager* spriteManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;

    // バッファ内データへのCPU側ポインタ ← 型変更
    SpriteVertexData* vertexData_ = nullptr;     // ← VertexData から変更
    uint32_t* indexData_ = nullptr;

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW  indexBufferView_{};

    // マテリアル用リソース ← 型変更
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    SpriteMaterialData* materialData_ = nullptr;  // ← Material から変更

    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_ = nullptr;
    TransformationMatrix* transformationMatrixData_ = nullptr;

    D3D12_GPU_DESCRIPTOR_HANDLE textureGpuHandle_{};
    int textureNum_ = 0;
    uint32_t textureIndex_ = 0;

    /// <summary>4 頂点の頂点バッファと 6 インデックスのインデックスバッファを作ってマップし、ビューを設定する</summary>
    void CreateVertexBuffer();
    /// <summary>マテリアル（色・UV 変換）の定数バッファを作ってマップし、白・単位行列を入れる</summary>
    void CreateMaterialBuffer();
    /// <summary>座標変換行列（World・WVP）の定数バッファを作ってマップし、単位行列を入れる</summary>
    void CreateTransformationMatrixBuffer();

public:
    SpriteInstance() = default;
#ifdef USE_IMGUI
    /// <summary>マップしていた頂点・インデックス・マテリアル・行列のバッファをアンマップするデストラクタ</summary>
    ~SpriteInstance() override;
#else 
    /// <summary>マップしていた頂点・インデックス・マテリアル・行列のバッファをアンマップするデストラクタ</summary>
    ~SpriteInstance();
#endif

    // 名前は実行時の RemoveDynamicSprite 等でも使うので常に提供する
    // （USE_IMGUI 時は IImGuiEditable の override となる）
#ifdef USE_IMGUI
    /// <summary>スプライトの名前を返す</summary>
    /// <returns>名前</returns>
    std::string GetName() const override { return name_; }
    /// <summary>エディタに出す種類名を返す</summary>
    /// <returns>"Sprite"</returns>
    std::string GetTypeName() const override { return "Sprite"; }
    /// <summary>位置・大きさ・回転[度]・アンカー・UV・反転・色・テクスチャ情報をインスペクタに表示・編集する</summary>
    void OnImGuiInspector() override;
    /// <summary>エディタのギズモで動かす 2D 位置を返す</summary>
    /// <returns>位置へのポインタ</returns>
    Vector2* GetEditable2DPosition() override { return &position_; }
#else
    /// <summary>スプライトの名前を返す</summary>
    /// <returns>名前</returns>
    std::string GetName() const { return name_; }
#endif

    /// <summary>
    /// テクスチャをロードし、切り出し範囲をテクスチャ全体にして、頂点・マテリアル・行列のバッファを作る
    /// </summary>
    /// <param name="spriteManager">共通のルートシグネチャ・PSO を持つ SpriteManager</param>
    /// <param name="filePath">テクスチャのパス</param>
    /// <param name="name">名前（空ならファイル名を使う）</param>
    void Initialize(SpriteManager* spriteManager, const std::string& filePath,
        const std::string& name = "");
    /// <summary>
    /// アンカー・反転・切り出し範囲から 4 頂点を作り直し、大きさ・回転・位置と UI 解像度の正射影から World・WVP を書き込む
    /// </summary>
    void Update();
    /// <summary>
    /// SpriteManager のルートシグネチャで [0]=マテリアル(PS b0)、[1]=行列(VS b0)、[2]=テクスチャ(t0) をバインドして描く。
    /// 先に SpriteManager::DrawSetting を呼んでおくこと。Debug でエディタ上非表示なら描かない
    /// </summary>
    void Draw();

    // ゲッター
    /// <summary>位置を返す</summary>
    /// <returns>UI 座標 [px]</returns>
    const Vector2& GetPosition() const { return position_; }
    /// <summary>アンカーポイントを返す</summary>
    /// <returns>アンカー（0〜1。(0,0)=左上）</returns>
    const Vector2& GetAnchorPoint() const { return anchorPoint_; }
    /// <summary>回転を返す</summary>
    /// <returns>回転 [ラジアン]</returns>
    const float& GetRotation() const { return rotation_; }
    /// <summary>色を返す</summary>
    /// <returns>色（RGBA）</returns>
    const Vector4& GetColor() const { return materialData_->color; }
    /// <summary>表示サイズを返す</summary>
    /// <returns>サイズ [px]</returns>
    const Vector2& GetSize() const { return size_; }
    /// <summary>左右反転するかを返す</summary>
    /// <returns>反転するなら true</returns>
    const bool& GetIsFlipX() const { return isFlipX_; }
    /// <summary>上下反転するかを返す</summary>
    /// <returns>反転するなら true</returns>
    const bool& GetIsFlipY() const { return isFlipY_; }
    /// <summary>テクスチャの切り出し範囲の左上を返す</summary>
    /// <returns>テクスチャ上の座標 [px]</returns>
    const Vector2& GetTextureLeftTop() const { return textureLeftTop_; }
    /// <summary>テクスチャの切り出し範囲の大きさを返す</summary>
    /// <returns>テクスチャ上の大きさ [px]</returns>
    const Vector2& GetTextureSize() const { return textureSize_; }
    /// <summary>テクスチャのパスを返す</summary>
    /// <returns>パス</returns>
    const std::string& GetTextureFilePath() const { return textureFilePath_; }

    // セッター
    /// <summary>名前を設定する</summary>
    /// <param name="name">名前</param>
    void SetName(const std::string& name) override { name_ = name; }
    /// <summary>位置を設定する</summary>
    /// <param name="position">UI 座標 [px]</param>
    void SetPosition(const Vector2& position) { position_ = position; }
    /// <summary>アンカーポイントを設定する</summary>
    /// <param name="anchorPoint">アンカー（0〜1。(0,0)=左上、(0.5,0.5)=中央）</param>
    void SetAnchorPoint(const Vector2& anchorPoint) { anchorPoint_ = anchorPoint; }
    /// <summary>回転を設定する</summary>
    /// <param name="rotation">回転 [ラジアン]</param>
    void SetRotation(const float& rotation) { rotation_ = rotation; }
    /// <summary>色を設定する（マテリアルに直接書く）</summary>
    /// <param name="color">色（RGBA）</param>
    void SetColor(const Vector4& color) { materialData_->color = color; }
    /// <summary>表示サイズを設定する</summary>
    /// <param name="size">サイズ [px]</param>
    void SetSize(const Vector2& size) { size_ = size; }
    /// <summary>左右反転するかを設定する</summary>
    /// <param name="isFlipX">反転するなら true</param>
    void SetIsFlipX(const bool& isFlipX) { isFlipX_ = isFlipX; }
    /// <summary>上下反転するかを設定する</summary>
    /// <param name="isFlipY">反転するなら true</param>
    void SetIsFlipY(const bool& isFlipY) { isFlipY_ = isFlipY; }
    /// <summary>テクスチャの切り出し範囲の左上を設定する</summary>
    /// <param name="textureLeftTop">テクスチャ上の座標 [px]</param>
    void SetTextureLeftTop(const Vector2& textureLeftTop) { textureLeftTop_ = textureLeftTop; }
    /// <summary>テクスチャの切り出し範囲の大きさを設定する</summary>
    /// <param name="textureSize">テクスチャ上の大きさ [px]</param>
    void SetTextureSize(const Vector2& textureSize) { textureSize_ = textureSize; }

    /// <summary>
    /// 実行時にテクスチャを差し替える（パス指定）。サイズ等はそのまま維持する。
    /// 未ロードなら TextureManager に読み込ませる。Draw はパスから SRV を引くため即反映される。
    /// </summary>
    /// <param name="filePath">テクスチャのパス（空や今と同じなら何もしない）</param>
    void SetTexture(const std::string& filePath);
};