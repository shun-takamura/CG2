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
    std::string GetName() const override;
    void        SetName(const std::string& n) override { name_ = n; }
    std::string GetTypeName() const override { return "CameraRotKey"; }
    void        OnImGuiInspector() override;

private:
    std::string name_;
};
