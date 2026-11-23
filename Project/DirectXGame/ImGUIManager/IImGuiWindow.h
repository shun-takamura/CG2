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
    /// 名前を決めて開いた状態で作る
    /// </summary>
    /// <param name="name">ウィンドウ名</param>
    IImGuiWindow(const std::string& name) 
        : name_(name), isOpen_(true) {}

    virtual ~IImGuiWindow() = default;

    /// <summary>
    /// 開いていればウィンドウを出して OnDraw を呼ぶ（_DEBUG の時だけ。閉じるボタンで isOpen_ が false になる）
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
    /// 初めて出す時のウィンドウの大きさを設定する（ImGui の ini に記録があればそちらが優先）
    /// </summary>
    /// <param name="size">大きさ [px]（0 以下なら指定しない）</param>
    void SetInitialSize(const ImVec2& size) { initialSize_ = size; }

    /// <summary>
    /// ウィンドウ名（ImGui の ID を兼ねる）
    /// </summary>
    /// <returns>ウィンドウ名</returns>
    const std::string& GetName() const { return name_; }

    /// <summary>
    /// ウィンドウが開いているか
    /// </summary>
    /// <returns>開いていれば true</returns>
    bool IsOpen() const { return isOpen_; }

    /// <summary>
    /// ウィンドウの表示/非表示を設定する
    /// </summary>
    /// <param name="open">表示するなら true</param>
    void SetOpen(bool open) { isOpen_ = open; }

protected:
    /// <summary>
    /// ウィンドウの中身を描く。派生クラスが実装する（Begin / End は Draw が済ませている）
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
    /// <summary>名前と、ウィンドウの中身を描く関数を受け取る</summary>
    /// <param name="name">ウィンドウ名</param>
    /// <param name="draw">中身を描く関数（空なら何も描かない）</param>
    CallbackWindow(const std::string& name, std::function<void()> draw)
        : IImGuiWindow(name), draw_(std::move(draw)) {}

protected:
    /// <summary>受け取った関数で中身を描く</summary>
    void OnDraw() override {
        if (draw_) draw_();
    }

private:
    std::function<void()> draw_;
};
