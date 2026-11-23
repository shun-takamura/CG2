#pragma once

struct EffectCurve;

namespace EffectUI {

    /// <summary>
    /// アニメーションカーブのグラフ編集UI。横軸=正規化時間(0..1)、縦軸=出力(0..1)。
    /// 左ドラッグで点移動／右クリックで中間点削除／Add Pointで追加／Snapでグリッド吸着。
    /// 端点(最初/最後)は x を 0/1 に固定し y のみ可動。
    ///
    /// Effect の各カーブ編集と、レールカメラの緩急カーブ編集で共用する。
    /// </summary>
    /// <param name="id">ImGui の ID（同じ画面に複数置くときに区別する）</param>
    /// <param name="curve">編集するカーブ</param>
    /// <returns>有効フラグや点を変えたら true</returns>
    bool DrawCurveEditor(const char* id, EffectCurve& curve);

} // namespace EffectUI
