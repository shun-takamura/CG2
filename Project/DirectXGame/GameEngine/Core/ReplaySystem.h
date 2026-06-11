#pragma once
#include <cstdint>

class InputManager;

/// <summary>
/// 決定論リプレイの記録/再生を一元管理する（シングルトン）。
/// S.U.N.D.A.Y. が異常を検知したとき、同一シード＋記録した dt 列＋入力で
/// エンジン内再生して再現性を確認するための土台。
///
/// このステップでは Record（記録）のみ実装。Replay（再生注入）は次段で追加する。
/// 記録は1フレーム1行で input.log（SessionLogger の Input カテゴリ）へ出力する。
/// 生デバイス状態（キー/スティック/トリガー/ボタン/マウス）を丸ごと記録するため、
/// 移動が生キー直読みでも忠実に再現できる。
/// </summary>
class ReplaySystem {
public:
    enum class Mode {
        Record,  // 通常プレイ：dt と入力を記録
        Replay,  // 再生：記録から dt と入力を供給（次段で実装）
    };

    static ReplaySystem& Instance();

    /// <summary>
    /// Record モードで初期化する。seed はステップ3で確定した使用シード。
    /// Framework::Initialize から1回呼ぶ。
    /// </summary>
    void InitializeRecord(uint32_t seed);

    Mode GetMode() const { return mode_; }

    /// <summary>
    /// 1フレーム分の dt と現在の入力状態を input.log に記録する。
    /// Framework::Update の末尾（入力・シーン更新後、dt 確定済み）で呼ぶ。
    /// </summary>
    void RecordFrame(float dt, InputManager* input);

private:
    ReplaySystem() = default;
    ~ReplaySystem() = default;
    ReplaySystem(const ReplaySystem&) = delete;
    ReplaySystem& operator=(const ReplaySystem&) = delete;

    Mode     mode_ = Mode::Record;
    uint64_t frame_ = 0;
    uint32_t seed_ = 0;
    bool     active_ = false;
};
