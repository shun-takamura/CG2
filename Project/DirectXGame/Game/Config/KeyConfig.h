#pragma once

#include <string>

class InputActionMap;

/// <summary>
/// キーコンフィグの永続化と適用。
/// 配置:
///   Resources/Json/Setting/keyconfig.default.json  ... 配布時の初期設定
///   Resources/Json/Setting/keyconfig.user.json     ... ユーザーが変更したもの
///
/// 起動時: user → 無ければ default を読む。両方無ければコード組み込みデフォルトを使う。
/// リバインド時: user に書き出す。
/// </summary>
class KeyConfig {
public:
	struct Options {
		// 精密射撃モードの切替方式（"hold" または "toggle"）
		std::string precisionAimMode = "hold";
	};

public:
	/// <summary>
	/// アクション数を確保し、コード組み込みのデフォルトバインドを map に書き込む。
	/// </summary>
	/// <param name="map">書き込み先のアクションマップ</param>
	static void ApplyHardcodedDefaults(InputActionMap& map);

	/// <summary>
	/// user → default の順でファイルを試し、見つかったものを map に適用する。
	/// どちらも無ければ ApplyHardcodedDefaults を呼ぶ。Options も同時に読む。
	/// </summary>
	/// <param name="map">書き込み先のアクションマップ</param>
	/// <param name="options">読んだオプションの書き込み先</param>
	static void LoadAndApply(InputActionMap& map, Options& options);

	/// <summary>
	/// 現在の map / options を keyconfig.user.json に書き出す。
	/// </summary>
	/// <param name="map">書き出すアクションマップ</param>
	/// <param name="options">書き出すオプション</param>
	/// <returns>書き出せたら true</returns>
	static bool SaveUser(const InputActionMap& map, const Options& options);

	//====================
	// ファイルパス
	//====================
	/// <summary>配布時の初期設定ファイルのパスを取得する</summary>
	/// <returns>"Resources/Json/Setting/keyconfig.default.json"</returns>
	static const char* GetDefaultFilePath();
	/// <summary>ユーザーが変更した設定ファイルのパスを取得する</summary>
	/// <returns>"Resources/Json/Setting/keyconfig.user.json"</returns>
	static const char* GetUserFilePath();
};
