#include "StageStartGeometry.h"

#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include <filesystem>
#include <optional>

namespace {
	void ReadFloat(const JsonValue& node, float& value) {
		value = static_cast<float>(node.AsDouble(value));
	}

	void ReadVector3(const JsonValue& node, Vector3& value) {
		if (!node.IsArray() || node.Size() < 3) return;
		value = { static_cast<float>(node[0].AsDouble(value.x)), static_cast<float>(node[1].AsDouble(value.y)),
			static_cast<float>(node[2].AsDouble(value.z)) };
	}

	void ReadFloatPair(const JsonValue& node, float (&value)[2]) {
		if (!node.IsArray() || node.Size() < 2) return;
		value[0] = static_cast<float>(node[0].AsDouble(value[0]));
		value[1] = static_cast<float>(node[1].AsDouble(value[1]));
	}

	std::optional<StageStartHandoff> gPendingHandoff;
}

bool StageStartGeometry::Load(const std::string& path) {
	if (!std::filesystem::exists(path)) return false;
	const auto result = JsonParser::ParseFile(path);
	if (!result.success) return false;
	const JsonValue& root = result.value;

	const JsonValue& octagon = root["octagon"];
	ReadFloat(octagon["halfWidth"], octagonHalfWidth);
	ReadFloat(octagon["chamfer"], octagonChamfer);

	const JsonValue& lift = root["lift"];
	ReadFloat(lift["thickness"], liftThickness);
	ReadFloat(lift["rimWidth"], liftRimWidth);

	const JsonValue& shaft = root["shaft"];
	ReadFloat(shaft["unitHeight"], shaftUnitHeight);
	ReadFloat(shaft["topCollarDepth"], shaftTopCollarDepth);
	ReadFloatPair(shaft["bandY"], shaftBandY);

	const JsonValue& catapult = root["catapult"];
	ReadFloat(catapult["ceilingY"], catapultCeilingY);
	ReadFloat(catapult["shaftBaseY"], catapultShaftBaseY);
	ReadFloat(catapult["railStartZ"], catapultRailStartZ);
	ReadFloat(catapult["tunnelEndZ"], catapultTunnelEndZ);
	ReadFloat(catapult["exitLightZ"], catapultExitLightZ);
	const JsonValue& clampNode = catapult["clamps"];
	for (size_t i = 0; i < clamps.size() && clampNode.IsArray() && i < clampNode.Size(); ++i) {
		ReadVector3(clampNode[i], clamps[i]);
	}
	ReadFloatPair(catapult["railX"], railX);
	ReadFloat(catapult["grooveHalfWidth"], grooveHalfWidth);
	ReadFloat(catapult["grooveDepth"], grooveDepth);
	ReadFloat(catapult["liftGrooveStartZ"], liftGrooveStartZ);
	const JsonValue& anchorNode = catapult["cableAnchors"];
	for (size_t i = 0; i < cableAnchors.size() && anchorNode.IsArray() && i < anchorNode.Size(); ++i) {
		ReadVector3(anchorNode[i], cableAnchors[i]);
	}
	neonMaterial = catapult["neonMaterial"].AsString(neonMaterial);
	const JsonValue& neonRows = catapult["neonRows"];
	const JsonValue& rowMaterials = neonRows["materials"];
	const JsonValue& rowZ = neonRows["z"];
	if (rowMaterials.IsArray() && rowZ.IsArray() && rowMaterials.Size() == rowZ.Size()) {
		neonRowMaterials.clear();
		neonRowZ.clear();
		for (size_t i = 0; i < rowMaterials.Size(); ++i) {
			neonRowMaterials.push_back(rowMaterials[i].AsString(""));
			neonRowZ.push_back(static_cast<float>(rowZ[i].AsDouble(0.0)));
		}
	}

	const JsonValue& lcd = catapult["lcd"];
	const JsonValue& lcdMaterialNode = lcd["materials"];
	for (size_t i = 0; i < lcdMaterials.size() && lcdMaterialNode.IsArray() && i < lcdMaterialNode.Size(); ++i) {
		lcdMaterials[i] = lcdMaterialNode[i].AsString(lcdMaterials[i]);
	}
	ReadFloatPair(lcd["uPerMeterZ"], lcdUPerMeterZ);
	const JsonValue& textures = lcd["textures"];
	lcdTextureUnable = textures["unable"].AsString(lcdTextureUnable);
	lcdTextureReady = textures["ready"].AsString(lcdTextureReady);
	return true;
}

void StageStartHandoff::Store(const StageStartHandoff& handoff) {
	gPendingHandoff = handoff;
}

bool StageStartHandoff::Take(StageStartHandoff& out) {
	if (!gPendingHandoff) return false;
	out = *gPendingHandoff;
	gPendingHandoff.reset();
	return true;
}

bool StageStartHandoff::HasPending() {
	return gPendingHandoff.has_value();
}
