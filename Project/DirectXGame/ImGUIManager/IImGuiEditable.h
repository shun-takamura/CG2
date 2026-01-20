#pragma once
#include <string>

// 前方宣言
class ImGuiManager;

/// <summary>
/// ImGuiで編集可能なオブジェクトのインターフェース
/// このクラスを継承すると、自動的にImGuiManagerに登録される
/// </summary>
class IImGuiEditable {
public:
    /// <summary>
    /// コンストラクタ（自動登録）
    /// </summary>
    IImGuiEditable();

    /// <summary>
    /// デストラクタ（自動登録解除）
    /// </summary>
    virtual ~IImGuiEditable();

    /// <summary>
    /// オブジェクト名を取得
    /// </summary>
    virtual std::string GetName() const = 0;

    /// <summary>
    /// 型名を取得（例: "Object3D", "Sprite"）
    /// </summary>
    virtual std::string GetTypeName() const = 0;

    /// <summary>
    /// Inspectorでの編集UIを描画
    /// </summary>
    virtual void OnImGuiInspector() = 0;

    // コピー禁止
    IImGuiEditable(const IImGuiEditable&) = delete;
    IImGuiEditable& operator=(const IImGuiEditable&) = delete;
};
