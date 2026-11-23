#pragma once

#include <string>

#include "IImGuiEditable.h"
#include "Vector3.h"
#include "EffectDef.h"   // EffectCurve（緩急カーブ。Effect と共用）

/// <summary>
/// レールカメラの「向き」キーフレーム。位置はスプライン、向きはこのキー列で決める。
/// 軽量 IImGuiEditable（NoAutoRegister で Hierarchy には登録しない。選択は Tuning パネルの
/// リスト経由で ImGuiManager->SetSelected(key) する）。
///
/// 回転はオイラー角（ラジアン, pitch/yaw/roll）で保存する。補間時のみ Quaternion 化して Slerp。
/// </summary>
class CameraRotKey : public IImGuiEditable {
public:
    /// <summary>Hierarchy・自動登録に載せない軽量なキーとして作る</summary>
    CameraRotKey() : IImGuiEditable(NoAutoRegister{}) {}
    ~CameraRotKey() override = default;

    //====================
    // データ
    //====================
    float       t = 0.0f;               // progress(0..1) 上の位置
    Vector3     rotate{ 0.0f, 0.0f, 0.0f }; // 向き（オイラー角ラジアン pitch/yaw/roll）
    EffectCurve easeToNext;             // 次キーへ向かう緩急（0..1→0..1 再マップ）

    //====================
    // IImGuiEditable
    //====================
    /// <summary>表示名を取得する</summary>
    /// <returns>名前。未設定なら "RotKey t=0.00" の形</returns>
    std::string GetName() const override;
    /// <summary>表示名を設定する</summary>
    /// <param name="n">名前</param>
    void        SetName(const std::string& n) override { name_ = n; }
    /// <summary>型名を取得する</summary>
    /// <returns>"CameraRotKey"</returns>
    std::string GetTypeName() const override { return "CameraRotKey"; }
    /// <summary>t・向き（度で編集）・緩急カーブを Inspector に出す</summary>
    void        OnImGuiInspector() override;

private:
    std::string name_;
};
