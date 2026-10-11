#pragma once
#include "BaseTransition.h"
#include <string_view>

/// <summary>
/// シーン遷移の組み合わせ表（どのシーンからどのシーンへ、どのトランジションで覆って開くか）。
/// ゲームの ChangeScene(行き先) とデバッグのシーン切り替え（TimeControler の Scene Switch）が
/// 同じ表を引くので、組み合わせを直すときはここだけ直せばよい。表に無い組み合わせは kDefault。
/// </summary>
namespace SceneRoutes {

	struct Transitions {
		TransitionType cover;   // 覆う側
		TransitionType reveal;  // 開く側（cover と同じなら同じトランジションが開く）
	};

	struct Route {
		std::string_view from;
		std::string_view to;
		Transitions transitions;
	};

	inline constexpr Transitions kDefault = { TransitionType::Stripe, TransitionType::Stripe };

	inline constexpr Route kRoutes[] = {
		// タイトルの扉に入る白フェード（色と長さは TitleScene が直前に FadeTransition::SetNextFade で決める）
		{ "TITLE",       "HUB",       { TransitionType::Fade,   TransitionType::Fade   } },
		// 墨で覆い、黒いストライプで開く（15_SceneTransition.md T3）
		{ "HUB",         "STAGEPLAY", { TransitionType::Ink,    TransitionType::Stripe } },
		{ "HUB",         "TITLE",     { TransitionType::Stripe, TransitionType::Stripe } },
		{ "STAGESELECT", "STAGEPLAY", { TransitionType::Stripe, TransitionType::Stripe } },
		{ "STAGEPLAY",   "RESULT",    { TransitionType::Stripe, TransitionType::Stripe } },
		// 死亡 → ゲームオーバー：羽根の着水（15_SceneTransition.md T4）
		{ "STAGEPLAY",   "GAMEOVER",  { TransitionType::FeatherLand, TransitionType::FeatherLand } },
		{ "STAGEPLAY",   "STAGEPLAY", { TransitionType::Stripe, TransitionType::Stripe } },
		{ "STAGEPLAY",   "TITLE",     { TransitionType::Stripe, TransitionType::Stripe } },
		{ "STAGEPLAY",   "HUB",       { TransitionType::Stripe, TransitionType::Stripe } },
		// リトライ：羽根の飛翔。着水で羽根が消えた所からよみがえって飛び立つ（15_SceneTransition.md T4）
		{ "GAMEOVER",    "STAGEPLAY", { TransitionType::FeatherRise, TransitionType::FeatherRise } },
		{ "GAMEOVER",    "HUB",       { TransitionType::Stripe, TransitionType::Stripe } },
		{ "RESULT",      "HUB",       { TransitionType::Stripe, TransitionType::Stripe } },
	};

	/// <summary>from → to の組み合わせを引く（表に無ければ kDefault）</summary>
	constexpr Transitions Find(std::string_view from, std::string_view to) {
		for (const Route& route : kRoutes) {
			if (route.from == from && route.to == to) return route.transitions;
		}
		return kDefault;
	}

} // namespace SceneRoutes
