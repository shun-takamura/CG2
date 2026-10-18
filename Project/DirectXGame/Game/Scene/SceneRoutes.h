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
	// ポーズの「ステージセレクトに戻る」（⑩）と、ゲームオーバー・リザルトの「ステージ選択に戻る」。シーン名 STAGESELECT の HubScene がコクピットの起動を演じる
	// （18_HubTransitions.md ①「C で決まったこと」）。None ＝ TransitionManager::kRevealByScene
	inline constexpr TransitionType kStageSelectReveal = TransitionType::None;

	// HUB の開く側。別のシーンから HUB へ戻るときは「機体から降りる」で開く
	// （18_HubTransitions.md ②の IN、⑦）。None ＝ TransitionManager::kRevealByScene。
	// SceneFactory はこの表で「開く側がシーン」の道から来たら HubScene を真っ暗なコクピット（Entry::Disembark）から始める
	inline constexpr TransitionType kHubReveal = TransitionType::None;

	// 組み合わせの正本は 18_HubTransitions.md §0（HUB まわり）と 15_SceneTransition.md（羽根・墨）
	inline constexpr Route kRoutes[] = {
		// ⑧ タイトル ↔ HUB：白フェード（色と長さは TitleScene / HubScene が直前に FadeTransition::SetNextFade で決める）
		{ "TITLE",       "HUB",         { TransitionType::Fade,   TransitionType::Fade   } },
		{ "HUB",         "TITLE",       { TransitionType::Fade,   TransitionType::Fade   } },
		// ①② HUB ↔ ステージ選択は同じ HubScene の中（シーンを切り替えない）。
		// シーン名 STAGESELECT で始まった HubScene（⑩）からタイトルへ戻るときも白フェード
		{ "STAGESELECT", "TITLE",       { TransitionType::Fade,   TransitionType::Fade   } },
		// ③ ステージ選択 → ステージ：リフトで縦穴を降り、暗転せず同じ縦穴の StagePlay に差し替える（16_StageStart.md F2・F3）。
		// 覆う・開くともシーン（None / None）なので SceneManager は ChangeSceneImmediate で差し替える。
		// HubScene のシーン名は入口で HUB / STAGESELECT のどちらか
		{ "HUB",         "STAGEPLAY",   { TransitionType::None,   TransitionType::None   } },
		{ "STAGESELECT", "STAGEPLAY",   { TransitionType::None,   TransitionType::None   } },
		// ⑨ ステージ → リザルト / ゲームオーバー（死亡は羽根の着水、15_SceneTransition.md T4）
		{ "STAGEPLAY",   "RESULT",      { TransitionType::Stripe, TransitionType::Stripe } },
		{ "STAGEPLAY",   "GAMEOVER",    { TransitionType::FeatherLand, TransitionType::FeatherLand } },
		// ⑤⑥ リトライ（ゲームオーバーから / ポーズから）：羽根の飛翔
		{ "GAMEOVER",    "STAGEPLAY",   { TransitionType::FeatherRise, TransitionType::FeatherRise } },
		{ "STAGEPLAY",   "STAGEPLAY",   { TransitionType::FeatherRise, TransitionType::FeatherRise } },
		// ポーズの「ステージセレクトに戻る」：墨で覆い（進行を捨てて戻る遷移。2026-10-14 ストライプから変更）、ステージ選択の IN で開く
		{ "STAGEPLAY",   "STAGESELECT", { TransitionType::Ink,    kStageSelectReveal } },
		{ "STAGEPLAY",   "TITLE",       { TransitionType::Stripe, TransitionType::Stripe } },
		// ゲームオーバー・リザルトからはステージ選択へ戻る（⑩と同じくコクピットの起動で開く）。
		// 進行を捨てる遷移は墨、リザルトからは黒フェード
		{ "GAMEOVER",    "STAGESELECT", { TransitionType::Ink,    kStageSelectReveal } },
		{ "RESULT",      "STAGESELECT", { TransitionType::Fade,   kStageSelectReveal } },
		// ⑦ HUB へ戻る（今はステージからデバッグの F3 だけ）：墨。開く側は「機体から降りる」
		{ "STAGEPLAY",   "HUB",         { TransitionType::Ink,    kHubReveal } },
	};

	/// <summary>from → to の組み合わせを引く（表に無ければ kDefault）</summary>
	constexpr Transitions Find(std::string_view from, std::string_view to) {
		for (const Route& route : kRoutes) {
			if (route.from == from && route.to == to) return route.transitions;
		}
		return kDefault;
	}

} // namespace SceneRoutes
