#pragma once

struct EffectCurve;

namespace EffectUI {

    /// <summary>
    /// アニメーションカーブのグラフ編集UI。横軸=正規化時間(0..1)、縦軸=出力(0..1)。
    /// 左ドラッグで点移動／右クリックで中間点削除／Add Pointで追加／Snapでグリッド吸着。
    /// 端点(最初/最後)は x を 0/1 に固定し y のみ可動。dirty(変更あり) を返す。
    ///
    /// Effect の各カーブ編集と、レールカメラの緩急カーブ編集で共用する。
    /// </summary>
    bool DrawCurveEditor(const char* id, EffectCurve& curve);

} // namespace EffectUI
