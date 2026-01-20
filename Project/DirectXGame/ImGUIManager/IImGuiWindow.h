#pragma once
#include <string>
#include "imgui.h"

/// <summary>
/// ImGuiウィンドウの基底クラス
/// 派生クラスはOnDraw()を実装する
/// </summary>
class IImGuiWindow {
public:
    /// <summary>
    /// コンストラクタ
    /// </summary>
    /// <param name="name">ウィンドウ名</param>
    IImGuiWindow(const std::string& name) 
        : name_(name), isOpen_(true) {}

    /// <summary>
    /// デストラクタ
    /// </summary>
    virtual ~IImGuiWindow() = default;

    /// <summary>
    /// 描画処理（共通）
    /// </summary>
    void Draw() {
        if (!isOpen_) return;

        if (ImGui::Begin(name_.c_str(), &isOpen_)) {
            OnDraw();
        }
        ImGui::End();
    }

    /// <summary>
    /// ウィンドウ名を取得
    /// </summary>
    const std::string& GetName() const { return name_; }

    /// <summary>
    /// ウィンドウが開いているか
    /// </summary>
    bool IsOpen() const { return isOpen_; }

    /// <summary>
    /// ウィンドウの表示/非表示を設定
    /// </summary>
    void SetOpen(bool open) { isOpen_ = open; }

protected:
    /// <summary>
    /// 派生クラスが実装する描画処理
    /// </summary>
    virtual void OnDraw() = 0;

    std::string name_;
    bool isOpen_;
};
