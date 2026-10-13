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

	// ステージ選択（コクピット）の開く側。ステージ選択は HUB のシーンの中なので、別のシーンから来るのは
	// ポーズの「ステージセレクトに戻る」（⑩）だけ。シーン名 STAGESELECT の HubScene がコクピットの起動を演じる
	// （18_HubTransitions.md ①「C で決まったこと」）。None ＝ TransitionManager::kRevealByScene
	inline constexpr TransitionType kStageSelectReveal = TransitionType::None;

	// HUB の開く側。ステージ・ゲームオーバー・リザルトから戻るときは「機体から降りる」で開く
	// （18_HubTransitions.md ②の IN、⑦）。できたら kRevealByScene に変える。それまでは仮にストライプ
	inline constexpr TransitionType kHubReveal = TransitionType::Stripe;

	// 組み合わせの正本は 18_HubTransitions.md §0（HUB まわり）と 15_SceneTransition.md（羽根・墨）
	inline constexpr Route kRoutes[] = {
		// ⑧ タイトル ↔ HUB：白フェード（色と長さは TitleScene / HubScene が直前に FadeTransition::SetNextFade で決める）
		{ "TITLE",       "HUB",         { TransitionType::Fade,   TransitionType::Fade   } },
		{ "HUB",         "TITLE",       { TransitionType::Fade,   TransitionType::Fade   } },
		// ①② HUB ↔ ステージ選択は同じ HubScene の中（シーンを切り替えない）。
		// シーン名 STAGESELECT で始まった HubScene（⑩）からタイトルへ戻るときも白フェード
		{ "STAGESELECT", "TITLE",       { TransitionType::Fade,   TransitionType::Fade   } },
		// ③ ステージ選択 → ステージ：未定（ストライプを仮置き）。HubScene のシーン名は入口で HUB / STAGESELECT のどちらか
		{ "HUB",         "STAGEPLAY",   { TransitionType::Stripe, TransitionType::Stripe } },
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
