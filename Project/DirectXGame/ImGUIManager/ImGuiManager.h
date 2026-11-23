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
    /// ホストの Framework 実体（ShadowMap 等のエンジン機能へ辿るため）。
    /// ホストが自分自身（this）を渡せるよう、これだけは状態を持てる std::function にしてある
    std::function<Framework*()> getFramework;

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

    /// <summary>
    /// TimeControler の「Scene Switch」欄の中身（シーンの切り替え先・トランジションの選択）。
    /// シーンの一覧と切り替えはホストのシーン管理が持つので、エンジンは呼ぶだけ。未配線なら欄を出さない。
    /// </summary>
    void (*drawSceneSwitcher)() = nullptr;
};

/// <summary>
/// ImGui管理クラス（シングルトン）
/// シングルトンにする理由：ImGui のコンテキストはプロセスに 1 つで、各ウィンドウとフックを 1 か所にまとめるため。
/// </summary>
class ImGuiManager : public IEditorSelection {
public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns>インスタンス</returns>
    static ImGuiManager* GetInstance();

    /// <summary>
    /// ホストフックを配線する。Initialize より前でも後でもよい（Draw 時にしか参照しない）。
    /// </summary>
    /// <param name="hooks">ホストのフック（関数ポインタは nullptr 可）</param>
    static void SetHostHooks(const EditorHostHooks& hooks);

    /// <summary>アクティブシーン取得（フック未配線なら nullptr）。</summary>
    /// <returns>シーン</returns>
    Scene* GetActiveScene() const;

    /// <summary>アクティブシーン名取得（フック未配線なら nullptr）。</summary>
    /// <returns>シーン名</returns>
    const char* GetActiveSceneName() const;

    /// <summary>ホストの PostEffect 取得（フック未配線なら nullptr）。</summary>
    /// <returns>PostEffect</returns>
    PostEffect* GetHostPostEffect() const;

    /// <summary>ホストの Framework 取得（フック未配線なら nullptr）。</summary>
    /// <returns>Framework</returns>
    Framework* GetHostFramework() const;

    //====================
    // エンティティのグループ（未配線時は「グループ 0 のみ・名前 "All"・灰色」で動く）
    //====================
    /// <summary>グループの総数</summary>
    /// <returns>総数（未配線・0 以下なら 1）</returns>
    int GetEntityGroupCount() const;
    /// <summary>エンティティが属するグループ番号</summary>
    /// <param name="e">エンティティ</param>
    /// <returns>グループ番号（未配線・nullptr・範囲外なら 0）</returns>
    int GetEntityGroup(IImGuiEditable* e) const;
    /// <summary>グループの表示名</summary>
    /// <param name="group">グループ番号</param>
    /// <returns>表示名（未配線なら "All"、空なら "(unnamed)"）</returns>
    const char* GetEntityGroupName(int group) const;
    /// <summary>グループの色（Hierarchy のヘッダ着色。未配線なら灰色）</summary>
    /// <param name="group">グループ番号</param>
    /// <param name="r">赤を書く先（0〜1）</param>
    /// <param name="g">緑を書く先（0〜1）</param>
    /// <param name="b">青を書く先（0〜1）</param>
    /// <param name="a">不透明度を書く先（0〜1）</param>
    void GetEntityGroupColor(int group, float& r, float& g, float& b, float& a) const;
    /// <summary>エンティティのグループを変える（未配線・nullptr なら何もしない）</summary>
    /// <param name="e">エンティティ</param>
    /// <param name="group">グループ番号</param>
    void SetEntityGroup(IImGuiEditable* e, int group);

    /// <summary>
    /// Inspector が編集するコライダー。フック未配線なら CollisionSystem のものを返す。
    /// </summary>
    /// <param name="e">エンティティ</param>
    /// <returns>コライダー（e が nullptr なら nullptr）</returns>
    struct Collider* GetEntityCollider(IImGuiEditable* e) const;

    /// <summary>
    /// Inspector にホスト固有のセクションを追加する。
    /// エンジンは共通部（名前 / 型 / グループ / 表示 / コライダー）だけを持ち、
    /// ゲーム固有のコンポーネント欄はここから差し込む。
    ///
    /// 描画関数は選択中エンティティを受け取り、登録順に呼ばれる。
    /// CollapsingHeader などの見出しは登録側が自前で出すこと（エンジンは包まない）。
    /// </summary>
    /// <param name="name">セクション名</param>
    /// <param name="draw">選択中のエンティティを受け取って描く関数</param>
    void AddInspectorSection(const std::string& name,
        std::function<void(IImGuiEditable*)> draw);

    /// <summary>登録済み Inspector セクション（InspectorWindow が描画に使う）。</summary>
    struct InspectorSection {
        std::string name;
        std::function<void(IImGuiEditable*)> draw;
    };
    /// <summary>登録済み Inspector セクション</summary>
    /// <returns>登録順の一覧</returns>
    const std::vector<InspectorSection>& GetInspectorSections() const { return inspectorSections_; }

    /// <summary>
    /// アセットブラウザ（SceneEditor）にホスト固有のセクションを追加する。
    /// エンジンは Resources/ を走査して並べる汎用ブラウザだけを持ち、
    /// プレハブ一覧やゲーム固有の配置ボタンはここから差し込む。
    /// アセット一覧より前、シーン保存/読込の直後に、登録順で呼ばれる。
    /// 見出しは登録側が自前で出すこと。
    /// </summary>
    /// <param name="name">セクション名</param>
    /// <param name="draw">描く関数</param>
    void AddAssetBrowserSection(const std::string& name, std::function<void()> draw);

    /// <summary>登録済みアセットブラウザセクション（SceneEditorWindow が描画に使う）。</summary>
    struct AssetBrowserSection {
        std::string name;
        std::function<void()> draw;
    };
    /// <summary>登録済みアセットブラウザセクション</summary>
    /// <returns>登録順の一覧</returns>
    const std::vector<AssetBrowserSection>& GetAssetBrowserSections() const { return assetBrowserSections_; }

    /// <summary>
    /// ホスト側のウィンドウを追加登録する。Initialize 後に呼ぶこと。
    /// 所有権は ImGuiManager が持つ。
    /// </summary>
    /// <param name="window">追加するウィンドウ</param>
    void AddWindow(std::unique_ptr<IImGuiWindow> window);

    /// <summary>
    /// 描画コールバックだけのウィンドウを追加登録する（AddWindow の簡易版）。
    /// </summary>
    /// <param name="name">ウィンドウ名</param>
    /// <param name="draw">ウィンドウの中身を描く関数</param>
    void AddCallbackWindow(const std::string& name, std::function<void()> draw);

    /// <summary>
    /// ImGui（Win32 / DX12・Docking）を初期化し、標準のウィンドウ群を作る（_DEBUG の時だけ）
    /// </summary>
    /// <param name="hwnd">ウィンドウハンドル</param>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
    /// <param name="srvManager">SRVManagerへのポインタ</param>
    void Initialize(HWND hwnd, DirectXCore* dxCore, SRVManager* srvManager);

    /// <summary>
    /// ウィンドウ・登録・セクションを捨てて ImGui を終了する（初期化していなければ何もしない）
    /// </summary>
    void Shutdown();

    /// <summary>
    /// ImGui の新しいフレームを始める（毎フレーム、シーン更新の前に呼ぶ）
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// DockSpace・メニューバー・全ウィンドウ・選択中オブジェクトのギズモを描き、ImGui の描画をコマンドリストに積む
    /// </summary>
    void EndFrame();

    /// <summary>
    /// 編集可能オブジェクトを登録する（nullptr・登録済みは無視）
    /// </summary>
    /// <param name="editable">登録するオブジェクト</param>
    void Register(IImGuiEditable* editable);

    /// <summary>
    /// 編集可能オブジェクトを登録解除する（選択中なら選択も外す）
    /// </summary>
    /// <param name="editable">外すオブジェクト</param>
    void Unregister(IImGuiEditable* editable);

    /// <summary>
    /// 選択オブジェクトを設定する
    /// </summary>
    /// <param name="editable">選択するオブジェクト（nullptr で選択解除）</param>
    void SetSelected(IImGuiEditable* editable) override { selectedObject_ = editable; }

    /// <summary>
    /// 選択オブジェクトを取得する
    /// </summary>
    /// <returns>選択中のオブジェクト（無ければ nullptr）</returns>
    IImGuiEditable* GetSelected() const override { return selectedObject_; }

    /// <summary>
    /// 登録済みオブジェクト一覧を取得する
    /// </summary>
    /// <returns>登録順の一覧</returns>
    const std::vector<IImGuiEditable*>& GetEditables() const { return editables_; }

    /// <summary>
    /// 初期化済みかどうか
    /// </summary>
    /// <returns>初期化済みなら true（Debug 以外は常に false）</returns>
    bool IsInitialized() const { return isInitialized_; }

    /// <summary>
    /// ビューポートに映す RenderTexture をセットする
    /// </summary>
    /// <param name="renderTexture">ゲーム画像</param>
    void SetViewportRenderTexture(RenderTexture* renderTexture);

    /// <summary>
    /// ギズモ用にアクティブなカメラをセット
    /// </summary>
    /// <param name="camera">シーンのカメラ</param>
    void SetCamera(Camera* camera) { camera_ = camera; }

    /// <summary>
    /// デバッグUIで操作するGPUParticleManagerをセット（無いシーンではnullptr）
    /// </summary>
    /// <param name="manager">GPUParticleManager</param>
    void SetGPUParticleManager(GPUParticleManager* manager) { gpuParticleManager_ = manager; }

private:
    ImGuiManager() = default;
    ~ImGuiManager() = default;
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    /// <summary>
    /// メインメニューバーを描く（各ウィンドウの表示切り替えと、アセットの読み込み方式 FS / pack の切り替え）
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
    /// <returns>EffectEditor ウィンドウ（Debug 以外は nullptr）</returns>
    EffectEditorWindow* GetEffectEditorWindow() { return effectEditorWindow_; }

    /// <summary>
    /// Scene（メイン Viewport）ウィンドウ取得。マウスホバー判定等に使う。
    /// </summary>
    /// <returns>ビューポートウィンドウ（Debug 以外は nullptr）</returns>
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
    /// <summary>3D ギズモ（ドラッグ中かを調べる用）</summary>
    /// <returns>ギズモ</returns>
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
