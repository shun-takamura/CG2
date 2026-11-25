#pragma once
#include <cstdint>
#include <random>

/// <summary>
/// 乱数を集約する中央ジェネレータ（シングルトン）。
/// 起動時のシード 1 つから生成器を 2 本作り、シードを固定/記録することで
/// S.U.N.D.A.Y. のリプレイ（同一シードからの再現）を可能にする。
///   ゲーム用 : 敵・ボスの行動、報酬の抽選、トランジションの選択など、ゲームの結果に効く乱数。
///   見た目用 : パーティクル・エフェクト・破片の飛び方など、見た目だけの乱数。
/// 2 本に分けるのは、見た目の乱数を使う量（Debug と Release で描く量の違い、エディタのプレビューなど）が
/// ゲーム用の乱数の並びをずらさないようにするため。どちらも同じシードから作るので、同じシードなら両方とも同じ並びになる。
/// 自分の生成器を持ちたいクラス（ボスの抽選など）は NextSeed / NextVisualSeed で種だけもらう。
///
/// 注意: 決定性のためメインスレッド逐次アクセス専用。スレッド安全ではない
/// （mutex を入れても呼び出し順序は保証できないため、あえて持たない）。
/// std::rand は使わない（値の幅が狭く質が低い上、プログラム全体で 1 つの状態を共有するため並びがずれやすい）。
/// シングルトンにする理由：リプレイで同じ結果にするため、乱数の種と生成器をプロセスで 1 つにそろえるため。
/// </summary>
class RandomGenerator {
public:
    /// <summary>唯一のインスタンス</summary>
    /// <returns>インスタンス</returns>
    static RandomGenerator* GetInstance();

    /// <summary>
    /// シードを与えてゲーム用・見た目用の生成器を初期化する。Framework::Initialize で1回呼ぶ。
    /// </summary>
    /// <param name="seed">シード</param>
    void Initialize(uint32_t seed);

    /// <summary>
    /// 今回のセッションが使用したシード（ログ記録・リプレイ用）。
    /// </summary>
    /// <returns>シード</returns>
    uint32_t GetSeed() const { return seed_; }

    // ----- ゲーム用 -----

    /// <summary>
    /// [0, 1) の一様乱数（ゲーム用）。
    /// </summary>
    /// <returns>0 以上 1 未満の値</returns>
    float NextFloat01();

    /// <summary>
    /// [min, max) の一様乱数（ゲーム用）。
    /// </summary>
    /// <param name="min">下限（含む）</param>
    /// <param name="max">上限（含まない）</param>
    /// <returns>min 以上 max 未満の値</returns>
    float NextFloat(float min, float max);

    /// <summary>
    /// [min, max] の整数一様乱数（ゲーム用、両端含む）。
    /// </summary>
    /// <param name="min">下限（含む）</param>
    /// <param name="max">上限（含む。min 以上であること）</param>
    /// <returns>min 以上 max 以下の整数</returns>
    int NextInt(int min, int max);

    /// <summary>
    /// 自分の生成器を持つクラスに渡す種（ゲーム用の並びから 1 つ取る）。
    /// </summary>
    /// <returns>種</returns>
    uint32_t NextSeed();

    /// <summary>
    /// ゲーム用の生エンジンへの参照（std::shuffle 等に渡す用途）。
    /// </summary>
    /// <returns>std::mt19937 への参照</returns>
    std::mt19937& Engine() { return engine_; }

    // ----- 見た目用 -----

    /// <summary>
    /// [min, max) の一様乱数（見た目用）。
    /// </summary>
    /// <param name="min">下限（含む）</param>
    /// <param name="max">上限（含まない）</param>
    /// <returns>min 以上 max 未満の値</returns>
    float NextVisualFloat(float min, float max);

    /// <summary>
    /// 自分の生成器を持つクラスに渡す種（見た目用の並びから 1 つ取る）。
    /// </summary>
    /// <returns>種</returns>
    uint32_t NextVisualSeed();

    /// <summary>
    /// 見た目用の生エンジンへの参照（分布をその場で作って引く用途）。
    /// </summary>
    /// <returns>std::mt19937 への参照</returns>
    std::mt19937& VisualEngine() { return visualEngine_; }

private:
    RandomGenerator() = default;
    ~RandomGenerator() = default;
    RandomGenerator(const RandomGenerator&) = delete;
    RandomGenerator& operator=(const RandomGenerator&) = delete;

    std::mt19937 engine_;       // ゲーム用
    std::mt19937 visualEngine_; // 見た目用
    uint32_t seed_ = 0;
};
