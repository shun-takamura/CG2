#pragma once
#include <string>
#include "Vector2.h"
#include "Vector3.h"

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

    /// <summary>
    /// 3Dギズモ操作用：Translateへのポインタを返す（nullptrならギズモ非対応）
    /// 3Dオブジェクトはオーバーライドして transform_.translate のアドレスを返す
    /// </summary>
    virtual Vector3* GetEditableTranslate() { return nullptr; }

    /// <summary>
    /// 2Dギズモ操作用：スクリーン座標(Vector2)へのポインタを返す（nullptrなら非対応）
    /// Spriteなどはオーバーライドして position_ のアドレスを返す
    /// </summary>
    virtual Vector2* GetEditable2DPosition() { return nullptr; }

    // コピー禁止
    IImGuiEditable(const IImGuiEditable&) = delete;
    IImGuiEditable& operator=(const IImGuiEditable&) = delete;
};
