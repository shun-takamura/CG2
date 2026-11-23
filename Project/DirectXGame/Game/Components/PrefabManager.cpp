#include "PrefabManager.h"

#include "Json/JsonValue.h"
#include "Json/JsonVector.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include "Log.h"

#include <filesystem>

namespace {
	constexpr const char* kPrefabDir = "Resources/Json/Prefabs";
	// JSON に無いときのコライダー（OBB）の半分の大きさ
	constexpr float kDefaultColliderHalfExtent = 0.5f;
	// JSON に無いときのカプセルの半径
	constexpr double kDefaultCapsuleRadius = 0.5;
	// 撃破スコアの既定値（これと違うときだけ書き出す。PrefabDef::scoreValue の初期値と同じ）
	constexpr int kDefaultScoreValue = 10;
	// 書き出す JSON の字下げの幅 [文字]
	constexpr int kJsonIndentWidth = 2;

	/// <summary>プリファブの種別を保存用の文字列にする</summary>
	/// <param name="k">種別</param>
	/// <returns>"Animated" / "Primitive" / "Object3D"（不明なら "Object3D"）</returns>
	const char* PrefabKindToStr(PrefabKind k) {
		switch (k) {
		case PrefabKind::Animated:  return "Animated";
		case PrefabKind::Primitive: return "Primitive";
		case PrefabKind::Object3D:
		default:                    return "Object3D";
		}
	}
	/// <summary>文字列からプリファブの種別を読む</summary>
	/// <param name="s">種別の文字列</param>
	/// <param name="fallback">どれにも当たらない時の値</param>
	/// <returns>種別。読めなければ fallback</returns>
	PrefabKind PrefabKindFromStr(const std::string& s, PrefabKind fallback) {
		if (s == "Animated")  return PrefabKind::Animated;
		if (s == "Primitive") return PrefabKind::Primitive;
		if (s == "Object3D")  return PrefabKind::Object3D;
		return fallback;
	}

	/// <summary>ビルボードの種類を保存用の文字列にする</summary>
	/// <param name="m">ビルボードの種類</param>
	/// <returns>"Full" / "YAxis" / "None"（不明なら "None"）</returns>
	const char* BillboardModeToStr(BillboardMode m) {
		switch (m) {
		case BillboardMode::Full:  return "Full";
		case BillboardMode::YAxis: return "YAxis";
		case BillboardMode::None:
		default:                   return "None";
		}
	}
	/// <summary>文字列からビルボードの種類を読む</summary>
	/// <param name="s">種類の文字列</param>
	/// <param name="fallback">どれにも当たらない時の値</param>
	/// <returns>種類。読めなければ fallback</returns>
	BillboardMode BillboardModeFromStr(const std::string& s, BillboardMode fallback) {
		if (s == "Full")  return BillboardMode::Full;
		if (s == "YAxis") return BillboardMode::YAxis;
		if (s == "None")  return BillboardMode::None;
		return fallback;
	}

	/// <summary>時間グループを保存用の文字列にする</summary>
	/// <param name="g">時間グループ</param>
	/// <returns>"Player" / "UI" / "World"（不明なら "World"）</returns>
	const char* TimeGroupToStr(TimeGroup g) {
		switch (g) {
		case TimeGroup::Player: return "Player";
		case TimeGroup::UI:     return "UI";
		case TimeGroup::World:
		default:                return "World";
		}
	}
	/// <summary>文字列から時間グループを読む</summary>
	/// <param name="s">時間グループの文字列</param>
	/// <param name="fallback">どれにも当たらない時の値</param>
	/// <returns>時間グループ。読めなければ fallback</returns>
	TimeGroup TimeGroupFromStr(const std::string& s, TimeGroup fallback) {
		if (s == "Player") return TimeGroup::Player;
		if (s == "UI")     return TimeGroup::UI;
		if (s == "World")  return TimeGroup::World;
		return fallback;
	}

	// ===== Primitive 形状パラメータの JSON IO =====

	/// <summary>リングの形状パラメータを JSON にする</summary>
	/// <param name="p">書き出すパラメータ</param>
	/// <returns>JSON オブジェクト</returns>
	JsonValue RingParamsToJson(const PrimitiveGenerator::RingParams& p) {
		JsonValue o = JsonValue::MakeObject();
		o["outerRadius"] = static_cast<double>(p.outerRadius);
		o["innerRadius"] = static_cast<double>(p.innerRadius);
		o["divisions"]   = static_cast<int64_t>(p.divisions);
		o["innerColor"]  = MakeVector4(p.innerColor);
		o["outerColor"]  = MakeVector4(p.outerColor);
		o["startAngle"]  = static_cast<double>(p.startAngle);
		o["endAngle"]    = static_cast<double>(p.endAngle);
		return o;
	}
	/// <summary>リングの形状パラメータを読む</summary>
	/// <param name="o">読む JSON オブジェクト</param>
	/// <param name="p">書き込み先（無いキーは元の値のまま）</param>
	void RingParamsFromJson(const JsonValue& o, PrimitiveGenerator::RingParams& p) {
		p.outerRadius = static_cast<float>(o["outerRadius"].AsDouble(p.outerRadius));
		p.innerRadius = static_cast<float>(o["innerRadius"].AsDouble(p.innerRadius));
		p.divisions   = static_cast<uint32_t>(o["divisions"].AsInt(static_cast<int64_t>(p.divisions)));
		p.innerColor  = ReadVector4(o["innerColor"], p.innerColor);
		p.outerColor  = ReadVector4(o["outerColor"], p.outerColor);
		p.startAngle  = static_cast<float>(o["startAngle"].AsDouble(p.startAngle));
		p.endAngle    = static_cast<float>(o["endAngle"].AsDouble(p.endAngle));
	}

	/// <summary>円柱の形状パラメータを JSON にする</summary>
	/// <param name="p">書き出すパラメータ</param>
	/// <returns>JSON オブジェクト</returns>
	JsonValue CylinderParamsToJson(const PrimitiveGenerator::CylinderParams& p) {
		JsonValue o = JsonValue::MakeObject();
		o["topRadius"]    = static_cast<double>(p.topRadius);
		o["bottomRadius"] = static_cast<double>(p.bottomRadius);
		o["height"]       = static_cast<double>(p.height);
		o["divisions"]    = static_cast<int64_t>(p.divisions);
		o["topColor"]     = MakeVector4(p.topColor);
		o["bottomColor"]  = MakeVector4(p.bottomColor);
		o["startAngle"]   = static_cast<double>(p.startAngle);
		o["endAngle"]     = static_cast<double>(p.endAngle);
		return o;
	}
	/// <summary>円柱の形状パラメータを読む</summary>
	/// <param name="o">読む JSON オブジェクト</param>
	/// <param name="p">書き込み先（無いキーは元の値のまま）</param>
	void CylinderParamsFromJson(const JsonValue& o, PrimitiveGenerator::CylinderParams& p) {
		p.topRadius    = static_cast<float>(o["topRadius"].AsDouble(p.topRadius));
		p.bottomRadius = static_cast<float>(o["bottomRadius"].AsDouble(p.bottomRadius));
		p.height       = static_cast<float>(o["height"].AsDouble(p.height));
		p.divisions    = static_cast<uint32_t>(o["divisions"].AsInt(static_cast<int64_t>(p.divisions)));
		p.topColor     = ReadVector4(o["topColor"], p.topColor);
		p.bottomColor  = ReadVector4(o["bottomColor"], p.bottomColor);
		p.startAngle   = static_cast<float>(o["startAngle"].AsDouble(p.startAngle));
		p.endAngle     = static_cast<float>(o["endAngle"].AsDouble(p.endAngle));
	}

	/// <summary>らせんの形状パラメータを JSON にする</summary>
	/// <param name="p">書き出すパラメータ</param>
	/// <returns>JSON オブジェクト</returns>
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
		o["startColor"]       = MakeVector4(p.startColor);
		o["endColor"]         = MakeVector4(p.endColor);
		return o;
	}
	/// <summary>らせんの形状パラメータを読む</summary>
	/// <param name="o">読む JSON オブジェクト</param>
	/// <param name="p">書き込み先（無いキーは元の値のまま）</param>
	void HelixParamsFromJson(const JsonValue& o, PrimitiveGenerator::HelixParams& p) {
		p.startHelixRadius = static_cast<float>(o["startHelixRadius"].AsDouble(p.startHelixRadius));
		p.endHelixRadius   = static_cast<float>(o["endHelixRadius"].AsDouble(p.endHelixRadius));
		p.startTubeRadius  = static_cast<float>(o["startTubeRadius"].AsDouble(p.startTubeRadius));
		p.endTubeRadius    = static_cast<float>(o["endTubeRadius"].AsDouble(p.endTubeRadius));
		p.pitch            = static_cast<float>(o["pitch"].AsDouble(p.pitch));
		p.turns            = static_cast<float>(o["turns"].AsDouble(p.turns));
		p.circleSegments   = static_cast<uint32_t>(o["circleSegments"].AsInt(static_cast<int64_t>(p.circleSegments)));
		p.lengthSegments   = static_cast<uint32_t>(o["lengthSegments"].AsInt(static_cast<int64_t>(p.lengthSegments)));
		p.startColor       = ReadVector4(o["startColor"], p.startColor);
		p.endColor         = ReadVector4(o["endColor"], p.endColor);
	}
}

PrefabManager* PrefabManager::GetInstance() {
	static PrefabManager instance;
	return &instance;
}

const char* PrefabManager::GetPrefabDir() {
	return kPrefabDir;
}

bool PrefabManager::Delete(const std::string& name) {
	if (name.empty()) return false;
	std::filesystem::path path = std::filesystem::path(kPrefabDir) / (name + ".json");
	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) {
		Log(std::string("[PrefabManager] Delete: file not found: ") + path.generic_string() + "\n");
		return false;
	}
	if (!std::filesystem::remove(path, ec) || ec) {
		Log(std::string("[PrefabManager] Delete failed: ") + path.generic_string() + " (" + ec.message() + ")\n");
		return false;
	}
	Log(std::string("[PrefabManager] Deleted prefab: ") + path.generic_string() + "\n");
	return true;
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

	out.defaultScale = ReadVector3(root["defaults"]["scale"], { 1,1,1 });
	out.defaultRotate = ReadVector3(root["defaults"]["rotate"], { 0,0,0 });

	// Primitive 用パラメータ
	if (out.kind == PrefabKind::Primitive) {
		const JsonValue& pp = root["primitive"];
		auto& pr = out.primitiveParams;
		pr.primitiveType = static_cast<int>(pp["primitiveType"].AsInt(static_cast<int64_t>(pr.primitiveType)));
		if (pp["texturePath"].IsString()) pr.texturePath = pp["texturePath"].AsString();
		pr.color          = ReadVector4(pp["color"], pr.color);
		pr.blendMode      = static_cast<int>(pp["blendMode"].AsInt(static_cast<int64_t>(pr.blendMode)));
		pr.depthWrite     = pp["depthWrite"].AsBool(pr.depthWrite);
		pr.alphaReference = static_cast<float>(pp["alphaReference"].AsDouble(pr.alphaReference));
		pr.cullBackface   = pp["cullBackface"].AsBool(pr.cullBackface);
		pr.samplerMode    = static_cast<int>(pp["samplerMode"].AsInt(static_cast<int64_t>(pr.samplerMode)));
		pr.uvAutoScroll   = pp["uvAutoScroll"].AsBool(pr.uvAutoScroll);
		pr.uvScrollSpeed  = ReadVector2(pp["uvScrollSpeed"], pr.uvScrollSpeed);
		pr.uvOffset       = ReadVector2(pp["uvOffset"], pr.uvOffset);
		pr.uvScale        = ReadVector2(pp["uvScale"], pr.uvScale);
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

		out.colliderOffset = ReadVector3(col["offset"], { 0,0,0 });
		out.colliderRadius = static_cast<float>(col["radius"].AsDouble(1.0));
		out.colliderHalfExtents = ReadVector3(col["halfExtents"], { kDefaultColliderHalfExtent, kDefaultColliderHalfExtent, kDefaultColliderHalfExtent });
		out.colliderCapsuleRadius = static_cast<float>(col["capsuleRadius"].AsDouble(kDefaultCapsuleRadius));
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

	// ScoreValue（敵プレハブ用の撃破スコア。number or {value:int}）
	const JsonValue& svJ = root["scoreValue"];
	if (svJ.IsObject()) {
		out.scoreValue = static_cast<int>(svJ["value"].AsInt(static_cast<int64_t>(out.scoreValue)));
	} else if (svJ.IsNumber()) {
		out.scoreValue = static_cast<int>(svJ.AsInt(static_cast<int64_t>(out.scoreValue)));
	}

	// Bullet（弾プレハブ用の速度・寿命・ホーミング・貫通）
	const JsonValue& blJ = root["bullet"];
	if (blJ.IsObject()) {
		out.hasBullet            = true;
		out.bulletSpeed          = static_cast<float>(blJ["speed"].AsDouble(out.bulletSpeed));
		out.bulletLifetime       = static_cast<float>(blJ["lifetime"].AsDouble(out.bulletLifetime));
		out.bulletHomingStrength = static_cast<float>(blJ["homingStrength"].AsDouble(out.bulletHomingStrength));
		out.bulletStrongHomingStrength = static_cast<float>(blJ["strongHomingStrength"].AsDouble(out.bulletStrongHomingStrength));
		out.bulletColliderGrowth = static_cast<float>(blJ["colliderGrowth"].AsDouble(out.bulletColliderGrowth));
		out.bulletPenetrate      = blJ["penetrate"].AsBool(out.bulletPenetrate);
		out.bulletPenetrateDamageRate = static_cast<float>(blJ["penetrateDamageRate"].AsDouble(out.bulletPenetrateDamageRate));
		if (blJ["penetrateEffect"].IsString()) out.bulletPenetrateEffect = blJ["penetrateEffect"].AsString();
	}

	// Melee（近接攻撃プレハブ用：持続・オフセット・コンボ・本/持続あて）
	const JsonValue& meJ = root["melee"];
	if (meJ.IsObject()) {
		out.hasMelee             = true;
		out.meleeStartup         = static_cast<float>(meJ["startup"].AsDouble(out.meleeStartup));
		out.meleeActiveDuration  = static_cast<float>(meJ["activeDuration"].AsDouble(out.meleeActiveDuration));
		out.meleeOffset          = ReadVector3(meJ["offset"], out.meleeOffset);
		out.meleeComboWindow     = static_cast<float>(meJ["comboWindow"].AsDouble(out.meleeComboWindow));
		out.meleeRecovery        = static_cast<float>(meJ["recovery"].AsDouble(out.meleeRecovery));
		out.meleeCleanWindow     = static_cast<float>(meJ["cleanWindow"].AsDouble(out.meleeCleanWindow));
		out.meleeCleanMultiplier = static_cast<float>(meJ["cleanMultiplier"].AsDouble(out.meleeCleanMultiplier));
		out.meleeLateMultiplier  = static_cast<float>(meJ["lateMultiplier"].AsDouble(out.meleeLateMultiplier));
	}

	// Movement（敵プレハブ用：登場/移動の方法と速度）
	const JsonValue& mvJ = root["movement"];
	if (mvJ.IsObject()) {
		out.hasMovement        = true;
		out.movementType       = MovementTypeFromStr(mvJ["type"].AsString("SplineFollow"), out.movementType);
		out.moveSpeed          = static_cast<float>(mvJ["moveSpeed"].AsDouble(out.moveSpeed));
		out.hoverApproachSpeed = static_cast<float>(mvJ["hoverApproachSpeed"].AsDouble(out.hoverApproachSpeed));
		out.hoverHoldDuration  = static_cast<float>(mvJ["hoverHoldDuration"].AsDouble(out.hoverHoldDuration));
	}

	// Carrier（運び屋プレハブ用の子敵パラメータ）
	const JsonValue& caJ = root["carrier"];
	if (caJ.IsObject()) {
		out.hasCarrier               = true;
		out.carrierChildLifetimeSec  = static_cast<float>(caJ["childLifetimeSec"].AsDouble(out.carrierChildLifetimeSec));
		out.carrierChildWanderRadius = static_cast<float>(caJ["childWanderRadius"].AsDouble(out.carrierChildWanderRadius));
		out.carrierChildMoveSpeed    = static_cast<float>(caJ["childMoveSpeed"].AsDouble(out.carrierChildMoveSpeed));
	}

	// Charge（プレイヤープレハブ用のチャージ時間 + 連射間隔）
	const JsonValue& chJ = root["charge"];
	if (chJ.IsObject()) {
		out.hasCharge        = true;
		out.chargeStage1Time = static_cast<float>(chJ["stage1Time"].AsDouble(out.chargeStage1Time));
		out.chargeStage2Time = static_cast<float>(chJ["stage2Time"].AsDouble(out.chargeStage2Time));
		out.chargeFireRate   = static_cast<float>(chJ["fireRate"].AsDouble(out.chargeFireRate));
	}

	// Precision（プレイヤープレハブ用の精密射撃モード加算値）
	const JsonValue& prJ = root["precision"];
	if (prJ.IsObject()) {
		out.hasPrecision       = true;
		out.precisionSpeedAdd  = static_cast<float>(prJ["speedAdd"].AsDouble(out.precisionSpeedAdd));
		out.precisionHomingAdd = static_cast<float>(prJ["homingAdd"].AsDouble(out.precisionHomingAdd));
	}

	// Weapon（プレイヤープレハブ用のソケット追従武器）
	const JsonValue& weJ = root["weapon"];
	if (weJ.IsObject()) {
		out.hasWeapon     = true;
		out.weaponEnabled = weJ["enabled"].AsBool(out.weaponEnabled);
		if (weJ["modelDir"].IsString())  out.weaponModelDir  = weJ["modelDir"].AsString();
		if (weJ["modelFile"].IsString()) out.weaponModelFile = weJ["modelFile"].AsString();
		if (weJ["bone"].IsString())      out.weaponBone      = weJ["bone"].AsString();
		out.weaponOffsetTranslate = ReadVector3(weJ["translate"], out.weaponOffsetTranslate);
		out.weaponOffsetRotate    = ReadVector3(weJ["rotate"],    out.weaponOffsetRotate);
		out.weaponOffsetScale     = ReadVector3(weJ["scale"],     out.weaponOffsetScale);
	}

	// エフェクトスロット（スロット名 → エフェクト名）
	const JsonValue& efJ = root["effects"];
	if (efJ.IsObject()) {
		for (const auto& kv : efJ.AsObject()) {
			if (kv.second.IsString()) {
				out.effects[kv.first] = kv.second.AsString();
			}
		}
	}

	// 弾プレハブスロット（スロット名 → 弾プレハブ名）
	const JsonValue& bpJ = root["bulletPrefabs"];
	if (bpJ.IsObject()) {
		for (const auto& kv : bpJ.AsObject()) {
			if (kv.second.IsString()) {
				out.bulletPrefabs[kv.first] = kv.second.AsString();
			}
		}
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
	defaultsObj["scale"] = MakeVector3(def.defaultScale);
	defaultsObj["rotate"] = MakeVector3(def.defaultRotate);
	root["defaults"] = std::move(defaultsObj);

	if (def.kind == PrefabKind::Primitive) {
		const auto& pr = def.primitiveParams;
		JsonValue pp = JsonValue::MakeObject();
		pp["primitiveType"] = static_cast<int64_t>(pr.primitiveType);
		pp["texturePath"]   = pr.texturePath;
		pp["color"]         = MakeVector4(pr.color);
		pp["blendMode"]     = static_cast<int64_t>(pr.blendMode);
		pp["depthWrite"]    = pr.depthWrite;
		pp["alphaReference"] = static_cast<double>(pr.alphaReference);
		pp["cullBackface"]  = pr.cullBackface;
		pp["samplerMode"]   = static_cast<int64_t>(pr.samplerMode);
		pp["uvAutoScroll"]  = pr.uvAutoScroll;
		pp["uvScrollSpeed"] = MakeVector2(pr.uvScrollSpeed);
		pp["uvOffset"]      = MakeVector2(pr.uvOffset);
		pp["uvScale"]       = MakeVector2(pr.uvScale);
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
		colObj["offset"] = MakeVector3(def.colliderOffset);
		colObj["radius"] = static_cast<double>(def.colliderRadius);
		colObj["halfExtents"] = MakeVector3(def.colliderHalfExtents);
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
		blObj["strongHomingStrength"] = static_cast<double>(def.bulletStrongHomingStrength);
		blObj["colliderGrowth"] = static_cast<double>(def.bulletColliderGrowth);
		blObj["penetrate"]      = def.bulletPenetrate;
		blObj["penetrateDamageRate"] = static_cast<double>(def.bulletPenetrateDamageRate);
		blObj["penetrateEffect"] = def.bulletPenetrateEffect;
		root["bullet"] = std::move(blObj);
	}

	// Enemy/Boss のスコア（デフォルト 10 から外れていれば書き出す）
	if (def.scoreValue != kDefaultScoreValue) {
		root["scoreValue"] = static_cast<int64_t>(def.scoreValue);
	}

	if (def.hasMelee) {
		JsonValue meObj = JsonValue::MakeObject();
		meObj["startup"]         = static_cast<double>(def.meleeStartup);
		meObj["activeDuration"]  = static_cast<double>(def.meleeActiveDuration);
		meObj["offset"]          = MakeVector3(def.meleeOffset);
		meObj["comboWindow"]     = static_cast<double>(def.meleeComboWindow);
		meObj["recovery"]        = static_cast<double>(def.meleeRecovery);
		meObj["cleanWindow"]     = static_cast<double>(def.meleeCleanWindow);
		meObj["cleanMultiplier"] = static_cast<double>(def.meleeCleanMultiplier);
		meObj["lateMultiplier"]  = static_cast<double>(def.meleeLateMultiplier);
		root["melee"] = std::move(meObj);
	}

	if (def.hasMovement) {
		JsonValue mvObj = JsonValue::MakeObject();
		mvObj["type"]               = std::string(MovementTypeToStr(def.movementType));
		mvObj["moveSpeed"]          = static_cast<double>(def.moveSpeed);
		mvObj["hoverApproachSpeed"] = static_cast<double>(def.hoverApproachSpeed);
		mvObj["hoverHoldDuration"]  = static_cast<double>(def.hoverHoldDuration);
		root["movement"] = std::move(mvObj);
	}

	if (def.hasCarrier) {
		JsonValue caObj = JsonValue::MakeObject();
		caObj["childLifetimeSec"]  = static_cast<double>(def.carrierChildLifetimeSec);
		caObj["childWanderRadius"] = static_cast<double>(def.carrierChildWanderRadius);
		caObj["childMoveSpeed"]    = static_cast<double>(def.carrierChildMoveSpeed);
		root["carrier"] = std::move(caObj);
	}

	if (def.hasCharge) {
		JsonValue chObj = JsonValue::MakeObject();
		chObj["stage1Time"] = static_cast<double>(def.chargeStage1Time);
		chObj["stage2Time"] = static_cast<double>(def.chargeStage2Time);
		chObj["fireRate"]   = static_cast<double>(def.chargeFireRate);
		root["charge"] = std::move(chObj);
	}

	if (def.hasPrecision) {
		JsonValue prObj = JsonValue::MakeObject();
		prObj["speedAdd"]  = static_cast<double>(def.precisionSpeedAdd);
		prObj["homingAdd"] = static_cast<double>(def.precisionHomingAdd);
		root["precision"] = std::move(prObj);
	}

	if (def.hasWeapon) {
		JsonValue weObj = JsonValue::MakeObject();
		weObj["enabled"]   = def.weaponEnabled;
		weObj["modelDir"]  = def.weaponModelDir;
		weObj["modelFile"] = def.weaponModelFile;
		weObj["bone"]      = def.weaponBone;
		weObj["translate"] = MakeVector3(def.weaponOffsetTranslate);
		weObj["rotate"]    = MakeVector3(def.weaponOffsetRotate);
		weObj["scale"]     = MakeVector3(def.weaponOffsetScale);
		root["weapon"] = std::move(weObj);
	}

	// エフェクトスロット
	if (!def.effects.empty()) {
		JsonValue efObj = JsonValue::MakeObject();
		for (const auto& kv : def.effects) {
			efObj[kv.first] = kv.second;
		}
		root["effects"] = std::move(efObj);
	}

	// 弾プレハブスロット
	if (!def.bulletPrefabs.empty()) {
		JsonValue bpObj = JsonValue::MakeObject();
		for (const auto& kv : def.bulletPrefabs) {
			bpObj[kv.first] = kv.second;
		}
		root["bulletPrefabs"] = std::move(bpObj);
	}

	// ディレクトリ作成
	std::filesystem::path path(filePath);
	if (path.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
	}
	return JsonWriter::WriteFile(filePath, root, { true, kJsonIndentWidth });
}
