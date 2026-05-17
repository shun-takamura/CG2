#include "EffectDef.h"
#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include "Json/JsonWriter.h"

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

    Vector2 AsVec2(const JsonValue& v, const Vector2& fallback) {
        if (!v.IsArray() || v.Size() < 2) return fallback;
        return {
            static_cast<float>(v[0].AsDouble(fallback.x)),
            static_cast<float>(v[1].AsDouble(fallback.y)),
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
        if (o["displayName"].IsString()) c.displayName = o["displayName"].AsString();
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

        // 静的マテリアル
        if (o["depthWrite"].IsBool()) c.depthWrite = o["depthWrite"].AsBool(c.depthWrite);
        c.alphaReference = AsFloat(o["alphaReference"], c.alphaReference);
        if (o["cullBackface"].IsBool()) c.cullBackface = o["cullBackface"].AsBool(c.cullBackface);
        c.samplerMode = AsInt(o["samplerMode"], c.samplerMode);

        // UV
        if (o["uvAutoScroll"].IsBool()) c.uvAutoScroll = o["uvAutoScroll"].AsBool(c.uvAutoScroll);
        c.uvScrollSpeed = AsVec2(o["uvScrollSpeed"], c.uvScrollSpeed);
        c.uvOffset      = AsVec2(o["uvOffset"],      c.uvOffset);
        c.uvScale       = AsVec2(o["uvScale"],       c.uvScale);
        if (o["uvFlipU"].IsBool()) c.uvFlipU = o["uvFlipU"].AsBool(c.uvFlipU);
        if (o["uvFlipV"].IsBool()) c.uvFlipV = o["uvFlipV"].AsBool(c.uvFlipV);
    }

    void ParseParticle(const JsonValue& o, EffectParticleComponent& c) {
        if (o["displayName"].IsString()) c.displayName = o["displayName"].AsString();
        if (o["gpuParticleGroupName"].IsString()) {
            c.gpuParticleGroupName = o["gpuParticleGroupName"].AsString();
        }
        c.offset     = AsVec3(o["offset"], c.offset);
        c.startTime  = AsFloat(o["startTime"], c.startTime);
        c.duration   = AsFloat(o["duration"], c.duration);
        c.burstCount = AsUInt(o["burstCount"], c.burstCount);
        c.billboardMode = AsBillboardMode(o["billboardMode"], c.billboardMode);
    }

    void ParseSound(const JsonValue& o, EffectSoundComponent& c) {
        if (o["displayName"].IsString()) c.displayName = o["displayName"].AsString();
        if (o["soundName"].IsString()) c.soundName = o["soundName"].AsString();
        c.offset        = AsVec3(o["offset"], c.offset);
        c.startTime     = AsFloat(o["startTime"], c.startTime);
        c.distanceScale = AsFloat(o["distanceScale"], c.distanceScale);
        c.volume        = AsFloat(o["volume"], c.volume);
    }

    void ParseLight(const JsonValue& o, EffectLightComponent& c) {
        if (o["displayName"].IsString()) c.displayName = o["displayName"].AsString();
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

        // sounds
        const JsonValue& sounds = root["sounds"];
        if (sounds.IsArray()) {
            out.sounds.clear();
            out.sounds.reserve(sounds.Size());
            for (size_t i = 0; i < sounds.Size(); ++i) {
                EffectSoundComponent c{};
                ParseSound(sounds[i], c);
                out.sounds.push_back(c);
            }
        }

        return true;
    }

    //=========================================================
    // Save
    //=========================================================

    namespace {
        JsonValue Vec2ToJson(const Vector2& v) {
            JsonValue arr = JsonValue::MakeArray();
            arr.Push(JsonValue(static_cast<double>(v.x)));
            arr.Push(JsonValue(static_cast<double>(v.y)));
            return arr;
        }
        JsonValue Vec3ToJson(const Vector3& v) {
            JsonValue arr = JsonValue::MakeArray();
            arr.Push(JsonValue(static_cast<double>(v.x)));
            arr.Push(JsonValue(static_cast<double>(v.y)));
            arr.Push(JsonValue(static_cast<double>(v.z)));
            return arr;
        }
        JsonValue Vec4ToJson(const Vector4& v) {
            JsonValue arr = JsonValue::MakeArray();
            arr.Push(JsonValue(static_cast<double>(v.x)));
            arr.Push(JsonValue(static_cast<double>(v.y)));
            arr.Push(JsonValue(static_cast<double>(v.z)));
            arr.Push(JsonValue(static_cast<double>(v.w)));
            return arr;
        }
        const char* BillboardModeStr(BillboardMode m) {
            switch (m) {
            case BillboardMode::None:  return "None";
            case BillboardMode::Full:  return "Full";
            case BillboardMode::YAxis: return "YAxis";
            }
            return "Full";
        }
        const char* LightKindStr(EffectLightKind k) {
            return k == EffectLightKind::Spot ? "Spot" : "Point";
        }
    }

    bool SaveToFile(const std::string& filePath, const EffectDef& def) {
        JsonValue root = JsonValue::MakeObject();
        root["name"] = def.name;
        root["totalDuration"] = static_cast<double>(def.totalDuration);

        JsonValue primArr = JsonValue::MakeArray();
        for (const auto& c : def.primitives) {
            JsonValue o = JsonValue::MakeObject();
            o["displayName"] = c.displayName;
            o["meshType"]   = static_cast<int64_t>(c.meshType);
            o["offset"]     = Vec3ToJson(c.offset);
            o["rotate"]     = Vec3ToJson(c.rotate);
            o["startTime"]  = static_cast<double>(c.startTime);
            o["lifetime"]   = static_cast<double>(c.lifetime);
            o["startScale"] = Vec3ToJson(c.startScale);
            o["endScale"]   = Vec3ToJson(c.endScale);
            o["startColor"] = Vec4ToJson(c.startColor);
            o["endColor"]   = Vec4ToJson(c.endColor);
            o["texturePath"] = c.texturePath;
            o["blendMode"]   = static_cast<int64_t>(c.blendMode);
            o["billboardMode"] = std::string(BillboardModeStr(c.billboardMode));
            // 静的マテリアル
            o["depthWrite"]     = c.depthWrite;
            o["alphaReference"] = static_cast<double>(c.alphaReference);
            o["cullBackface"]   = c.cullBackface;
            o["samplerMode"]    = static_cast<int64_t>(c.samplerMode);
            // UV
            o["uvAutoScroll"]   = c.uvAutoScroll;
            o["uvScrollSpeed"]  = Vec2ToJson(c.uvScrollSpeed);
            o["uvOffset"]       = Vec2ToJson(c.uvOffset);
            o["uvScale"]        = Vec2ToJson(c.uvScale);
            o["uvFlipU"]        = c.uvFlipU;
            o["uvFlipV"]        = c.uvFlipV;
            primArr.Push(std::move(o));
        }
        root["primitives"] = std::move(primArr);

        JsonValue partArr = JsonValue::MakeArray();
        for (const auto& c : def.particles) {
            JsonValue o = JsonValue::MakeObject();
            o["displayName"] = c.displayName;
            o["gpuParticleGroupName"] = c.gpuParticleGroupName;
            o["offset"]     = Vec3ToJson(c.offset);
            o["startTime"]  = static_cast<double>(c.startTime);
            o["duration"]   = static_cast<double>(c.duration);
            o["burstCount"] = static_cast<int64_t>(c.burstCount);
            o["billboardMode"] = std::string(BillboardModeStr(c.billboardMode));
            partArr.Push(std::move(o));
        }
        root["particles"] = std::move(partArr);

        JsonValue lightArr = JsonValue::MakeArray();
        for (const auto& c : def.lights) {
            JsonValue o = JsonValue::MakeObject();
            o["displayName"] = c.displayName;
            o["kind"]      = std::string(LightKindStr(c.kind));
            o["offset"]    = Vec3ToJson(c.offset);
            o["direction"] = Vec3ToJson(c.direction);
            o["startTime"] = static_cast<double>(c.startTime);
            o["lifetime"]  = static_cast<double>(c.lifetime);
            o["color"]     = Vec4ToJson(c.color);
            o["startIntensity"] = static_cast<double>(c.startIntensity);
            o["endIntensity"]   = static_cast<double>(c.endIntensity);
            o["range"] = static_cast<double>(c.range);
            o["spotCosAngle"] = static_cast<double>(c.spotCosAngle);
            o["spotCosFalloffStart"] = static_cast<double>(c.spotCosFalloffStart);
            lightArr.Push(std::move(o));
        }
        root["lights"] = std::move(lightArr);

        JsonValue soundArr = JsonValue::MakeArray();
        for (const auto& c : def.sounds) {
            JsonValue o = JsonValue::MakeObject();
            o["displayName"] = c.displayName;
            o["soundName"]     = c.soundName;
            o["offset"]        = Vec3ToJson(c.offset);
            o["startTime"]     = static_cast<double>(c.startTime);
            o["distanceScale"] = static_cast<double>(c.distanceScale);
            o["volume"]        = static_cast<double>(c.volume);
            soundArr.Push(std::move(o));
        }
        root["sounds"] = std::move(soundArr);

        return JsonWriter::WriteFile(filePath, root, { true, 2 });
    }

}
