#pragma once
#include <cstdint>
#include <string>
#include <vector>

class InputManager;

/// <summary>
/// 決定論リプレイの記録/再生を一元管理する（シングルトン）。
/// S.U.N.D.A.Y. が異常を検知したとき、同一シード＋記録した dt 列＋入力で
/// エンジン内再生して再現性を確認するための土台。
///
/// 記録は1フレーム1行で input.log（SessionLogger の Input カテゴリ）へ出力する。
/// 生デバイス状態（キー/スティック/トリガー/ボタン/マウス）を丸ごと記録するため、
/// 移動が生キー直読みでも忠実に再現できる。
/// 再生は input.log を読み、ハードを読まずに各デバイスへ状態を注入する。
/// </summary>
class ReplaySystem {
public:
    enum class Mode {
        Record,  // 通常プレイ：dt と入力を記録
        Replay,  // 再生：記録から dt と入力を供給
    };

    static ReplaySystem& Instance();

    /// <summary>
    /// Record モードで初期化する。seed はステップ3で確定した使用シード。
    /// </summary>
    void InitializeRecord(uint32_t seed);

    /// <summary>
    /// Replay モードで初期化する。dir は記録時のセッションフォルダ
    /// （例: "Logs/2026-06-11_212430"）。input.log を読み込み、
    /// session.log から記録時シードも取り出す。
    /// </summary>
    void InitializeReplay(const std::string& dir);

    Mode GetMode() const { return mode_; }

    /// <summary>
    /// 記録時シードが session.log から取得できたか。
    /// </summary>
    bool HasLoadedSeed() const { return hasLoadedSeed_; }
    uint32_t GetLoadedSeed() const { return loadedSeed_; }

    /// <summary>
    /// 1フレーム分の dt と現在の入力状態を input.log に記録する（Record モード）。
    /// Framework::Update の末尾で呼ぶ。
    /// </summary>
    void RecordFrame(float dt, InputManager* input);

    /// <summary>
    /// 次フレームの記録を取り出し、入力を各デバイスへ注入する（Replay モード）。
    /// outDt に記録した dt を返す。記録を再生し終えたら false（=リプレイ終了）。
    /// Framework::Update の先頭、シーン更新より前に呼ぶ。
    /// </summary>
    bool AdvanceReplay(InputManager* input, float& outDt);

private:
    ReplaySystem() = default;
    ~ReplaySystem() = default;
    ReplaySystem(const ReplaySystem&) = delete;
    ReplaySystem& operator=(const ReplaySystem&) = delete;

    // 1フレーム分の入力スナップショット（生デバイス状態）
    struct FrameRecord {
        float    dt = 0.0f;
        uint8_t  keys[256] = {};                 // DIK 押下状態（0x80=押下）
        int32_t  mdx = 0, mdy = 0, mwheel = 0;   // マウス移動量
        int      mbtn = 0;                       // マウスボタンマスク
        bool     padConnected = false;
        int16_t  lx = 0, ly = 0, rx = 0, ry = 0; // スティック生値
        uint8_t  lt = 0, rt = 0;                 // トリガー生値
        uint16_t btns = 0;                       // ボタンビット
    };

    Mode     mode_ = Mode::Record;
    uint64_t frame_ = 0;          // Record 用の出力フレーム番号
    uint32_t seed_ = 0;

    // Replay 用
    std::vector<FrameRecord> records_;
    size_t   replayIndex_ = 0;
    bool     hasLoadedSeed_ = false;
    uint32_t loadedSeed_ = 0;

    bool active_ = false;
};
