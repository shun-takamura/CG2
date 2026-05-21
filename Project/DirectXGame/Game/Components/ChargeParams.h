#pragma once

/// <summary>
/// プレイヤープレハブ用チャージパラメータ。
/// 1段階目・2段階目のチャージ完了までの時間を保持する。
/// Inspector で編集 → "Save as Prefab" でプレハブ JSON に反映される。
/// </summary>
struct ChargeParams {
	bool  enabled    = false;
	float stage1Time = 3.0f; // 1段階目完了までの秒数
	float stage2Time = 6.0f; // 2段階目完了までの秒数（合計時間）
};
