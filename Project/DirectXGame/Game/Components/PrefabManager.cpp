#include "PrefabManager.h"

#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include "Log.h"

#include <filesystem>

namespace {
	constexpr const char* kPrefabDir = "Resources/Json/Prefabs";

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

	Vector2 JsonToVec2(const JsonValue& v, const Vector2& fallback = {}) {
		if (!v.IsArray() || v.Size() < 2) return fallback;
		return {
			static_cast<float>(v[0].AsDouble(fallback.x)),
			static_cast<float>(v[1].AsDouble(fallback.y))
		};
	}
	Vector3 JsonToVec3(const JsonValue& v, const Vector3& fallback = {}) {
		if (!v.IsArray() || v.Size() < 3) return fallback;
		return {
			static_cast<float>(v[0].AsDouble(fallback.x)),
			static_cast<float>(v[1].AsDouble(fallback.y)),
			static_cast<float>(v[2].AsDouble(fallback.z))
		};
	}
	Vector4 JsonToVec4(const JsonValue& v, const Vector4& fallback = {}) {
		if (!v.IsArray() || v.Size() < 4) return fallback;
		return {
			static_cast<float>(v[0].AsDouble(fallback.x)),
			static_cast<float>(v[1].AsDouble(fallback.y)),
			static_cast<float>(v[2].AsDouble(fallback.z)),
			static_cast<float>(v[3].AsDouble(fallback.w))
		};
	}

	const char* PrefabKindToStr(PrefabKind k) {
		switch (k) {
		case PrefabKind::Animated:  return "Animated";
		case PrefabKind::Primitive: return "Primitive";
		case PrefabKind::Object3D:
		default:                    return "Object3D";
		}
	}
	PrefabKind PrefabKindFromStr(const std::string& s, PrefabKind fallback) {
		if (s == "Animated")  return PrefabKind::Animated;
		if (s == "Primitive") return PrefabKind::Primitive;
		if (s == "Object3D")  return PrefabKind::Object3D;
		return fallback;
	}

	const char* BillboardModeToStr(BillboardMode m) {
		switch (m) {
		case BillboardMode::Full:  return "Full";
		case BillboardMode::YAxis: return "YAxis";
		case BillboardMode::None:
		default:                   return "None";
		}
	}
	BillboardMode BillboardModeFromStr(const std::string& s, BillboardMode fallback) {
		if (s == "Full")  return BillboardMode::Full;
		if (s == "YAxis") return BillboardMode::YAxis;
		if (s == "None")  return BillboardMode::None;
		return fallback;
	}

	const char* TimeGroupToStr(TimeGroup g) {
		switch (g) {
		case TimeGroup::Player: return "Player";
		case TimeGroup::UI:     return "UI";
		case TimeGroup::World:
		default:                return "World";
		}
	}
	TimeGroup TimeGroupFromStr(const std::string& s, TimeGroup fallback) {
		if (s == "Player") return TimeGroup::Player;
		if (s == "UI")     return TimeGroup::UI;
		if (s == "World")  return TimeGroup::World;
		return fallback;
	}

	// ===== Primitive 形状パラメータの JSON IO =====

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
	void RingParamsFromJson(const JsonValue& o, PrimitiveGenerator::RingParams& p) {
		p.outerRadius = static_cast<float>(o["outerRadius"].AsDouble(p.outerRadius));
		p.innerRadius = static_cast<float>(o["innerRadius"].AsDouble(p.innerRadius));
		p.divisions   = static_cast<uint32_t>(o["divisions"].AsInt(static_cast<int64_t>(p.divisions)));
		p.innerColor  = JsonToVec4(o["innerColor"], p.innerColor);
		p.outerColor  = JsonToVec4(o["outerColor"], p.outerColor);
		p.startAngle  = static_cast<float>(o["startAngle"].AsDouble(p.startAngle));
		p.endAngle    = static_cast<float>(o["endAngle"].AsDouble(p.endAngle));
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
	void CylinderParamsFromJson(const JsonValue& o, PrimitiveGenerator::CylinderParams& p) {
		p.topRadius    = static_cast<float>(o["topRadius"].AsDouble(p.topRadius));
		p.bottomRadius = static_cast<float>(o["bottomRadius"].AsDouble(p.bottomRadius));
		p.height       = static_cast<float>(o["height"].AsDouble(p.height));
		p.divisions    = static_cast<uint32_t>(o["divisions"].AsInt(static_cast<int64_t>(p.divisions)));
		p.topColor     = JsonToVec4(o["topColor"], p.topColor);
		p.bottomColor  = JsonToVec4(o["bottomColor"], p.bottomColor);
		p.startAngle   = static_cast<float>(o["startAngle"].AsDouble(p.startAngle));
		p.endAngle     = static_cast<float>(o["endAngle"].AsDouble(p.endAngle));
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
	void HelixParamsFromJson(const JsonValue& o, PrimitiveGenerator::HelixParams& p) {
		p.startHelixRadius = static_cast<float>(o["startHelixRadius"].AsDouble(p.startHelixRadius));
		p.endHelixRadius   = static_cast<float>(o["endHelixRadius"].AsDouble(p.endHelixRadius));
		p.startTubeRadius  = static_cast<float>(o["startTubeRadius"].AsDouble(p.startTubeRadius));
		p.endTubeRadius    = static_cast<float>(o["endTubeRadius"].AsDouble(p.endTubeRadius));
		p.pitch            = static_cast<float>(o["pitch"].AsDouble(p.pitch));
		p.turns            = static_cast<float>(o["turns"].AsDouble(p.turns));
		p.circleSegments   = static_cast<uint32_t>(o["circleSegments"].AsInt(static_cast<int64_t>(p.circleSegments)));
		p.lengthSegments   = static_cast<uint32_t>(o["lengthSegments"].AsInt(static_cast<int64_t>(p.lengthSegments)));
		p.startColor       = JsonToVec4(o["startColor"], p.startColor);
		p.endColor         = JsonToVec4(o["endColor"], p.endColor);
	}
}

PrefabManager* PrefabManager::GetInstance() {
	static PrefabManager instance;
	return &instance;
}

const char* PrefabManager::GetPrefabDir() {
	return kPrefabDir;
}

void PrefabManager::Rescan() {
	prefabs_.clear();
	std::filesystem::path dir(kPrefabDir);
	std::error_code ec;
	if (!std::filesystem::exists(dir, ec)) {
		return; // 未作成なら何もしない（プリファブ無し状態）
	}
	for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
		if (ec) break;
		if (!entry.is_regular_file()) continue;
		if (entry.path().extension() != ".json") continue;

		PrefabDef def;
		if (LoadFile(entry.path().generic_string(), def)) {
			prefabs_.push_back(std::move(def));
		}
	}
}

const PrefabDef* PrefabManager::Find(const std::string& name) const {
	for (const auto& p : prefabs_) {
		if (p.name == name) return &p;
	}
	return nullptr;
}

bool PrefabManager::LoadFile(const std::string& filePath, PrefabDef& out) const {
	auto result = JsonParser::ParseFile(filePath);
	if (!result.success) {
		Log(std::string("[PrefabManager] Parse error in ") + filePath + ": " + result.errorMessage + "\n");
		return false;
	}
	const JsonValue& root = result.value;

	// 名前はファイル名から（拡張子無し）
	std::filesystem::path p(filePath);
	out.name = p.stem().string();

	// kind（後方互換：未指定なら model.animated を見て Object3D/Animated を判定）
	if (root["kind"].IsString()) {
		out.kind = PrefabKindFromStr(root["kind"].AsString(), PrefabKind::Object3D);
	} else {
		bool animated = root["model"]["animated"].AsBool(false);
		out.kind = animated ? PrefabKind::Animated : PrefabKind::Object3D;
	}
	out.isAnimated = (out.kind == PrefabKind::Animated);

	out.modelDir = root["model"]["dir"].AsString();
	out.modelFile = root["model"]["file"].AsString();
	out.tag = TagFromName(root["tag"].AsString());

	out.defaultScale = JsonToVec3(root["defaults"]["scale"], { 1,1,1 });
	out.defaultRotate = JsonToVec3(root["defaults"]["rotate"], { 0,0,0 });

	// Primitive 用パラメータ
	if (out.kind == PrefabKind::Primitive) {
		const JsonValue& pp = root["primitive"];
		auto& pr = out.primitiveParams;
		pr.primitiveType = static_cast<int>(pp["primitiveType"].AsInt(static_cast<int64_t>(pr.primitiveType)));
		if (pp["texturePath"].IsString()) pr.texturePath = pp["texturePath"].AsString();
		pr.color          = JsonToVec4(pp["color"], pr.color);
		pr.blendMode      = static_cast<int>(pp["blendMode"].AsInt(static_cast<int64_t>(pr.blendMode)));
		pr.depthWrite     = pp["depthWrite"].AsBool(pr.depthWrite);
		pr.alphaReference = static_cast<float>(pp["alphaReference"].AsDouble(pr.alphaReference));
		pr.cullBackface   = pp["cullBackface"].AsBool(pr.cullBackface);
		pr.samplerMode    = static_cast<int>(pp["samplerMode"].AsInt(static_cast<int64_t>(pr.samplerMode)));
		pr.uvAutoScroll   = pp["uvAutoScroll"].AsBool(pr.uvAutoScroll);
		pr.uvScrollSpeed  = JsonToVec2(pp["uvScrollSpeed"], pr.uvScrollSpeed);
		pr.uvOffset       = JsonToVec2(pp["uvOffset"], pr.uvOffset);
		pr.uvScale        = JsonToVec2(pp["uvScale"], pr.uvScale);
		pr.uvFlipU        = pp["uvFlipU"].AsBool(pr.uvFlipU);
		pr.uvFlipV        = pp["uvFlipV"].AsBool(pr.uvFlipV);
		pr.billboardMode  = BillboardModeFromStr(pp["billboardMode"].AsString(BillboardModeToStr(pr.billboardMode)), pr.billboardMode);
		pr.timeGroup      = TimeGroupFromStr(pp["timeGroup"].AsString(TimeGroupToStr(pr.timeGroup)), pr.timeGroup);
		if (pp["ring"].IsObject())     RingParamsFromJson(pp["ring"], pr.ringParams);
		if (pp["cylinder"].IsObject()) CylinderParamsFromJson(pp["cylinder"], pr.cylinderParams);
		if (pp["helix"].IsObject())    HelixParamsFromJson(pp["helix"], pr.helixParams);
	}

	const JsonValue& col = root["collider"];
	out.hasCollider = col.IsObject() && col.GetType() != JsonValue::Type::Null;
	if (out.hasCollider) {
		// shape: "Sphere" / "OBB" / "Capsule"。未指定なら Sphere（旧形式互換）。
		std::string shapeStr = col["shape"].AsString("Sphere");
		if (shapeStr == "OBB")      out.colliderShape = ColliderShape::OBB;
		else if (shapeStr == "Capsule") out.colliderShape = ColliderShape::Capsule;
		else                            out.colliderShape = ColliderShape::Sphere;

		out.colliderOffset = JsonToVec3(col["offset"], { 0,0,0 });
		out.colliderRadius = static_cast<float>(col["radius"].AsDouble(1.0));
		out.colliderHalfExtents = JsonToVec3(col["halfExtents"], { 0.5f, 0.5f, 0.5f });
		out.colliderCapsuleRadius = static_cast<float>(col["capsuleRadius"].AsDouble(0.5));
		out.colliderCapsuleHeight = static_cast<float>(col["capsuleHeight"].AsDouble(1.0));
	}

	// HP
	const JsonValue& hpJ = root["hp"];
	if (hpJ.IsObject()) {
		out.hasHP = true;
		out.maxHP = static_cast<int>(hpJ["maxHP"].AsInt(static_cast<int64_t>(out.maxHP)));
	}

	// DamageDealer
	const JsonValue& ddJ = root["damageDealer"];
	if (ddJ.IsObject()) {
		out.hasDamageDealer = true;
		out.damage           = static_cast<int>(ddJ["damage"].AsInt(static_cast<int64_t>(out.damage)));
		out.attackMultiplier = static_cast<float>(ddJ["multiplier"].AsDouble(out.attackMultiplier));
	}

	// AttackPower（数値直値 or オブジェクト）
	const JsonValue& apJ = root["attackPower"];
	if (apJ.IsObject()) {
		out.hasAttackPower = true;
		out.attackPower    = static_cast<int>(apJ["value"].AsInt(static_cast<int64_t>(out.attackPower)));
	} else if (apJ.IsNumber()) {
		out.hasAttackPower = true;
		out.attackPower    = static_cast<int>(apJ.AsInt(static_cast<int64_t>(out.attackPower)));
	}

	// Bullet（弾プレハブ用の速度・寿命・ホーミング）
	const JsonValue& blJ = root["bullet"];
	if (blJ.IsObject()) {
		out.hasBullet            = true;
		out.bulletSpeed          = static_cast<float>(blJ["speed"].AsDouble(out.bulletSpeed));
		out.bulletLifetime       = static_cast<float>(blJ["lifetime"].AsDouble(out.bulletLifetime));
		out.bulletHomingStrength = static_cast<float>(blJ["homingStrength"].AsDouble(out.bulletHomingStrength));
	}

	// Carrier（運び屋プレハブ用の子敵パラメータ）
	const JsonValue& caJ = root["carrier"];
	if (caJ.IsObject()) {
		out.hasCarrier               = true;
		out.carrierChildLifetimeSec  = static_cast<float>(caJ["childLifetimeSec"].AsDouble(out.carrierChildLifetimeSec));
		out.carrierChildWanderRadius = static_cast<float>(caJ["childWanderRadius"].AsDouble(out.carrierChildWanderRadius));
		out.carrierChildMoveSpeed    = static_cast<float>(caJ["childMoveSpeed"].AsDouble(out.carrierChildMoveSpeed));
	}

	return true;
}

bool PrefabManager::Save(const PrefabDef& def, const std::string& filePath) {
	JsonValue root = JsonValue::MakeObject();

	root["kind"] = std::string(PrefabKindToStr(def.kind));

	JsonValue modelObj = JsonValue::MakeObject();
	modelObj["dir"] = def.modelDir;
	modelObj["file"] = def.modelFile;
	modelObj["animated"] = (def.kind == PrefabKind::Animated);
	root["model"] = std::move(modelObj);

	root["tag"] = std::string(GetTagName(def.tag));

	JsonValue defaultsObj = JsonValue::MakeObject();
	defaultsObj["scale"] = Vec3ToJson(def.defaultScale);
	defaultsObj["rotate"] = Vec3ToJson(def.defaultRotate);
	root["defaults"] = std::move(defaultsObj);

	if (def.kind == PrefabKind::Primitive) {
		const auto& pr = def.primitiveParams;
		JsonValue pp = JsonValue::MakeObject();
		pp["primitiveType"] = static_cast<int64_t>(pr.primitiveType);
		pp["texturePath"]   = pr.texturePath;
		pp["color"]         = Vec4ToJson(pr.color);
		pp["blendMode"]     = static_cast<int64_t>(pr.blendMode);
		pp["depthWrite"]    = pr.depthWrite;
		pp["alphaReference"] = static_cast<double>(pr.alphaReference);
		pp["cullBackface"]  = pr.cullBackface;
		pp["samplerMode"]   = static_cast<int64_t>(pr.samplerMode);
		pp["uvAutoScroll"]  = pr.uvAutoScroll;
		pp["uvScrollSpeed"] = Vec2ToJson(pr.uvScrollSpeed);
		pp["uvOffset"]      = Vec2ToJson(pr.uvOffset);
		pp["uvScale"]       = Vec2ToJson(pr.uvScale);
		pp["uvFlipU"]       = pr.uvFlipU;
		pp["uvFlipV"]       = pr.uvFlipV;
		pp["billboardMode"] = std::string(BillboardModeToStr(pr.billboardMode));
		pp["timeGroup"]     = std::string(TimeGroupToStr(pr.timeGroup));
		pp["ring"]     = RingParamsToJson(pr.ringParams);
		pp["cylinder"] = CylinderParamsToJson(pr.cylinderParams);
		pp["helix"]    = HelixParamsToJson(pr.helixParams);
		root["primitive"] = std::move(pp);
	}

	if (def.hasCollider) {
		JsonValue colObj = JsonValue::MakeObject();
		const char* shapeStr = "Sphere";
		switch (def.colliderShape) {
		case ColliderShape::OBB:     shapeStr = "OBB"; break;
		case ColliderShape::Capsule: shapeStr = "Capsule"; break;
		case ColliderShape::Sphere:  shapeStr = "Sphere"; break;
		}
		colObj["shape"] = std::string(shapeStr);
		colObj["offset"] = Vec3ToJson(def.colliderOffset);
		colObj["radius"] = static_cast<double>(def.colliderRadius);
		colObj["halfExtents"] = Vec3ToJson(def.colliderHalfExtents);
		colObj["capsuleRadius"] = static_cast<double>(def.colliderCapsuleRadius);
		colObj["capsuleHeight"] = static_cast<double>(def.colliderCapsuleHeight);
		root["collider"] = std::move(colObj);
	}

	if (def.hasHP) {
		JsonValue hpObj = JsonValue::MakeObject();
		hpObj["maxHP"] = static_cast<int64_t>(def.maxHP);
		root["hp"] = std::move(hpObj);
	}

	if (def.hasDamageDealer) {
		JsonValue ddObj = JsonValue::MakeObject();
		ddObj["damage"]     = static_cast<int64_t>(def.damage);
		ddObj["multiplier"] = static_cast<double>(def.attackMultiplier);
		root["damageDealer"] = std::move(ddObj);
	}

	if (def.hasAttackPower) {
		JsonValue apObj = JsonValue::MakeObject();
		apObj["value"] = static_cast<int64_t>(def.attackPower);
		root["attackPower"] = std::move(apObj);
	}

	if (def.hasBullet) {
		JsonValue blObj = JsonValue::MakeObject();
		blObj["speed"]          = static_cast<double>(def.bulletSpeed);
		blObj["lifetime"]       = static_cast<double>(def.bulletLifetime);
		blObj["homingStrength"] = static_cast<double>(def.bulletHomingStrength);
		root["bullet"] = std::move(blObj);
	}

	if (def.hasCarrier) {
		JsonValue caObj = JsonValue::MakeObject();
		caObj["childLifetimeSec"]  = static_cast<double>(def.carrierChildLifetimeSec);
		caObj["childWanderRadius"] = static_cast<double>(def.carrierChildWanderRadius);
		caObj["childMoveSpeed"]    = static_cast<double>(def.carrierChildMoveSpeed);
		root["carrier"] = std::move(caObj);
	}

	// ディレクトリ作成
	std::filesystem::path path(filePath);
	if (path.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
	}
	return JsonWriter::WriteFile(filePath, root, { true, 2 });
}
