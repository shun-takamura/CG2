#include "RandomGenerator.h"

namespace {
    // 見た目用の生成器の種を作るとき、セッションのシードに混ぜる値（ゲーム用と別の並びにするため。値に意味はない）
    constexpr uint32_t kVisualStreamSalt = 0x9E3779B9u;
}

RandomGenerator* RandomGenerator::GetInstance() {
    static RandomGenerator instance;
    return &instance;
}

void RandomGenerator::Initialize(uint32_t seed) {
    seed_ = seed;
    engine_.seed(seed);
    // シードと混ぜる値をまとめて種にする（同じシードなら毎回同じ種になる）
    std::seed_seq visualSeed{ seed, kVisualStreamSalt };
    visualEngine_.seed(visualSeed);
}

float RandomGenerator::NextFloat01() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(engine_);
}

float RandomGenerator::NextFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(engine_);
}

int RandomGenerator::NextInt(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(engine_);
}

uint32_t RandomGenerator::NextSeed() {
    return static_cast<uint32_t>(engine_());
}

float RandomGenerator::NextVisualFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(visualEngine_);
}

uint32_t RandomGenerator::NextVisualSeed() {
    return static_cast<uint32_t>(visualEngine_());
}
