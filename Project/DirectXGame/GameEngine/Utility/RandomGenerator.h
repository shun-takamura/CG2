#pragma once
#include <cstdint>
#include <random>

/// <summary>
/// ゲーム挙動に関わる乱数を集約する中央ジェネレータ（シングルトン）。
/// std::mt19937 を1つに集約し、起動時のシードを固定/記録することで
/// S.U.N.D.A.Y. のリプレイ（同一シードからの再現）を可能にする。
///
/// 注意: 決定性のためメインスレッド逐次アクセス専用。スレッド安全ではない
/// （mutex を入れても呼び出し順序は保証できないため、あえて持たない）。
/// 見た目専用（パーティクル等）の乱数はここに集約しない。
/// シングルトンにする理由：リプレイで同じ結果にするため、乱数の種と生成器をプロセスで 1 つにそろえるため。
/// </summary>
class RandomGenerator {
public:
    /// <summary>唯一のインスタンス</summary>
    /// <returns>インスタンス</returns>
    static RandomGenerator* GetInstance();

    /// <summary>
    /// シードを与えてエンジンを初期化する。Framework::Initialize で1回呼ぶ。
    /// </summary>
    /// <param name="seed">シード</param>
    void Initialize(uint32_t seed);

    /// <summary>
    /// 今回のセッションが使用したシード（ログ記録・リプレイ用）。
    /// </summary>
    /// <returns>シード</returns>
    uint32_t GetSeed() const { return seed_; }

    /// <summary>
    /// [0, 1) の一様乱数。
    /// </summary>
    /// <returns>0 以上 1 未満の値</returns>
    float NextFloat01();

    /// <summary>
    /// [min, max) の一様乱数。
    /// </summary>
    /// <param name="min">下限（含む）</param>
    /// <param name="max">上限（含まない）</param>
    /// <returns>min 以上 max 未満の値</returns>
    float NextFloat(float min, float max);

    /// <summary>
    /// [min, max] の整数一様乱数（両端含む）。
    /// </summary>
    /// <param name="min">下限（含む）</param>
    /// <param name="max">上限（含む。min 以上であること）</param>
    /// <returns>min 以上 max 以下の整数</returns>
    int NextInt(int min, int max);

    /// <summary>
    /// 生エンジンへの参照（std::shuffle 等に渡す用途）。
    /// </summary>
    /// <returns>std::mt19937 への参照</returns>
    std::mt19937& Engine() { return engine_; }

private:
    RandomGenerator() = default;
    ~RandomGenerator() = default;
    RandomGenerator(const RandomGenerator&) = delete;
    RandomGenerator& operator=(const RandomGenerator&) = delete;

    std::mt19937 engine_;
    uint32_t seed_ = 0;
};
