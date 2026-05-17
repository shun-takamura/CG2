#include "EffectDef.h"
#include "Json/JsonParser.h"
#include "Json/JsonValue.h"

namespace {
    // ===== 小さなパースヘルパ =====

    Vector3 AsVec3(const JsonValue& v, const Vector3& fallback) {
        if (!v.IsArray() || v.Size() < 3) return fallback;
        return {
            static_cast<float>(v[0].AsDouble(fallback.x)),
            static_cast<float>(v[1].AsDouble(fallback.y)),
            static_cast<float>(v[2].AsDouble(fallback.z)),
        };
    }

    Vector4 AsVec4(const JsonValue& v, const Vector4& fallback) {
        if (!v.IsArray() || v.Size() < 4) return fallback;
        return {
            static_cast<float>(v[0].AsDouble(fallback.x)),
            static_cast<float>(v[1].AsDouble(fallback.y)),
            static_cast<float>(v[2].AsDouble(fallback.z)),
            static_cast<float>(v[3].AsDouble(fallback.w)),
        };
    }

    float AsFloat(const JsonValue& v, float fallback) {
        if (!v.IsNumber()) return fallback;
        return static_cast<float>(v.AsDouble(fallback));
    }

    int AsInt(const JsonValue& v, int fallback) {
        if (!v.IsNumber()) return fallback;
        return static_cast<int>(v.AsInt(fallback));
    }

    uint32_t AsUInt(const JsonValue& v, uint32_t fallback) {
        if (!v.IsNumber()) return fallback;
        int64_t i = v.AsInt(static_cast<int64_t>(fallback));
        if (i < 0) return 0;
        return static_cast<uint32_t>(i);
    }

    BillboardMode AsBillboardMode(const JsonValue& v, BillboardMode fallback) {
        if (v.IsString()) {
            const std::string& s = v.AsString();
            if (s == "None")  return BillboardMode::None;
            if (s == "Full")  return BillboardMode::Full;
            if (s == "YAxis") return BillboardMode::YAxis;
        } else if (v.IsNumber()) {
            int i = static_cast<int>(v.AsInt(static_cast<int64_t>(fallback)));
            if (i >= 0 && i <= 2) return static_cast<BillboardMode>(i);
        }
        return fallback;
    }

    EffectLightKind AsLightKind(const JsonValue& v, EffectLightKind fallback) {
        if (v.IsString()) {
            const std::string& s = v.AsString();
            if (s == "Point") return EffectLightKind::Point;
            if (s == "Spot")  return EffectLightKind::Spot;
        }
        return fallback;
    }

    // ===== コンポーネント別パース =====

    void ParsePrimitive(const JsonValue& o, EffectPrimitiveComponent& c) {
        c.meshType   = AsInt(o["meshType"], c.meshType);
        c.offset     = AsVec3(o["offset"], c.offset);
        c.rotate     = AsVec3(o["rotate"], c.rotate);
        c.startTime  = AsFloat(o["startTime"], c.startTime);
        c.lifetime   = AsFloat(o["lifetime"], c.lifetime);
        c.startScale = AsVec3(o["startScale"], c.startScale);
        c.endScale   = AsVec3(o["endScale"], c.endScale);
        c.startColor = AsVec4(o["startColor"], c.startColor);
        c.endColor   = AsVec4(o["endColor"], c.endColor);
        if (o["texturePath"].IsString()) c.texturePath = o["texturePath"].AsString();
        c.blendMode     = AsInt(o["blendMode"], c.blendMode);
        c.billboardMode = AsBillboardMode(o["billboardMode"], c.billboardMode);
    }

    void ParseParticle(const JsonValue& o, EffectParticleComponent& c) {
        if (o["gpuParticleGroupName"].IsString()) {
            c.gpuParticleGroupName = o["gpuParticleGroupName"].AsString();
        }
        c.offset     = AsVec3(o["offset"], c.offset);
        c.startTime  = AsFloat(o["startTime"], c.startTime);
        c.duration   = AsFloat(o["duration"], c.duration);
        c.burstCount = AsUInt(o["burstCount"], c.burstCount);
        c.billboardMode = AsBillboardMode(o["billboardMode"], c.billboardMode);
    }

    void ParseLight(const JsonValue& o, EffectLightComponent& c) {
        c.kind           = AsLightKind(o["kind"], c.kind);
        c.offset         = AsVec3(o["offset"], c.offset);
        c.direction      = AsVec3(o["direction"], c.direction);
        c.startTime      = AsFloat(o["startTime"], c.startTime);
        c.lifetime       = AsFloat(o["lifetime"], c.lifetime);
        c.color          = AsVec4(o["color"], c.color);
        c.startIntensity = AsFloat(o["startIntensity"], c.startIntensity);
        c.endIntensity   = AsFloat(o["endIntensity"], c.endIntensity);
        c.range          = AsFloat(o["range"], c.range);
        c.spotCosAngle        = AsFloat(o["spotCosAngle"], c.spotCosAngle);
        c.spotCosFalloffStart = AsFloat(o["spotCosFalloffStart"], c.spotCosFalloffStart);
    }
}

namespace EffectDefIO {

    bool LoadFromFile(const std::string& filePath, EffectDef& out) {
        auto result = JsonParser::ParseFile(filePath);
        if (!result.success || !result.value.IsObject()) {
            return false;
        }

        const JsonValue& root = result.value;

        if (root["name"].IsString()) {
            out.name = root["name"].AsString();
        }
        out.totalDuration = AsFloat(root["totalDuration"], out.totalDuration);

        // primitives
        const JsonValue& prims = root["primitives"];
        if (prims.IsArray()) {
            out.primitives.clear();
            out.primitives.reserve(prims.Size());
            for (size_t i = 0; i < prims.Size(); ++i) {
                EffectPrimitiveComponent c{};
                ParsePrimitive(prims[i], c);
                out.primitives.push_back(c);
            }
        }

        // particles
        const JsonValue& parts = root["particles"];
        if (parts.IsArray()) {
            out.particles.clear();
            out.particles.reserve(parts.Size());
            for (size_t i = 0; i < parts.Size(); ++i) {
                EffectParticleComponent c{};
                ParseParticle(parts[i], c);
                out.particles.push_back(c);
            }
        }

        // lights
        const JsonValue& lights = root["lights"];
        if (lights.IsArray()) {
            out.lights.clear();
            out.lights.reserve(lights.Size());
            for (size_t i = 0; i < lights.Size(); ++i) {
                EffectLightComponent c{};
                ParseLight(lights[i], c);
                out.lights.push_back(c);
            }
        }

        return true;
    }

}
