#pragma once
#include <vector>
#include <memory>
#include <d3d12.h>
#include <wrl.h>

// 前方宣言
class IImGuiWindow;
class IImGuiEditable;
class DirectXCore;
class SRVManager;
class FPSWindow;
class LogWindow;
class HierarchyWindow;
class InspectorWindow;
struct HWND__;
typedef HWND__* HWND;

/// <summary>
/// ImGui管理クラス（シングルトン）
/// </summary>
class ImGuiManager {
public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static ImGuiManager& Instance();

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
    void SetSelected(IImGuiEditable* editable) { selectedObject_ = editable; }

    /// <summary>
    /// 選択オブジェクトを取得
    /// </summary>
    IImGuiEditable* GetSelected() const { return selectedObject_; }

    /// <summary>
    /// 登録済みオブジェクト一覧を取得
    /// </summary>
    const std::vector<IImGuiEditable*>& GetEditables() const { return editables_; }

    /// <summary>
    /// 初期化済みかどうか
    /// </summary>
    bool IsInitialized() const { return isInitialized_; }

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

    // 編集可能オブジェクト一覧
    std::vector<IImGuiEditable*> editables_;

    // 選択中のオブジェクト
    IImGuiEditable* selectedObject_ = nullptr;

    // DirectX関連
    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;
    uint32_t srvIndex_ = 0;

    bool isInitialized_ = false;
};
