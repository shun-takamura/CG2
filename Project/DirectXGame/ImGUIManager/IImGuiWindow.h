#pragma once
#include <string>
#include <functional>
#include <utility>
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

#ifdef _DEBUG

        if (!isOpen_) return;

        // 初回起動時のサイズを指定
        if (initialSize_.x > 0 && initialSize_.y > 0) {
            ImGui::SetNextWindowSize(initialSize_, ImGuiCond_FirstUseEver);
        }

        if (ImGui::Begin(name_.c_str(), &isOpen_)) {
            OnDraw();
        }
        ImGui::End();

#endif // DEBUG
    }

    /// <summary>
    /// 初期サイズを設定（FirstUseEver）
    /// </summary>
    void SetInitialSize(const ImVec2& size) { initialSize_ = size; }

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
    ImVec2 initialSize_ = ImVec2(0, 0);
};

/// <summary>
/// 任意の関数オブジェクトを描画コールバックとして受け取る汎用ウィンドウ
/// </summary>
class CallbackWindow : public IImGuiWindow {
public:
    CallbackWindow(const std::string& name, std::function<void()> draw)
        : IImGuiWindow(name), draw_(std::move(draw)) {}

protected:
    void OnDraw() override {
        if (draw_) draw_();
    }

private:
    std::function<void()> draw_;
};
