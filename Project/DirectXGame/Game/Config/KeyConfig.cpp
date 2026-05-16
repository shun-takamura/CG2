#include "KeyConfig.h"

#include "GameActions.h"
#include "InputAction.h"
#include "PhysicalBinding.h"
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include "Log.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <Windows.h>
#include <XInput.h>

#include <filesystem>

namespace {
	constexpr const char* kDefaultPath = "Resources/Json/Setting/keyconfig.default.json";
	constexpr const char* kUserPath    = "Resources/Json/Setting/keyconfig.user.json";

	// 1行で「アクション名, スロット0, スロット1」をバインドするヘルパー
	void Set(InputActionMap& map, Action a, PhysicalBinding primary, PhysicalBinding secondary = {}) {
		map.Bind(static_cast<int>(a), 0, primary);
		map.Bind(static_cast<int>(a), 1, secondary);
	}

	// バインド設定 -> JSON
	JsonValue BuildBindingsJson(const InputActionMap& map) {
		JsonValue bindings = JsonValue::MakeObject();
		for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
			Action a = static_cast<Action>(i);
			const auto& slots = map.GetSlots(i);

			JsonValue arr = JsonValue::MakeArray();
			for (const auto& b : slots.bindings) {
				std::string s = b.ToString();
				if (!s.empty()) {
					arr.Push(JsonValue(std::move(s)));
				}
			}
			bindings[std::string(GetActionName(a))] = std::move(arr);
		}
		return bindings;
	}

	// JSON -> バインド設定
	void ApplyBindingsJson(const JsonValue& bindings, InputActionMap& map) {
		if (!bindings.IsObject()) return;
		const auto& obj = bindings.AsObject();
		for (const auto& [name, value] : obj) {
			Action a = ActionFromName(name);
			if (a == Action::Count) continue;
			if (!value.IsArray()) continue;
			const auto& arr = value.AsArray();
			for (size_t slot = 0; slot < InputActionMap::kSlotCount; ++slot) {
				if (slot < arr.size() && arr[slot].IsString()) {
					map.Bind(static_cast<int>(a), slot,
						PhysicalBinding::FromString(arr[slot].AsString()));
				} else {
					map.Unbind(static_cast<int>(a), slot);
				}
			}
		}
	}

	bool TryLoadFile(const char* path, InputActionMap& map, KeyConfig::Options& options) {
		if (!std::filesystem::exists(path)) return false;
		auto result = JsonParser::ParseFile(path);
		if (!result.success) {
			Log(std::string("[KeyConfig] Parse error in ") + path + ": " + result.errorMessage + "\n");
			return false;
		}
		const JsonValue& root = result.value;
		ApplyBindingsJson(root["bindings"], map);
		if (root["options"]["precisionAimMode"].IsString()) {
			options.precisionAimMode = root["options"]["precisionAimMode"].AsString();
		}
		return true;
	}
}

const char* KeyConfig::GetDefaultFilePath() { return kDefaultPath; }
const char* KeyConfig::GetUserFilePath()    { return kUserPath; }

void KeyConfig::ApplyHardcodedDefaults(InputActionMap& map) {
	map.Resize(static_cast<size_t>(Action::Count));

	// ===== ゲームプレイ =====
	Set(map, Action::Fire,
		PhysicalBinding::Mouse(MouseCode::Left),
		PhysicalBinding::Gamepad(GamepadCode::RT));
	Set(map, Action::PrecisionAim,
		PhysicalBinding::Mouse(MouseCode::Right),
		PhysicalBinding::Gamepad(GamepadCode::LT));
	Set(map, Action::MeleeStrong,
		PhysicalBinding::Keyboard(DIK_Q),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_Y));
	Set(map, Action::MeleeWeak,
		PhysicalBinding::Keyboard(DIK_E),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_B));
	Set(map, Action::Dodge,
		PhysicalBinding::Keyboard(DIK_SPACE),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_A));
	Set(map, Action::Heal,
		PhysicalBinding::Keyboard(DIK_R),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_X));
	Set(map, Action::Ultimate,
		PhysicalBinding::Keyboard(DIK_F),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_RIGHT_THUMB));
	Set(map, Action::LockCamButton,
		PhysicalBinding::Keyboard(DIK_TAB),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_LEFT_SHOULDER));
	Set(map, Action::Pause,
		PhysicalBinding::Keyboard(DIK_ESCAPE),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_START));

	// ===== メニュー =====
	Set(map, Action::MenuConfirm,
		PhysicalBinding::Keyboard(DIK_RETURN),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_A));
	Set(map, Action::MenuCancel,
		PhysicalBinding::Keyboard(DIK_BACK),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_B));
	Set(map, Action::MenuUp,
		PhysicalBinding::Keyboard(DIK_UP),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_DPAD_UP));
	Set(map, Action::MenuDown,
		PhysicalBinding::Keyboard(DIK_DOWN),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_DPAD_DOWN));
	Set(map, Action::MenuLeft,
		PhysicalBinding::Keyboard(DIK_LEFT),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_DPAD_LEFT));
	Set(map, Action::MenuRight,
		PhysicalBinding::Keyboard(DIK_RIGHT),
		PhysicalBinding::Gamepad(XINPUT_GAMEPAD_DPAD_RIGHT));
}

void KeyConfig::LoadAndApply(InputActionMap& map, Options& options) {
	ApplyHardcodedDefaults(map);

	// user → default の順で試す。先に当たったほうで上書きする。
	if (TryLoadFile(kUserPath, map, options)) {
		Log("[KeyConfig] Loaded user config\n");
		return;
	}
	if (TryLoadFile(kDefaultPath, map, options)) {
		Log("[KeyConfig] Loaded default config\n");
		return;
	}
	Log("[KeyConfig] Using hardcoded defaults (no config file found)\n");
}

bool KeyConfig::SaveUser(const InputActionMap& map, const Options& options) {
	// 保存先ディレクトリを作成
	std::filesystem::path path(kUserPath);
	if (path.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
	}

	JsonValue root = JsonValue::MakeObject();
	root["profile"] = "user";
	root["bindings"] = BuildBindingsJson(map);

	JsonValue opts = JsonValue::MakeObject();
	opts["precisionAimMode"] = options.precisionAimMode;
	root["options"] = std::move(opts);

	return JsonWriter::WriteFile(kUserPath, root, { true, 2 });
}
