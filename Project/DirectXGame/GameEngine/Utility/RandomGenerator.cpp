#include "RandomGenerator.h"

RandomGenerator& RandomGenerator::Instance() {
    static RandomGenerator instance;
    return instance;
}

void RandomGenerator::Initialize(uint32_t seed) {
    seed_ = seed;
    engine_.seed(seed);
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
