#pragma once
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <d3d12.h>
#include <wrl.h>

#include "Gizmo.h"
#include "SpriteGizmo.h"
#include "IEditorSelection.h"

// 前方宣言
class IImGuiWindow;
class IImGuiEditable;
class DirectXCore;
class SRVManager;
class FPSWindow;
class LogWindow;
class ViewportWindow;
class EffectEditorWindow;
class RenderTexture;
class Camera;
class GPUParticleManager;
class Scene;
class PostEffect;
class Framework;
struct HWND__;
typedef HWND__* HWND;

/// <summary>
/// エディタ核（エンジン）がホストアプリの実体へ触るための注入フック（依存性の逆転）。
/// エンジンは SceneManager / Game といった具象を名指しせず、ホスト側が起動時に配線する。
/// 未配線のものは nullptr のままでよく、その場合はパネルが「無効」表示になるだけ。
/// </summary>
struct EditorHostHooks {
    /// 現在アクティブなシーン（無ければ nullptr）
    Scene* (*getActiveScene)() = nullptr;
    /// 現在アクティブなシーンの表示名（無ければ nullptr）
    const char* (*getActiveSceneName)() = nullptr;
    /// ホストが保持する PostEffect（無ければ nullptr）
    PostEffect* (*getPostEffect)() = nullptr;
    /// ホストの Framework 実体（ShadowMap 等のエンジン機能へ辿るため）
    Framework* (*getFramework)() = nullptr;

    //====================
    // エンティティのグループ（Hierarchy のグルーピングと Inspector の切替に使う）
    //
    // エンジンは「0 以上の整数」としてしか扱わない。ゲームのタグ enum を
    // int にキャストして渡すのが典型。未配線なら「1 グループのみ」として動く。
    //====================
    /// グループの総数
    int (*getEntityGroupCount)() = nullptr;
    /// エンティティが属するグループ番号
    int (*getEntityGroup)(IImGuiEditable* e) = nullptr;
    /// グループの表示名
    const char* (*getEntityGroupName)(int group) = nullptr;
    /// グループの色（Hierarchy のヘッダ着色）
    void (*getEntityGroupColor)(int group, float& r, float& g, float& b, float& a) = nullptr;
    /// エンティティのグループを変更する（Inspector のコンボから呼ばれる）
    void (*setEntityGroup)(IImGuiEditable* e, int group) = nullptr;

    /// <summary>
    /// Inspector で編集するコライダーの実体。
    /// ホストが独自のコンポーネント表にコライダーを持つ場合に配線する。
    /// 未配線なら CollisionSystem のサイドテーブルを編集する。
    /// </summary>
    struct Collider* (*getCollider)(IImGuiEditable* e) = nullptr;
};

/// <summary>
/// ImGui管理クラス（シングルトン）
/// </summary>
class ImGuiManager : public IEditorSelection {
public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static ImGuiManager& Instance();

    /// <summary>
    /// ホストフックを配線する。Initialize より前でも後でもよい（Draw 時にしか参照しない）。
    /// </summary>
    static void SetHostHooks(const EditorHostHooks& hooks);

    /// <summary>アクティブシーン取得（フック未配線なら nullptr）。</summary>
    Scene* GetActiveScene() const;

    /// <summary>アクティブシーン名取得（フック未配線なら nullptr）。</summary>
    const char* GetActiveSceneName() const;

    /// <summary>ホストの PostEffect 取得（フック未配線なら nullptr）。</summary>
    PostEffect* GetHostPostEffect() const;

    /// <summary>ホストの Framework 取得（フック未配線なら nullptr）。</summary>
    Framework* GetHostFramework() const;

    //====================
    // エンティティのグループ（未配線時は「グループ 0 のみ・名前 "All"・灰色」で動く）
    //====================
    int GetEntityGroupCount() const;
    int GetEntityGroup(IImGuiEditable* e) const;
    const char* GetEntityGroupName(int group) const;
    void GetEntityGroupColor(int group, float& r, float& g, float& b, float& a) const;
    void SetEntityGroup(IImGuiEditable* e, int group);

    /// <summary>
    /// Inspector が編集するコライダー。フック未配線なら CollisionSystem のものを返す。
    /// </summary>
    struct Collider* GetEntityCollider(IImGuiEditable* e) const;

    /// <summary>
    /// Inspector にホスト固有のセクションを追加する。
    /// エンジンは共通部（名前 / 型 / グループ / 表示 / コライダー）だけを持ち、
    /// ゲーム固有のコンポーネント欄はここから差し込む。
    ///
    /// 描画関数は選択中エンティティを受け取り、登録順に呼ばれる。
    /// CollapsingHeader などの見出しは登録側が自前で出すこと（エンジンは包まない）。
    /// </summary>
    void AddInspectorSection(const std::string& name,
        std::function<void(IImGuiEditable*)> draw);

    /// <summary>登録済み Inspector セクション（InspectorWindow が描画に使う）。</summary>
    struct InspectorSection {
        std::string name;
        std::function<void(IImGuiEditable*)> draw;
    };
    const std::vector<InspectorSection>& GetInspectorSections() const { return inspectorSections_; }

    /// <summary>
    /// アセットブラウザ（SceneEditor）にホスト固有のセクションを追加する。
    /// エンジンは Resources/ を走査して並べる汎用ブラウザだけを持ち、
    /// プレハブ一覧やゲーム固有の配置ボタンはここから差し込む。
    /// アセット一覧より前、シーン保存/読込の直後に、登録順で呼ばれる。
    /// 見出しは登録側が自前で出すこと。
    /// </summary>
    void AddAssetBrowserSection(const std::string& name, std::function<void()> draw);

    /// <summary>登録済みアセットブラウザセクション（SceneEditorWindow が描画に使う）。</summary>
    struct AssetBrowserSection {
        std::string name;
        std::function<void()> draw;
    };
    const std::vector<AssetBrowserSection>& GetAssetBrowserSections() const { return assetBrowserSections_; }

    /// <summary>
    /// ホスト側のウィンドウを追加登録する。Initialize 後に呼ぶこと。
    /// 所有権は ImGuiManager が持つ。
    /// </summary>
    void AddWindow(std::unique_ptr<IImGuiWindow> window);

    /// <summary>
    /// 描画コールバックだけのウィンドウを追加登録する（AddWindow の簡易版）。
    /// </summary>
    void AddCallbackWindow(const std::string& name, std::function<void()> draw);

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="hwnd">ウィンドウハンドル</param>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
    /// <param name="srvManager">SRVManagerへのポインタ</param>
    void Initialize(HWND hwnd, DirectXCore* dxCore, SRVManager* srvManager);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Shutdown();

    /// <summary>
    /// フレーム開始
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// フレーム終了（描画）
    /// </summary>
    void EndFrame();

    /// <summary>
    /// 編集可能オブジェクトを登録
    /// </summary>
    void Register(IImGuiEditable* editable);

    /// <summary>
    /// 編集可能オブジェクトを登録解除
    /// </summary>
    void Unregister(IImGuiEditable* editable);

    /// <summary>
    /// 選択オブジェクトを設定
    /// </summary>
    void SetSelected(IImGuiEditable* editable) override { selectedObject_ = editable; }

    /// <summary>
    /// 選択オブジェクトを取得
    /// </summary>
    IImGuiEditable* GetSelected() const override { return selectedObject_; }

    /// <summary>
    /// 登録済みオブジェクト一覧を取得
    /// </summary>
    const std::vector<IImGuiEditable*>& GetEditables() const { return editables_; }

    /// <summary>
    /// 初期化済みかどうか
    /// </summary>
    bool IsInitialized() const { return isInitialized_; }

    /// <summary>
    /// ビューポートのRenderTextureをセット
    /// </summary>
    void SetViewportRenderTexture(RenderTexture* renderTexture);

    /// <summary>
    /// ギズモ用にアクティブなカメラをセット
    /// </summary>
    void SetCamera(Camera* camera) { camera_ = camera; }

    /// <summary>
    /// デバッグUIで操作するGPUParticleManagerをセット（無いシーンではnullptr）
    /// </summary>
    void SetGPUParticleManager(GPUParticleManager* manager) { gpuParticleManager_ = manager; }

private:
    ImGuiManager() = default;
    ~ImGuiManager() = default;
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    /// <summary>
    /// メニューバーを描画
    /// </summary>
    void DrawMenuBar();

    // ウィンドウ一覧
    std::vector<std::unique_ptr<IImGuiWindow>> windows_;

    // ビューポートウィンドウ（参照用）
    ViewportWindow* viewportWindow_ = nullptr;

    // Effect Editor ウィンドウ（参照用、所有はwindows_）
    EffectEditorWindow* effectEditorWindow_ = nullptr;

public:
    /// <summary>
    /// Effect Editor ウィンドウ取得（Render パスを Scene から呼び出すために使う）
    /// </summary>
    EffectEditorWindow* GetEffectEditorWindow() { return effectEditorWindow_; }

    /// <summary>
    /// Scene（メイン Viewport）ウィンドウ取得。マウスホバー判定等に使う。
    /// </summary>
    ViewportWindow* GetViewportWindow() { return viewportWindow_; }

private:

    // 編集可能オブジェクト一覧
    std::vector<IImGuiEditable*> editables_;

    // 選択中のオブジェクト
    IImGuiEditable* selectedObject_ = nullptr;

    // DirectX関連
    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;
    uint32_t srvIndex_ = 0;

    // ギズモ
    Gizmo gizmo_;
    SpriteGizmo spriteGizmo_;
public:
    const Gizmo& GetGizmo() const { return gizmo_; }
private:
    Camera* camera_ = nullptr;

    // シーン保有のGPUParticleManagerへの参照（非所有）
    GPUParticleManager* gpuParticleManager_ = nullptr;

    // ホスト（ゲーム/エディタアプリ）から注入されたフック
    static EditorHostHooks hostHooks_;

    // ホストが追加した Inspector / アセットブラウザのセクション
    std::vector<InspectorSection> inspectorSections_;
    std::vector<AssetBrowserSection> assetBrowserSections_;

    bool isInitialized_ = false;
};
