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

    // ===== 形状パラメータのパース =====

    void ParseRingParams(const JsonValue& o, PrimitiveGenerator::RingParams& p) {
        if (!o.IsObject()) return;
        p.outerRadius = AsFloat(o["outerRadius"], p.outerRadius);
        p.innerRadius = AsFloat(o["innerRadius"], p.innerRadius);
        p.divisions   = AsUInt(o["divisions"], p.divisions);
        p.innerColor  = AsVec4(o["innerColor"], p.innerColor);
        p.outerColor  = AsVec4(o["outerColor"], p.outerColor);
        p.startAngle  = AsFloat(o["startAngle"], p.startAngle);
        p.endAngle    = AsFloat(o["endAngle"], p.endAngle);
    }

    void ParseCylinderParams(const JsonValue& o, PrimitiveGenerator::CylinderParams& p) {
        if (!o.IsObject()) return;
        p.topRadius    = AsFloat(o["topRadius"], p.topRadius);
        p.bottomRadius = AsFloat(o["bottomRadius"], p.bottomRadius);
        p.height       = AsFloat(o["height"], p.height);
        p.divisions    = AsUInt(o["divisions"], p.divisions);
        p.topColor     = AsVec4(o["topColor"], p.topColor);
        p.bottomColor  = AsVec4(o["bottomColor"], p.bottomColor);
        p.startAngle   = AsFloat(o["startAngle"], p.startAngle);
        p.endAngle     = AsFloat(o["endAngle"], p.endAngle);
    }

    void ParseBeamAppearance(const JsonValue& o, PrimitiveGenerator::BeamAppearance& a) {
        if (!o.IsObject()) return;
        a.startWidth      = AsFloat(o["startWidth"], a.startWidth);
        a.endWidth        = AsFloat(o["endWidth"], a.endWidth);
        a.planeCount      = AsUInt(o["planeCount"], a.planeCount);
        a.fadeStartLength = AsFloat(o["fadeStartLength"], a.fadeStartLength);
        a.fadeEndLength   = AsFloat(o["fadeEndLength"], a.fadeEndLength);
        a.startColor      = AsVec4(o["startColor"], a.startColor);
        a.endColor        = AsVec4(o["endColor"], a.endColor);
        if (o["uvWrapByLength"].IsBool()) a.uvWrapByLength = o["uvWrapByLength"].AsBool(a.uvWrapByLength);
        a.uvTilesPerUnit  = AsFloat(o["uvTilesPerUnit"], a.uvTilesPerUnit);
    }

    void ParseBeamParams(const JsonValue& o, PrimitiveGenerator::BeamParams& p) {
        if (!o.IsObject()) return;
        p.startPos       = AsVec3(o["startPos"], p.startPos);
        p.endPos         = AsVec3(o["endPos"], p.endPos);
        p.lengthSegments = AsUInt(o["lengthSegments"], p.lengthSegments);
        ParseBeamAppearance(o["appearance"], p.appearance);
    }

    void ParseLightningParams(const JsonValue& o, PrimitiveGenerator::LightningBoltParams& p) {
        if (!o.IsObject()) return;
        p.startPos          = AsVec3(o["startPos"], p.startPos);
        p.endPos            = AsVec3(o["endPos"], p.endPos);
        p.generations       = AsUInt(o["generations"], p.generations);
        p.maxOffsetRatio    = AsFloat(o["maxOffsetRatio"], p.maxOffsetRatio);
        p.randomSeed        = AsUInt(o["randomSeed"], p.randomSeed);
        p.branchProbability = AsFloat(o["branchProbability"], p.branchProbability);
        p.branchLengthScale = AsFloat(o["branchLengthScale"], p.branchLengthScale);
        p.branchMaxAngle    = AsFloat(o["branchMaxAngle"], p.branchMaxAngle);
        p.branchWidthScale  = AsFloat(o["branchWidthScale"], p.branchWidthScale);
        p.branchColorScale  = AsFloat(o["branchColorScale"], p.branchColorScale);
        ParseBeamAppearance(o["appearance"], p.appearance);
    }

    void ParseHelixParams(const JsonValue& o, PrimitiveGenerator::HelixParams& p) {
        if (!o.IsObject()) return;
        p.startHelixRadius = AsFloat(o["startHelixRadius"], p.startHelixRadius);
        p.endHelixRadius   = AsFloat(o["endHelixRadius"], p.endHelixRadius);
        p.startTubeRadius  = AsFloat(o["startTubeRadius"], p.startTubeRadius);
        p.endTubeRadius    = AsFloat(o["endTubeRadius"], p.endTubeRadius);
        p.pitch            = AsFloat(o["pitch"], p.pitch);
        p.turns            = AsFloat(o["turns"], p.turns);
        p.circleSegments   = AsUInt(o["circleSegments"], p.circleSegments);
        p.lengthSegments   = AsUInt(o["lengthSegments"], p.lengthSegments);
        p.startColor       = AsVec4(o["startColor"], p.startColor);
        p.endColor         = AsVec4(o["endColor"], p.endColor);
    }

    // ===== コンポーネント別パース =====

    void ParsePrimitive(const JsonValue& o, EffectPrimitiveComponent& c) {
        if (o["displayName"].IsString()) c.displayName = o["displayName"].AsString();
        c.meshType   = AsInt(o["meshType"], c.meshType);
        // 形状ごとのジオメトリパラメータ
        ParseRingParams(o["ringParams"], c.ringParams);
        ParseCylinderParams(o["cylinderParams"], c.cylinderParams);
        ParseHelixParams(o["helixParams"], c.helixParams);
        ParseBeamParams(o["beamParams"], c.beamParams);
        ParseLightningParams(o["lightningParams"], c.lightningParams);
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
        c.viewAngleFadePower = AsFloat(o["viewAngleFadePower"], c.viewAngleFadePower);

        // UV
        if (o["uvAutoScroll"].IsBool()) c.uvAutoScroll = o["uvAutoScroll"].AsBool(c.uvAutoScroll);
        c.uvScrollSpeed = AsVec2(o["uvScrollSpeed"], c.uvScrollSpeed);
        c.uvOffset      = AsVec2(o["uvOffset"],      c.uvOffset);
        c.uvScale       = AsVec2(o["uvScale"],       c.uvScale);
        if (o["uvFlipU"].IsBool()) c.uvFlipU = o["uvFlipU"].AsBool(c.uvFlipU);
        if (o["uvFlipV"].IsBool()) c.uvFlipV = o["uvFlipV"].AsBool(c.uvFlipV);

        // Distortion
        if (o["useDistortion"].IsBool()) c.useDistortion = o["useDistortion"].AsBool(c.useDistortion);
        if (o["distortionTexturePath"].IsString()) c.distortionTexturePath = o["distortionTexturePath"].AsString();
        c.distortionStrength = AsFloat(o["distortionStrength"], c.distortionStrength);
        if (o["distortionUvAutoScroll"].IsBool()) c.distortionUvAutoScroll = o["distortionUvAutoScroll"].AsBool(c.distortionUvAutoScroll);
        c.distortionUvScrollSpeed = AsVec2(o["distortionUvScrollSpeed"], c.distortionUvScrollSpeed);
        c.distortionUvOffset      = AsVec2(o["distortionUvOffset"],      c.distortionUvOffset);
        c.distortionUvScale       = AsVec2(o["distortionUvScale"],       c.distortionUvScale);
        if (o["distortionUvFlipU"].IsBool()) c.distortionUvFlipU = o["distortionUvFlipU"].AsBool(c.distortionUvFlipU);
        if (o["distortionUvFlipV"].IsBool()) c.distortionUvFlipV = o["distortionUvFlipV"].AsBool(c.distortionUvFlipV);
    }

    void ParseParticle(const JsonValue& o, EffectParticleComponent& c) {
        if (o["displayName"].IsString()) c.displayName = o["displayName"].AsString();
        if (o["gpuParticleGroupName"].IsString()) {
            c.gpuParticleGroupName = o["gpuParticleGroupName"].AsString();
        }
        if (o["texturePath"].IsString()) {
            c.texturePath = o["texturePath"].AsString();
        }
        c.offset     = AsVec3(o["offset"], c.offset);
        c.startTime  = AsFloat(o["startTime"], c.startTime);
        c.duration   = AsFloat(o["duration"], c.duration);
        c.burstCount = AsUInt(o["burstCount"], c.burstCount);
        c.billboardMode = AsBillboardMode(o["billboardMode"], c.billboardMode);
        c.colorMode  = AsInt(o["colorMode"], c.colorMode);
        c.startColor = AsVec4(o["startColor"], c.startColor);
        c.endColor   = AsVec4(o["endColor"], c.endColor);
        c.scaleMin   = AsVec2(o["scaleMin"], c.scaleMin);
        c.scaleMax   = AsVec2(o["scaleMax"], c.scaleMax);
        if (o["uniformScale"].IsBool()) c.uniformScale = o["uniformScale"].AsBool(c.uniformScale);
        // 発生
        c.emitRadius       = AsFloat(o["emitRadius"], c.emitRadius);
        c.particleLifeTime = AsFloat(o["particleLifeTime"], c.particleLifeTime);
        // 初速制御
        c.velocityMode   = AsInt(o["velocityMode"], c.velocityMode);
        c.velocityDir    = AsVec3(o["velocityDir"], c.velocityDir);
        c.velocitySpeed  = AsFloat(o["velocitySpeed"], c.velocitySpeed);
        c.velocityJitter = AsFloat(o["velocityJitter"], c.velocityJitter);
        // 多色グラデーションキー
        c.colorKeys.clear();
        const JsonValue& ck = o["colorKeys"];
        if (ck.IsArray()) {
            for (size_t i = 0; i < ck.Size(); ++i) {
                const JsonValue& k = ck[i];
                EffectColorKey key;
                key.location = AsFloat(k["location"], key.location);
                key.color    = AsVec4(k["color"], key.color);
                c.colorKeys.push_back(key);
            }
        }
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
        if (root["loop"].IsBool()) out.loop = root["loop"].AsBool(out.loop);

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

        JsonValue RingParamsToJson(const PrimitiveGenerator::RingParams& p) {
            JsonValue o = JsonValue::MakeObject();
            o["outerRadius"] = static_cast<double>(p.outerRadius);
            o["innerRadius"] = static_cast<double>(p.innerRadius);
            o["divisions"]   = static_cast<int64_t>(p.divisions);
            o["innerColor"]  = Vec4ToJson(p.innerColor);
            o["outerColor"]  = Vec4ToJson(p.outerColor);
            o["startAngle"]  = static_cast<double>(p.startAngle);
            o["endAngle"]    = static_cast<double>(p.endAngle);
            return o;
        }
        JsonValue CylinderParamsToJson(const PrimitiveGenerator::CylinderParams& p) {
            JsonValue o = JsonValue::MakeObject();
            o["topRadius"]    = static_cast<double>(p.topRadius);
            o["bottomRadius"] = static_cast<double>(p.bottomRadius);
            o["height"]       = static_cast<double>(p.height);
            o["divisions"]    = static_cast<int64_t>(p.divisions);
            o["topColor"]     = Vec4ToJson(p.topColor);
            o["bottomColor"]  = Vec4ToJson(p.bottomColor);
            o["startAngle"]   = static_cast<double>(p.startAngle);
            o["endAngle"]     = static_cast<double>(p.endAngle);
            return o;
        }
        JsonValue BeamAppearanceToJson(const PrimitiveGenerator::BeamAppearance& a) {
            JsonValue o = JsonValue::MakeObject();
            o["startWidth"]      = static_cast<double>(a.startWidth);
            o["endWidth"]        = static_cast<double>(a.endWidth);
            o["planeCount"]      = static_cast<int64_t>(a.planeCount);
            o["fadeStartLength"] = static_cast<double>(a.fadeStartLength);
            o["fadeEndLength"]   = static_cast<double>(a.fadeEndLength);
            o["startColor"]      = Vec4ToJson(a.startColor);
            o["endColor"]        = Vec4ToJson(a.endColor);
            o["uvWrapByLength"]  = a.uvWrapByLength;
            o["uvTilesPerUnit"]  = static_cast<double>(a.uvTilesPerUnit);
            return o;
        }
        JsonValue BeamParamsToJson(const PrimitiveGenerator::BeamParams& p) {
            JsonValue o = JsonValue::MakeObject();
            o["startPos"]       = Vec3ToJson(p.startPos);
            o["endPos"]         = Vec3ToJson(p.endPos);
            o["lengthSegments"] = static_cast<int64_t>(p.lengthSegments);
            o["appearance"]     = BeamAppearanceToJson(p.appearance);
            return o;
        }
        JsonValue LightningParamsToJson(const PrimitiveGenerator::LightningBoltParams& p) {
            JsonValue o = JsonValue::MakeObject();
            o["startPos"]          = Vec3ToJson(p.startPos);
            o["endPos"]            = Vec3ToJson(p.endPos);
            o["generations"]       = static_cast<int64_t>(p.generations);
            o["maxOffsetRatio"]    = static_cast<double>(p.maxOffsetRatio);
            o["randomSeed"]        = static_cast<int64_t>(p.randomSeed);
            o["branchProbability"] = static_cast<double>(p.branchProbability);
            o["branchLengthScale"] = static_cast<double>(p.branchLengthScale);
            o["branchMaxAngle"]    = static_cast<double>(p.branchMaxAngle);
            o["branchWidthScale"]  = static_cast<double>(p.branchWidthScale);
            o["branchColorScale"]  = static_cast<double>(p.branchColorScale);
            o["appearance"]        = BeamAppearanceToJson(p.appearance);
            return o;
        }
        JsonValue HelixParamsToJson(const PrimitiveGenerator::HelixParams& p) {
            JsonValue o = JsonValue::MakeObject();
            o["startHelixRadius"] = static_cast<double>(p.startHelixRadius);
            o["endHelixRadius"]   = static_cast<double>(p.endHelixRadius);
            o["startTubeRadius"]  = static_cast<double>(p.startTubeRadius);
            o["endTubeRadius"]    = static_cast<double>(p.endTubeRadius);
            o["pitch"]            = static_cast<double>(p.pitch);
            o["turns"]            = static_cast<double>(p.turns);
            o["circleSegments"]   = static_cast<int64_t>(p.circleSegments);
            o["lengthSegments"]   = static_cast<int64_t>(p.lengthSegments);
            o["startColor"]       = Vec4ToJson(p.startColor);
            o["endColor"]         = Vec4ToJson(p.endColor);
            return o;
        }
    }

    bool SaveToFile(const std::string& filePath, const EffectDef& def) {
        JsonValue root = JsonValue::MakeObject();
        root["name"] = def.name;
        root["totalDuration"] = static_cast<double>(def.totalDuration);
        root["loop"] = def.loop;

        JsonValue primArr = JsonValue::MakeArray();
        for (const auto& c : def.primitives) {
            JsonValue o = JsonValue::MakeObject();
            o["displayName"] = c.displayName;
            o["meshType"]   = static_cast<int64_t>(c.meshType);
            o["ringParams"]     = RingParamsToJson(c.ringParams);
            o["cylinderParams"] = CylinderParamsToJson(c.cylinderParams);
            o["helixParams"]    = HelixParamsToJson(c.helixParams);
            o["beamParams"]     = BeamParamsToJson(c.beamParams);
            o["lightningParams"] = LightningParamsToJson(c.lightningParams);
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
            o["viewAngleFadePower"] = static_cast<double>(c.viewAngleFadePower);
            // UV
            o["uvAutoScroll"]   = c.uvAutoScroll;
            o["uvScrollSpeed"]  = Vec2ToJson(c.uvScrollSpeed);
            o["uvOffset"]       = Vec2ToJson(c.uvOffset);
            o["uvScale"]        = Vec2ToJson(c.uvScale);
            o["uvFlipU"]        = c.uvFlipU;
            o["uvFlipV"]        = c.uvFlipV;
            // Distortion
            o["useDistortion"]           = c.useDistortion;
            o["distortionTexturePath"]   = c.distortionTexturePath;
            o["distortionStrength"]      = static_cast<double>(c.distortionStrength);
            o["distortionUvAutoScroll"]  = c.distortionUvAutoScroll;
            o["distortionUvScrollSpeed"] = Vec2ToJson(c.distortionUvScrollSpeed);
            o["distortionUvOffset"]      = Vec2ToJson(c.distortionUvOffset);
            o["distortionUvScale"]       = Vec2ToJson(c.distortionUvScale);
            o["distortionUvFlipU"]       = c.distortionUvFlipU;
            o["distortionUvFlipV"]       = c.distortionUvFlipV;
            primArr.Push(std::move(o));
        }
        root["primitives"] = std::move(primArr);

        JsonValue partArr = JsonValue::MakeArray();
        for (const auto& c : def.particles) {
            JsonValue o = JsonValue::MakeObject();
            o["displayName"] = c.displayName;
            o["gpuParticleGroupName"] = c.gpuParticleGroupName;
            o["texturePath"] = c.texturePath;
            o["offset"]     = Vec3ToJson(c.offset);
            o["startTime"]  = static_cast<double>(c.startTime);
            o["duration"]   = static_cast<double>(c.duration);
            o["burstCount"] = static_cast<int64_t>(c.burstCount);
            o["billboardMode"] = std::string(BillboardModeStr(c.billboardMode));
            o["colorMode"]  = static_cast<int64_t>(c.colorMode);
            o["startColor"] = Vec4ToJson(c.startColor);
            o["endColor"]   = Vec4ToJson(c.endColor);
            o["scaleMin"]   = Vec2ToJson(c.scaleMin);
            o["scaleMax"]   = Vec2ToJson(c.scaleMax);
            o["uniformScale"] = c.uniformScale;
            o["emitRadius"]       = static_cast<double>(c.emitRadius);
            o["particleLifeTime"] = static_cast<double>(c.particleLifeTime);
            o["velocityMode"]   = static_cast<int64_t>(c.velocityMode);
            o["velocityDir"]    = Vec3ToJson(c.velocityDir);
            o["velocitySpeed"]  = static_cast<double>(c.velocitySpeed);
            o["velocityJitter"] = static_cast<double>(c.velocityJitter);
            {
                JsonValue ckArr = JsonValue::MakeArray();
                for (const auto& k : c.colorKeys) {
                    JsonValue ko = JsonValue::MakeObject();
                    ko["location"] = static_cast<double>(k.location);
                    ko["color"]    = Vec4ToJson(k.color);
                    ckArr.Push(std::move(ko));
                }
                o["colorKeys"] = std::move(ckArr);
            }
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
