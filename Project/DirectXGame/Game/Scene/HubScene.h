#pragma once
#include "GameScene.h"
#include "VerticalMenu.h"
#include "Hub/HubBoardingSequence.h"
#include "Hub/HubDisembarkSequence.h"
#include "Hub/HubEntrySequence.h"
#include "Hub/CockpitReveal.h"
#include "Hub/HubLoadoutSequence.h"
#include "StageStart/StageStartGeometry.h"
#include "AttractMode.h"
#include "Vector2.h"
#include <memory>

class Camera;
class Skybox;
class Object3DInstance;
class HubMechModel;
class HubTemple;
class HatchSign;
class CockpitLighting;
class CockpitConsole;
class HubLiftDescent;
class HubEtherBet;
class LoadoutAttachments;
class LoadoutScreen;
class StageStartSkip;

/// <summary>
/// ハブシーン（神殿。17_HubScene.md）。**ステージ選択（コクピット）もこのシーンの中**（2026-10-13 統合。18 番 ①「C で決まったこと」）。
/// 状態のまとめ役で、中身は Hub/ の部品がする：
///   入場（タイトルから。HubEntrySequence：白フェードが晴れる間に時計の門の扉の前から待機の視点まで歩く。18 番 ⑧）
///   → 待機（足場から機体を見る。メニューは「搭乗」だけ）
///   → 乗り込み（HubBoardingSequence。18 番 ①の OUT）
///   → コクピット（CockpitReveal の時間軸で、表示板 HatchSign が LOCKED → 六角形で外が透ける CockpitLighting
///      → ラジアルブラー → 文字のステージ選択 CockpitConsole。①の IN）
///   → 「HUB に戻る」でシャットダウン（②の OUT）→ 降りる（HubDisembarkSequence。②の IN）→ 待機
///   → 「ステージ1」で黒フェードして器の間（HubEtherBet。エーテルを賭けて純度を決める。19 番 §4.7）→ 黒フェードでコクピットへ戻る
///   → 「機体設定」でカメラを外へ切り替え、神殿をノイズで消して装備を選ぶ（HubLoadoutSequence / LoadoutScreen / LoadoutAttachments。20_Loadout.md）
///     → 戻るとノイズで神殿を戻してコクピットへ
///   → 決めたら機体ごと八角形のリフトで縦穴を降りる（HubLiftDescent。③の OUT、16_StageStart.md F2）→ StagePlay へ差し替え
/// 入口：TITLE から＝待機／シーン名 STAGESELECT（ポーズの「ステージセレクトに戻る」⑩）＝コクピットの起動から／
///   別のシーンから HUB へ（⑦。今はステージからデバッグの F3）＝真っ暗なコクピットに灯りが点いて（CockpitReveal の帰着）から降りる。
/// キャンセルで白フェードしてタイトルへ（18 番 ⑧）。ゲーム終了はタイトルの責務に一本化している。
/// 神殿（17 番 H1・H2）は HubTemple（天球儀の聖堂・鏡の大理石の床）。丸窓から見える空はタイトルの空。
/// 調整値は Resources/Json/Tuning/Hub.json（ImGui の「Hub Tuning」で保存 / 読み直し）。
/// </summary>
class HubScene : public GameScene {
public:
	enum class Entry {
		Temple,      // 回廊で待機から（タイトルから）
		Cockpit,     // コクピットの起動から（シーン名 STAGESELECT）
		Disembark,   // 真っ暗なコクピットから灯りを点けて降りる（⑦。SceneFactory が前のシーンで決める）
	};

	/// <summary>入口を受け取る（Initialize で入口に合わせた状態から始める）</summary>
	/// <param name="entry">入口（神殿で待機・コクピットの起動・真っ暗なコクピットから降りる）</param>
	explicit HubScene(Entry entry = Entry::Temple);
	~HubScene() override;

	/// <summary>カメラ・空・機体・神殿・リフト・コクピットの部品・器の間・機体設定を作り、調整値を読んで入口の状態から始める</summary>
	void Initialize() override;
	/// <summary>BGM・ラジアルブラー・自分が点けたノイズを止め、コクピットの灯り・表示板・器の間を片付ける</summary>
	void Finalize() override;
	/// <summary>今の状態を進め、見え方（視点・ハッチ・光・床の穴）を反映する（トランジションで覆われている間は止める）</summary>
	void Update() override;
	/// <summary>
	/// 表示板・床の反射を RT へ描いてから、空（機体設定では暗い背景）→ 神殿・リフト・機体 → 鏡の床 → 表示板 → UI の順に描く。
	/// 器の間の間は器の間だけを描く
	/// </summary>
	void Draw() override;

	/// <summary>今のカメラを取得</summary>
	/// <returns>器の間の間は器の間のカメラ、ほかはシーンのカメラ</returns>
	Camera* GetCamera() override;

	/// <summary>発光を足すか（トランジションの上に光が乗らないよう、遷移中は出さない）</summary>
	/// <returns>発光してよく、コクピットの灯りと表示板があれば true</returns>
	bool HasBloomSources() const override;
	/// <summary>機体の光る部位・表示板・リフト・装備（器の間の間は器の間）をブルームのマスクへ描く</summary>
	void DrawBloomSources() override;

	/// <summary>機体設定のノイズを機体だけ・機体以外にかけるため、機体と装備を ID マスクに描くか</summary>
	/// <returns>機体設定でマスク付きのノイズを出している間 true</returns>
	bool HasExtraIdPassObjects() const override;
	/// <summary>ID マスクへ描く（付け替えは替えた部分だけ、神殿を消す・戻す間は機体全体と装備）</summary>
	void DrawExtraIdPassObjects() override;

	/// <summary>調整値の ImGui（Game.cpp の「Hub Tuning」窓から呼ぶ）</summary>
	void OnImGuiTuning();

private:
	enum class State {
		Entering,    // タイトルから入って、時計の門の扉の前から待機の視点まで歩く
		Waiting,     // 足場で待機
		Boarding,    // 乗り込み
		Cockpit,     // コクピット（起動・ステージ選択・シャットダウン）
		Disembarking,  // 降りる（機体に背を向けて乗るときと同じ道を戻り、振り返って待機の視点へ）
		Descending,  // ③ ステージを決めて、機体ごとリフトで縦穴を降りる（コクピットの中の視点のまま）
		Betting,     // 器の間でエーテルを賭けて純度を決める（19 番 §4.7。黒フェードで行き来する。シーンは切り替えない）
		Loadout,     // 機体設定（装備画面。20 番）。コクピットから外へカメラを切り替え、神殿をノイズで消して機体と UI だけにする
	};

	/// <summary>タイトルへ戻る（白フェード）</summary>
	void ChangeToTitle();

	/// <summary>シーンと各部品の調整値を Hub.json から読む（無い・読めなければ今の値のまま）</summary>
	void LoadTuning();
	/// <summary>シーンと各部品の調整値を Hub.json へ書き出す</summary>
	void SaveTuning() const;

	/// <summary>待機のメニュー（「搭乗」で乗り込み、キャンセル・「タイトルに戻る」でタイトルへ）。デモモードは「搭乗」を自動で選ぶ</summary>
	void UpdateWaiting();
	/// <summary>コクピットの起動・ステージ選択・シャットダウンを進める（ステージ1 → 器の間、機体設定、HUB に戻る）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateCockpit(float dt);
	/// <summary>降りる時間軸を進める（何か押したら降り終わった所まで飛ばす）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateDisembarking(float dt);
	/// <summary>降り終わった所（待機の視点）へ。降り終わり・スキップの両方から</summary>
	void FinishDisembark();
	/// <summary>乗り込み・HUB へ戻る所のスキップ（何か押した）</summary>
	/// <returns>何か押したら true。デモモード中・白フェード中は false</returns>
	bool IsSkipPressed() const;
	/// <summary>タイトルからの入場を進め、歩き終えたら待機へ（入場中は入力を読まない）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateEntering(float dt);
	/// <summary>リフトの降下を進め、先読みが済んだら縦穴の状態を渡して StagePlay へ差し替える（LSHIFT 長押しで雲の上へスキップ）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateDescending(float dt);
	/// <summary>③：ステージを決めてリフトの降下を始める（StagePlay の先読みも始める）</summary>
	void StartDescent();
	/// <summary>器の間（19 番 §4.7）へ黒フェードで向かう。シーンは切り替えず、フェードが覆い切った所で EnterBet が中身を差し替える</summary>
	void StartBet();
	/// <summary>器の間に入る（灯り・光・ブラーを器の間用にし、StagePlay の先読みを始める）。フェードが覆い切った所で呼ばれる</summary>
	void EnterBet();
	/// <summary>器の間を進め、決定・キャンセルで黒フェードして戻る</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateBetting(float dt);
	/// <summary>器の間から黒フェードで戻り始める（覆い切った所で ExitBet）</summary>
	/// <param name="descend">決定して戻るなら true（戻ったらリフトの降下へ）</param>
	void LeaveBet(bool descend);
	/// <summary>器の間からコクピットの中へ戻す（カメラ・IBL・フォグを戻す）。フェードが覆い切った所で呼ばれる</summary>
	/// <param name="descend">true ならそのままリフトの降下を始める</param>
	void ExitBet(bool descend);
	/// <summary>機体設定（20 番）に入る：コクピットの「機体設定」から。ノイズの山でカメラを外へ切り替える</summary>
	void StartLoadout();
	/// <summary>機体設定の時間軸と装備画面の入力（付け替え・段階・戻る・タブのカメラ・機体を回す）を進め、終われば コクピットへ戻る</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateLoadout(float dt);
	/// <summary>機体設定の間だけノイズ（GlitchEffect）を出す。ほかの状態では自分が点けた分だけ切る</summary>
	void ApplyGlitch();

	/// <summary>今の見え方をカメラ・ハッチ・光・コクピットへ反映する</summary>
	void ApplyPose();
	/// <summary>コクピットに入って起動を始める（視点は乗り込みの最後の姿勢）</summary>
	void EnterCockpit();
	/// <summary>⑦：閉じたコクピットの真っ暗から灯りを点け、終わったら降りる</summary>
	void EnterArrive();
	/// <summary>UI の仮想解像度（メニュー・コンソールの配置用。描画解像度ではない）</summary>
	/// <returns>UI の幅と高さ [px]</returns>
	Vector2 GetScreenSize() const;
	/// <summary>発光（コクピットの部位・表示板・UI の文字）を出してよいか（トランジションの上に光が乗らないよう、遷移中は出さない）</summary>
	/// <returns>トランジション中でなければ true</returns>
	bool IsGlowAllowed() const;

	Entry entry_ = Entry::Temple;
	State state_ = State::Waiting;

	std::unique_ptr<Camera> camera_;
	std::unique_ptr<Skybox> skybox_;
	std::unique_ptr<HubMechModel> mech_;
	HubBoardingSequence boarding_;
	HubDisembarkSequence disembark_;
	HubEntrySequence entrySequence_;
	CockpitReveal reveal_;
	std::unique_ptr<HatchSign> hatchSign_;
	std::unique_ptr<CockpitLighting> cockpitLighting_;
	std::unique_ptr<CockpitConsole> console_;
	// 神殿（回廊と、手のひらの手前へ伸ばした足場もこの中。待機の視点は足場の上）
	std::unique_ptr<HubTemple> temple_;
	// ③ のリフト・固定台・縦穴（寸法は StageStartGeometry.json）
	StageStartGeometry stageGeometry_;
	std::unique_ptr<HubLiftDescent> liftDescent_;
	// デバッグ：降下の最後に StagePlay へ差し替えるか（ImGui で切ると降り続けて調整できる）
	bool descentChangesScene_ = true;
	VerticalMenu menu_;
	// デモモード（AttractMode）：待機の「搭乗」・ステージ選択の項目を自動で選ぶ
	AttractMenuTimer attractMenu_;
	// 乗り込んでコクピットに入った（デモモードはステージ1へ）。STAGESELECT の入口（ポーズから）なら false（HUB に戻る）
	bool cockpitFromBoarding_ = false;
	// コクピットの計器が回る経過秒
	float cockpitTime_ = 0.0f;
	// ステージへ向かう（ChangeScene を呼んだ）。以後は入力を読まない
	bool leaving_ = false;
	// リフトの降下中の LSHIFT 長押しで雲の上へスキップ（16 番 F5）
	std::unique_ptr<StageStartSkip> skip_;
	// 器の間（賭ける画面）
	std::unique_ptr<HubEtherBet> etherBet_;
	bool betLeaving_ = false;          // 器の間から戻る黒フェードの途中
	bool stagePreloadStarted_ = false; // StagePlay の先読みを器の間に入った所で始めた
	// 機体設定（20 番）
	HubLoadoutSequence loadoutSequence_;
	HubLoadoutSequence::View loadoutView_{};
	std::unique_ptr<LoadoutScreen> loadoutScreen_;
	std::unique_ptr<LoadoutAttachments> loadoutAttachments_;
	float loadoutTime_ = 0.0f;         // 背景の輪を回す経過秒
	bool loadoutDragging_ = false;     // UI の外でドラッグして機体を回している
	int loadoutTab_ = 0;               // カメラを合わせているタブ
	int loadoutSwapTab_ = 0;           // 付け替えのノイズをかける部分（替えたタブ）
	Vector2 loadoutDragLast_{};
	bool glitchOwned_ = false;         // ノイズを点けたのはこのシーン（ImGui で試している分は切らない）

	// 調整値（Hub.json）
	float whiteFadeSeconds_ = 0.8f;   // タイトルへ戻る白フェードの、白くなるまで
	float whiteHoldSeconds_ = 0.3f;   // 同・白のまま待つ秒数
	float fovYDegrees_ = 60.0f;       // 一人称の縦の画角
	float sunIntensity_ = 1.0f;       // 外の光（乗り込みでハッチが閉じるにつれて弱め、外が透けると戻す）
	float betFadeSeconds_ = 0.5f;     // 器の間へ行き来する黒フェードの、黒くなるまで
	float betHoldSeconds_ = 0.2f;     // 同・黒のまま待つ秒数
};
