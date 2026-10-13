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

	// ステージ選択（コクピット）の開く側。HUB からも、ポーズの「ステージセレクトに戻る」からも同じ IN で開く
	// （18_HubTransitions.md ①の IN：真っ暗から緑の灯りと UI が起動する）。
	// ①の IN ができたら TransitionManager::kRevealByScene に変える。それまでは仮にストライプ
	inline constexpr TransitionType kStageSelectReveal = TransitionType::Stripe;

	// HUB の開く側。ステージ・ゲームオーバー・リザルトから戻るときは「機体から降りる」で開く
	// （18_HubTransitions.md ②の IN、⑦）。できたら kRevealByScene に変える。それまでは仮にストライプ
	inline constexpr TransitionType kHubReveal = TransitionType::Stripe;

	// 組み合わせの正本は 18_HubTransitions.md §0（HUB まわり）と 15_SceneTransition.md（羽根・墨）
	inline constexpr Route kRoutes[] = {
		// ⑧ タイトル ↔ HUB：白フェード（色と長さは TitleScene / HubScene が直前に FadeTransition::SetNextFade で決める）
		{ "TITLE",       "HUB",         { TransitionType::Fade,   TransitionType::Fade   } },
		{ "HUB",         "TITLE",       { TransitionType::Fade,   TransitionType::Fade   } },
		// ① HUB → ステージ選択：乗り込み（HUB が演じる）→ 起動（ステージ選択が演じる）。
		// ①の OUT ができたら覆う側を TransitionManager::kCoverByScene に変える。それまでは仮にストライプ
		{ "HUB",         "STAGESELECT", { TransitionType::Stripe, kStageSelectReveal } },
		// ② ステージ選択 → HUB：UI のシャットダウン（ステージ選択が演じる）→ 機体から降りる。
		// ②の OUT ができたら覆う側を kCoverByScene に変える。それまでは仮にストライプ
		{ "STAGESELECT", "HUB",         { TransitionType::Stripe, kHubReveal } },
		// ③ ステージ選択 → ステージ：未定（ストライプを仮置き）
		{ "STAGESELECT", "STAGEPLAY",   { TransitionType::Stripe, TransitionType::Stripe } },
		// ⑨ ステージ → リザルト / ゲームオーバー（死亡は羽根の着水、15_SceneTransition.md T4）
		{ "STAGEPLAY",   "RESULT",      { TransitionType::Stripe, TransitionType::Stripe } },
		{ "STAGEPLAY",   "GAMEOVER",    { TransitionType::FeatherLand, TransitionType::FeatherLand } },
		// ⑤⑥ リトライ（ゲームオーバーから / ポーズから）：羽根の飛翔
		{ "GAMEOVER",    "STAGEPLAY",   { TransitionType::FeatherRise, TransitionType::FeatherRise } },
		{ "STAGEPLAY",   "STAGEPLAY",   { TransitionType::FeatherRise, TransitionType::FeatherRise } },
		// ポーズの「ステージセレクトに戻る」：ストライプで覆い、ステージ選択の IN で開く
		{ "STAGEPLAY",   "STAGESELECT", { TransitionType::Stripe, kStageSelectReveal } },
		{ "STAGEPLAY",   "TITLE",       { TransitionType::Stripe, TransitionType::Stripe } },
		// ⑦ HUB へ戻る：進行を捨てる遷移は墨、リザルトからは黒フェード。開く側は「機体から降りる」
		{ "STAGEPLAY",   "HUB",         { TransitionType::Ink,    kHubReveal } },
		{ "GAMEOVER",    "HUB",         { TransitionType::Ink,    kHubReveal } },
		{ "RESULT",      "HUB",         { TransitionType::Fade,   kHubReveal } },
	};

	/// <summary>from → to の組み合わせを引く（表に無ければ kDefault）</summary>
	constexpr Transitions Find(std::string_view from, std::string_view to) {
		for (const Route& route : kRoutes) {
			if (route.from == from && route.to == to) return route.transitions;
		}
		return kDefault;
	}

} // namespace SceneRoutes
