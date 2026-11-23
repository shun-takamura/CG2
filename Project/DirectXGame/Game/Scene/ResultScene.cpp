#include "ResultScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "SceneManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "DirectXCore.h"
#include "TextRenderer.h"
#include "SpriteInstance.h"
#include "SpriteManager.h"
#include "SoundManager.h"
#include "UIPointer.h"
#include "Score/ScoreManager.h"
#include "Score/LootCatalog.h"
#include "Score/LootLedger.h"
#include "Score/EtherState.h"
#include "Score/StageClearRecord.h"
#include "Score/GameSession.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string_view>

namespace {
	// ----- 出し方（実時間 [s]） -----
	constexpr float kRecordAppear = 0.25f;   // 左上の記録が出始める
	constexpr float kPanelAppear = 0.55f;    // 右上の所持・右のパネルが出始める
	constexpr float kAppearTime = 0.35f;     // 出てくる長さ（横から滑り込む）
	constexpr float kSlideDistance = 40.0f;  // 滑り込む距離
	constexpr float kSlotStart = 0.9f;       // 最初のマスが数え始める
	constexpr float kSlotStagger = 0.12f;    // マスごとのずれ
	constexpr float kSlotCountTime = 0.35f;  // 1 マスが 0 から数え上がる時間
	constexpr float kEtherStart = 0.9f;      // 獲得エーテルが数え始める
	constexpr float kEtherCountTime = 1.0f;
	constexpr float kZoomTime = 10.0f;       // 背景が寄りきるまで
	constexpr float kZoomAmount = 0.05f;     // 寄る量（1.0 → 1.05 倍）
	constexpr float kSoldPopupTime = 1.6f;   // 売ったときの「+N」を出す長さ

	// ----- 配置（UI 座標 1600×900） -----
	constexpr float kRecordX = 40.0f;
	constexpr float kRecordY = 40.0f;
	constexpr float kRecordW = 520.0f;
	constexpr float kRecordH = 300.0f;
	constexpr float kEarnedY = kRecordY + kRecordH + 12.0f;   // 獲得エーテル（記録の下）
	constexpr float kEarnedH = 72.0f;
	constexpr float kPanelX = 1020.0f;       // 右のパネルの左端
	constexpr float kPanelW = 540.0f;
	constexpr float kHandY = 40.0f;          // 所持エーテル（右上）
	constexpr float kHandH = 80.0f;
	constexpr float kPanelY = 140.0f;
	constexpr float kPanelH = 650.0f;
	constexpr float kSlotSize = 78.0f;
	constexpr float kSlotGap = 8.0f;
	constexpr int kSlotColumns = 6;
	constexpr int kStageSlotRows = 2;        // ステージ報酬のマスの段（越えたら段を足す）
	constexpr int kPartBreakSlotRows = 1;
	constexpr float kButtonH = 44.0f;
	constexpr int kHandDisplayMax = 99999;   // 所持の表示の上限（賭ける画面と同じ）
	// レア度 2（緑）
	constexpr int kRarityGreen = 2;
	// レア度 3（青）
	constexpr int kRarityBlue = 3;
	// レア度 4（紫）
	constexpr int kRarityPurple = 4;
	// レア度 5（橙）
	constexpr int kRarityOrange = 5;
	// レア度 2 の色の R と B
	constexpr float kRarityGreenColorRB = 0.55f;
	// レア度 2 の色の G
	constexpr float kRarityGreenColorG = 0.95f;
	// レア度 3 の色の R
	constexpr float kRarityBlueColorR = 0.45f;
	// レア度 3 の色の G
	constexpr float kRarityBlueColorG = 0.75f;
	// レア度 4 の色の R
	constexpr float kRarityPurpleColorR = 0.8f;
	// レア度 4 の色の G
	constexpr float kRarityPurpleColorG = 0.55f;
	// レア度 5 の色の G
	constexpr float kRarityOrangeColorG = 0.62f;
	// レア度 5 の色の B
	constexpr float kRarityOrangeColorB = 0.25f;
	// レア度 1（それ以外も）の白の明るさ
	constexpr float kRarityWhiteGray = 0.92f;
	// 数字をカンマで区切る桁の数
	constexpr size_t kDigitGroupSize = 3;
	// カメラを原点から手前（−Z）へ離す距離 [m]
	constexpr float kCameraBackZ = 10.0f;
	// 「+N」を出していない状態の経過時間（kSoldPopupTime より十分大きい）[秒]
	constexpr float kSoldPopupHiddenTime = 99.0f;
	// 右のパネルの上端から「ステージ報酬」の見出しまで [px]
	constexpr float kStageHeaderTopPad = 14.0f;
	// 見出しからマスの並びまで [px]
	constexpr float kHeaderToGridGap = 42.0f;
	// マスの並びから次の見出し・説明の欄まで [px]
	constexpr float kSectionGap = 20.0f;
	// 右のパネルの下端からボタンまでの余白 [px]
	constexpr float kButtonBottomPad = 16.0f;
	// 説明の欄からボタンまでのすき間 [px]
	constexpr float kDescToButtonGap = 12.0f;
	// 売る数（▲▼）を説明の欄の中心から左へずらす量 [px]
	constexpr float kSellAmountOffsetX = 90.0f;
	// 説明の欄の上端から ▲ まで [px]
	constexpr float kSellUpArrowY = 46.0f;
	// 説明の欄の上端から ▼ まで [px]
	constexpr float kSellDownArrowY = 106.0f;
	// ▲▼ の当たりの四角の幅の半分 [px]
	constexpr float kSellArrowHalfW = 40.0f;
	// ▲▼ の当たりの四角の幅 [px]
	constexpr float kSellArrowW = 80.0f;
	// ▲▼ の当たりの四角の高さ [px]
	constexpr float kSellArrowH = 28.0f;
	// 説明の欄の下端から [売る][やめる] までの余白 [px]
	constexpr float kSellButtonBottomPad = 16.0f;
	// [売る][やめる] の高さ [px]
	constexpr float kSellButtonH = 40.0f;
	// [売る] と [やめる] のすき間の半分 [px]
	constexpr float kSellButtonHalfGap = 10.0f;
	// UI の不透明度が目標へ追いつく速さ [/秒]
	constexpr float kUiAlphaFollowSpeed = 12.0f;
	// UI の不透明度がこれより大きければ描く
	constexpr float kUiVisibleAlpha = 0.001f;
	// 文字を読みやすくする画面全体の暗がりの濃さ
	constexpr float kDimAlpha = 0.22f;
	// 左右を少し濃くする帯の幅 [px]
	constexpr float kSideShadeWidth = 720.0f;
	// 左右を少し濃くする帯の一番濃い所の濃さ
	constexpr float kSideShadeAlpha = 0.28f;
	// 代わりの背景のグラデーション：下端の R
	constexpr float kFallbackBottomR = 0.03f;
	// 代わりの背景のグラデーション：上端へ足す R
	constexpr float kFallbackRangeR = 0.03f;
	// 代わりの背景のグラデーション：下端の G
	constexpr float kFallbackBottomG = 0.05f;
	// 代わりの背景のグラデーション：上端へ足す G
	constexpr float kFallbackRangeG = 0.05f;
	// 代わりの背景のグラデーション：下端の B
	constexpr float kFallbackBottomB = 0.10f;
	// 代わりの背景のグラデーション：上端へ足す B
	constexpr float kFallbackRangeB = 0.08f;
	// 代わりの背景の光の帯が上下に揺れる幅 [px]
	constexpr float kBandSwayPixels = 18.0f;
	// パネルの左端の色の帯の幅 [px]
	constexpr float kAccentBarWidth = 4.0f;
	// パネルの左端の色の帯の不透明度
	constexpr float kAccentBarAlpha = 0.8f;
	// 記録のパネルの左端から中身まで [px]
	constexpr float kRecordPadX = 24.0f;
	// 記録のパネルの上端から「STAGE CLEAR」まで [px]
	constexpr float kRecordTitleY = 14.0f;
	// 「STAGE CLEAR」の文字の大きさ
	constexpr float kRecordTitleScale = 0.55f;
	// 文字の縁の太さ [px]
	constexpr float kTextOutline = 1.5f;
	// 記録のパネルの上端からステージ名まで [px]
	constexpr float kStageNameY = 44.0f;
	// ステージ名の文字の大きさ
	constexpr float kStageNameScale = 1.15f;
	// 大きな文字の縁の太さ [px]
	constexpr float kTextOutlineBold = 2.0f;
	// 記録のパネルの上端から区切りの線まで [px]
	constexpr float kRecordRuleY = 96.0f;
	// 区切りの線を記録のパネルの幅より短くする量（左右の合計）[px]
	constexpr float kRecordRuleInsetTotal = 48.0f;
	// 記録のパネルの区切りの線の太さ [px]
	constexpr float kRecordRuleThickness = 2.0f;
	// 記録のパネルの区切りの線の不透明度
	constexpr float kRecordRuleAlpha = 0.6f;
	// 1 秒あたりの 1/100 秒の数（秒 → 1/100 秒）
	constexpr float kCentisecondsPerSecondF = 100.0f;
	// 1 分あたりの 1/100 秒の数
	constexpr int kCentisecondsPerMinute = 6000;
	// クリアタイムの分の表示の上限
	constexpr int kMaxDisplayMinutes = 99;
	// 1 秒あたりの 1/100 秒の数
	constexpr int kCentisecondsPerSecond = 100;
	// 1 分あたりの秒数
	constexpr int kSecondsPerMinute = 60;
	// 記録のパネルの上端から 1 行目まで [px]
	constexpr float kRecordRowsTop = 112.0f;
	// 記録の行ごとに遅らせて出す秒数 [秒]
	constexpr float kRecordRowStagger = 0.08f;
	// 記録の 1 行が出きるまで [秒]
	constexpr float kRecordRowFadeTime = 0.25f;
	// 記録の行の左右の余白 [px]
	constexpr float kRecordRowPadX = 28.0f;
	// 記録の行の見出しを値より下げる量 [px]
	constexpr float kRecordLabelOffsetY = 6.0f;
	// 見出しの文字色 R（薄い青灰色）
	constexpr float kLabelTextR = 0.8f;
	// 見出しの文字色 G（薄い青灰色）
	constexpr float kLabelTextG = 0.85f;
	// 見出しの文字色 B（薄い青灰色）
	constexpr float kLabelTextB = 0.9f;
	// 獲得エーテルを記録より遅らせて出す秒数 [秒]
	constexpr float kEarnedAppearDelay = 0.35f;
	// 獲得エーテルの枠の上端からアイコンまで [px]
	constexpr float kEarnedIconY = 18.0f;
	// 小さいアイコンの大きさ [px]
	constexpr float kSmallIconSize = 36.0f;
	// 獲得エーテルの枠の左端から見出しまで [px]
	constexpr float kEarnedLabelX = 74.0f;
	// 獲得エーテルの枠の上端から見出しまで [px]
	constexpr float kEarnedLabelY = 22.0f;
	// 見出しの文字の大きさ
	constexpr float kSmallTextScale = 0.6f;
	// エーテルの見出しの文字色 R
	constexpr float kEtherLabelR = 0.85f;
	// エーテルの見出しの文字色 G
	constexpr float kEtherLabelG = 0.9f;
	// エーテルの見出しの文字色 B
	constexpr float kEtherLabelB = 0.95f;
	// 獲得エーテルの枠の上端から数まで [px]
	constexpr float kEarnedValueY = 14.0f;
	// 所持エーテルの枠の下の線の太さ [px]
	constexpr float kHandUnderlineH = 3.0f;
	// 所持エーテルの枠の下の線の不透明度
	constexpr float kHandUnderlineAlpha = 0.7f;
	// 所持エーテルの枠の左端からアイコンまで [px]
	constexpr float kHandIconX = 20.0f;
	// 所持エーテルの枠の上端からアイコンまで [px]
	constexpr float kHandIconY = 22.0f;
	// 所持エーテルの枠の左端から見出しまで [px]
	constexpr float kHandLabelX = 70.0f;
	// 所持エーテルの枠の上端から見出し・「+N」まで [px]
	constexpr float kHandLabelY = 26.0f;
	// 所持エーテルの枠の右端から数まで [px]
	constexpr float kHandPadRight = 24.0f;
	// 所持エーテルの枠の上端から数まで [px]
	constexpr float kHandValueY = 18.0f;
	// 「+N」が消え始める進み具合
	constexpr float kPopupFadeStart = 0.6f;
	// 「+N」が消えきるまでの進み具合の幅
	constexpr float kPopupFadeSpan = 0.4f;
	// 「+N」が出きるまで [秒]
	constexpr float kPopupFadeIn = 0.08f;
	// 所持の数から「+N」まで [px]
	constexpr float kPopupGap = 16.0f;
	// 「+N」が上がる量 [px]
	constexpr float kPopupRise = 14.0f;
	// 「+N」の色 G（薄い黄色）
	constexpr float kPopupColorG = 0.95f;
	// 「+N」の色 B（薄い黄色）
	constexpr float kPopupColorB = 0.6f;
	// 見出しから下の線まで [px]
	constexpr float kHeaderRuleY = 30.0f;
	// 見出しの下の線の太さ [px]
	constexpr float kHeaderRuleThickness = 1.5f;
	// 見出しの下の線の不透明度
	constexpr float kHeaderRuleAlpha = 0.5f;
	// 空のマスの塗りの不透明度
	constexpr float kEmptySlotFillAlpha = 0.04f;
	// 空のマスの枠の不透明度
	constexpr float kEmptySlotFrameAlpha = 0.12f;
	// マスは数え始めのこの秒数前から出始める [秒]
	constexpr float kSlotAppearLead = 0.15f;
	// マスが出きるまで [秒]
	constexpr float kSlotAppearTime = 0.15f;
	// マスの塗りの不透明度
	constexpr float kSlotFillAlpha = 0.06f;
	// マスの枠の不透明度
	constexpr float kSlotFrameAlpha = 0.18f;
	// マスのアイコンが出たときに大きくなっている量（1＋この値から収まる）
	constexpr float kSlotPopScale = 0.25f;
	// マスのアイコンを上端から下げる量 [px]
	constexpr float kSlotIconOffsetY = 10.0f;
	// マスの右端から「×N」まで [px]
	constexpr float kSlotCountPadRight = 6.0f;
	// マスの下端から「×N」まで [px]
	constexpr float kSlotCountPadBottom = 24.0f;
	// 選んでいる枠の明滅の一番薄い不透明度
	constexpr float kPulseMinAlpha = 0.65f;
	// 選んでいる枠の明滅の不透明度の振れ幅
	constexpr float kPulseAlphaRange = 0.35f;
	// 選んでいる枠の明滅の速さ [rad/秒]
	constexpr float kPulseSpeed = 5.0f;
	// 選んでいる枠を外へ出す量 [px]
	constexpr float kSelectFrameOffset = 3.0f;
	// 選んでいる枠を大きくする量（左右・上下の合計）[px]
	constexpr float kSelectFrameGrow = 6.0f;
	// 選んでいる枠の太さ [px]
	constexpr float kSelectFrameThickness = 3.0f;
	// アイコンの下地を素材の色より暗くする倍率
	constexpr float kIconBaseDarken = 0.35f;
	// アイコンの中の四角を下地から内側へ入れる量（大きさに対する割合）
	constexpr float kIconInsetRatio = 0.18f;
	// アイコンの左上のつやの幅（中の四角に対する割合）
	constexpr float kIconGlossW = 0.45f;
	// アイコンの左上のつやの高さ（中の四角に対する割合）
	constexpr float kIconGlossH = 0.2f;
	// アイコンの左上のつやの不透明度
	constexpr float kIconGlossAlpha = 0.35f;
	// 説明の欄の下地の濃さ
	constexpr float kDescBgAlpha = 0.35f;
	// 説明の欄の枠の不透明度
	constexpr float kDescFrameAlpha = 0.15f;
	// 説明の欄・売る小窓の端から中身まで [px]
	constexpr float kDescPad = 18.0f;
	// 素材が無いときの案内の文字の大きさ
	constexpr float kDescEmptyScale = 0.55f;
	// 素材が無いときの案内の文字の灰色の明るさ
	constexpr float kDescEmptyGray = 0.8f;
	// 素材が無いときの案内の文字の不透明度
	constexpr float kDescEmptyAlpha = 0.8f;
	// 説明の欄のアイコンの大きさ [px]
	constexpr float kDescIconSize = 44.0f;
	// 説明の欄の左端から素材の名前まで [px]
	constexpr float kDescNameX = 76.0f;
	// 説明の欄の上端から 1 行目（名前・数）まで [px]
	constexpr float kDescTopRowY = 16.0f;
	// 素材の名前の文字の大きさ
	constexpr float kDescNameScale = 0.7f;
	// 説明の欄の左端からレア度まで [px]
	constexpr float kDescRarityX = 78.0f;
	// 説明の欄の上端から 2 行目（レア度・売値）まで [px]
	constexpr float kDescSecondRowY = 46.0f;
	// 説明の欄の小さい文字の大きさ
	constexpr float kDescSmallScale = 0.5f;
	// 説明の欄の「今回 ×N」の文字の大きさ
	constexpr float kDescCountScale = 0.55f;
	// 説明の欄の「今回 ×N」の文字の明るさ
	constexpr float kDescCountGray = 0.9f;
	// 売値の文字色 R
	constexpr float kPriceR = 0.75f;
	// 売値の文字色 G
	constexpr float kPriceG = 0.85f;
	// 売値の文字色 B
	constexpr float kPriceB = 0.95f;
	// 説明の欄の上端から区切りの線まで [px]
	constexpr float kDescRuleY = 76.0f;
	// 区切りの線を説明の欄の幅より短くする量（左右の合計）[px]
	constexpr float kDescRuleInsetTotal = 36.0f;
	// 説明の欄の区切りの線の不透明度
	constexpr float kDescRuleAlpha = 0.2f;
	// 説明の欄の上端から説明文まで [px]
	constexpr float kDescTextTop = 88.0f;
	// 説明文の色 R
	constexpr float kDescTextR = 0.88f;
	// 説明文の色 G
	constexpr float kDescTextG = 0.9f;
	// 説明文の色 B
	constexpr float kDescTextB = 0.94f;
	// 説明文の行の高さ [px]
	constexpr float kDescLineHeight = 26.0f;
	// 売る小窓の下地の色 R
	constexpr float kSellBgR = 0.02f;
	// 売る小窓の下地の色 G
	constexpr float kSellBgG = 0.04f;
	// 売る小窓の下地の色 B
	constexpr float kSellBgB = 0.08f;
	// 売る小窓の下地の不透明度
	constexpr float kSellBgAlpha = 0.85f;
	// 売る小窓の枠の太さ [px]
	constexpr float kSellFrameThickness = 2.0f;
	// 売る小窓の枠の不透明度
	constexpr float kSellFrameAlpha = 0.8f;
	// 売る小窓の上端からアイコンまで [px]
	constexpr float kSellIconY = 16.0f;
	// 売る小窓の左端から題まで [px]
	constexpr float kSellTitleX = 66.0f;
	// 売る小窓の上端から題まで [px]
	constexpr float kSellTitleY = 18.0f;
	// 売る小窓の題の文字の大きさ
	constexpr float kSellTitleScale = 0.62f;
	// ▲▼ の文字の大きさ
	constexpr float kSellArrowScale = 0.6f;
	// ▲▼ の当たりの四角の上端から文字まで [px]
	constexpr float kSellArrowGlyphY = 3.0f;
	// 押せない ▲▼ の不透明度
	constexpr float kSellArrowDisabledAlpha = 0.3f;
	// 売る数の文字の大きさ
	constexpr float kSellAmountScale = 0.85f;
	// 説明の欄の上端から売る数まで [px]
	constexpr float kSellAmountY = 76.0f;
	// 合計を説明の欄の中心から右へずらす量 [px]
	constexpr float kSellTotalOffsetX = 100.0f;
	// 「合計」の文字の大きさ
	constexpr float kSellTotalLabelScale = 0.5f;
	// 説明の欄の上端から「合計」まで [px]
	constexpr float kSellTotalLabelY = 56.0f;
	// 合計の数の文字の大きさ
	constexpr float kSellTotalScale = 0.7f;
	// 説明の欄の上端から合計の数まで [px]
	constexpr float kSellTotalY = 84.0f;
	// 選んでいるボタンの金の塗りの不透明度
	constexpr float kFocusedFillAlpha = 0.28f;
	// 選んでいないボタンの塗りの不透明度
	constexpr float kButtonFillAlpha = 0.06f;
	// 選んでいない [売る][やめる] の枠の不透明度
	constexpr float kSellButtonFrameAlpha = 0.3f;
	// [売る][やめる] の文字の大きさ
	constexpr float kSellButtonTextScale = 0.6f;
	// [売る][やめる] の上端から文字まで [px]
	constexpr float kSellButtonTextY = 8.0f;
	// 数え上げの間のステージ選択ボタンの不透明度
	constexpr float kCountingButtonAlpha = 0.5f;
	// 選んでいないステージ選択ボタンの枠の不透明度
	constexpr float kStageButtonFrameAlpha = 0.25f;
	// ステージ選択ボタンの文字の大きさ
	constexpr float kStageButtonTextScale = 0.62f;
	// ステージ選択ボタンの上端から文字まで [px]
	constexpr float kStageButtonTextY = 9.0f;
	// 背景を見ている間の案内を出しておく秒数 [秒]
	constexpr float kBgHintShowSeconds = 1.5f;
	// 背景を見ている間の案内が消えきるまで [秒]
	constexpr float kBgHintFadeSeconds = 0.5f;
	// 画面の下端から背景を見ている間の案内まで [px]
	constexpr float kBgHintBottom = 70.0f;
	// 背景を見ている間の案内の不透明度
	constexpr float kBgHintAlpha = 0.75f;
	// 操作の案内が出きるまで [秒]
	constexpr float kHintAppearTime = 0.3f;
	// 画面の下端から操作の案内の帯の上端まで [px]
	constexpr float kHintBarTop = 84.0f;
	// 操作の案内の帯の高さ [px]
	constexpr float kHintBarH = 50.0f;
	// 操作の案内の帯の濃さ
	constexpr float kHintBarAlpha = 0.35f;
	// 画面の下端から操作の案内の文字まで [px]
	constexpr float kHintTextBottom = 72.0f;
	// 操作の案内の文字の不透明度
	constexpr float kHintTextAlpha = 0.85f;

	const Vector4 kGold{ 1.0f, 0.88f, 0.55f, 1.0f };
	const Vector4 kPanelColor{ 0.0f, 0.02f, 0.05f, 0.55f };

	/// <summary>0〜1 に収める</summary>
	/// <param name="v">値</param>
	/// <returns>0〜1 に収めた値</returns>
	float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
	/// <summary>3 次のイーズアウト</summary>
	/// <param name="t">進み具合（0〜1 に収める）</param>
	/// <returns>補間した値</returns>
	float EaseOutCubic(float t) { t = 1.0f - Clamp01(t); return 1.0f - t * t * t; }
	/// <summary>色の α に倍率を掛ける</summary>
	/// <param name="c">色</param>
	/// <param name="a">α の倍率</param>
	/// <returns>α を掛けた色</returns>
	Vector4 WithAlpha(const Vector4& c, float a) { return { c.x, c.y, c.z, c.w * a }; }
	/// <summary>マスの数を 6 列で並べた時の段の数</summary>
	/// <param name="count">マスの数</param>
	/// <param name="minRows">少なくとも並べる段の数</param>
	/// <returns>段の数</returns>
	int RowsFor(size_t count, int minRows) {
		return (std::max)(minRows, (static_cast<int>(count) + kSlotColumns - 1) / kSlotColumns);
	}
	/// <summary>マスを rows 段並べた時の高さ</summary>
	/// <param name="rows">段の数</param>
	/// <returns>高さ [px]</returns>
	float GridHeight(int rows) { return rows * kSlotSize + (rows - 1) * kSlotGap; }

	/// <summary>レア度の色（1 白 → 2 緑 → 3 青 → 4 紫 → 5 橙）</summary>
	/// <param name="rarity">レア度（1〜5。それ以外は白）</param>
	/// <returns>色</returns>
	Vector4 RarityColor(int rarity) {
		switch (rarity) {
		case kRarityGreen: return { kRarityGreenColorRB, kRarityGreenColorG, kRarityGreenColorRB, 1.0f };
		case kRarityBlue: return { kRarityBlueColorR, kRarityBlueColorG, 1.0f, 1.0f };
		case kRarityPurple: return { kRarityPurpleColorR, kRarityPurpleColorG, 1.0f, 1.0f };
		case kRarityOrange: return { 1.0f, kRarityOrangeColorG, kRarityOrangeColorB, 1.0f };
		default: return { kRarityWhiteGray, kRarityWhiteGray, kRarityWhiteGray, 1.0f };
		}
	}

	/// <summary>"\n" で行に分ける（TextRenderer は改行を扱わない）</summary>
	/// <param name="text">文</param>
	/// <returns>行の一覧（元の文を指す）</returns>
	std::vector<std::string_view> SplitLines(std::string_view text) {
		std::vector<std::string_view> lines;
		size_t begin = 0;
		while (begin <= text.size()) {
			const size_t end = text.find('\n', begin);
			if (end == std::string_view::npos) {
				lines.push_back(text.substr(begin));
				break;
			}
			lines.push_back(text.substr(begin, end - begin));
			begin = end + 1;
		}
		return lines;
	}

	/// <summary>3 桁ごとにカンマを入れた文字列にする</summary>
	/// <param name="value">値（負なら先頭に "-"）</param>
	/// <returns>カンマ入りの文字列</returns>
	std::string WithCommas(int value) {
		std::string digits = std::to_string(value < 0 ? -value : value);
		std::string out;
		for (size_t i = 0; i < digits.size(); ++i) {
			if (i > 0 && (digits.size() - i) % kDigitGroupSize == 0) out.push_back(',');
			out.push_back(digits[i]);
		}
		return value < 0 ? "-" + out : out;
	}

	/// <summary>アクションがこのフレームに押されたか</summary>
	/// <param name="actions">アクションの入力（nullptr 可）</param>
	/// <param name="a">アクション</param>
	/// <returns>押されたら true（actions が nullptr なら false）</returns>
	bool Triggered(InputActionMap* actions, Action a) {
		return actions && actions->IsTriggered(static_cast<int>(a));
	}
}

ResultScene::ResultScene() = default;
ResultScene::~ResultScene() = default;

void ResultScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -kCameraBackZ });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	GetObject3DManager()->SetDefaultCamera(camera_.get());

	const StageClearRecord* record = GetGameSession()->GetStageClearRecord();
	stageName_ = record->GetStageName();
	clearTime_ = record->GetClearTime();
	purity_ = GetGameSession()->GetEtherState()->GetDifficulty();
	score_ = GetGameSession()->GetScoreManager()->GetScore();
	killCount_ = GetGameSession()->GetScoreManager()->GetKillCount();
	earnedEther_ = GetGameSession()->GetLootLedger()->GetEther();

	BuildRewards();
	SetupCaptureSprite();

	selected_ = (SelectableCount() > 0) ? 0 : ButtonIndex();
	lastColumn_ = 0;
	sellOpen_ = false;
	sellAmount_ = 1;
	soldEtherPopup_ = 0;
	soldPopupTime_ = kSoldPopupHiddenTime;
	time_ = 0.0f;
	backgroundOnly_ = false;
	uiAlpha_ = 1.0f;
	backgroundToggleTime_ = 0.0f;
}

void ResultScene::BuildRewards() {
	// 手に入れた素材だけをマスにする（Loot.json の並び）。ステージ報酬 → 部位破壊報酬の順に数え上げる
	stageSlots_.clear();
	partBreakSlots_.clear();
	const LootCatalog* catalog = LootCatalog::GetInstance();
	const LootLedger* ledger = GetGameSession()->GetLootLedger();
	for (int i = 0; i < catalog->GetItemCount(); ++i) {
		const LootItemDef* item = catalog->GetItem(i);
		const int stageCount = ledger->GetStageItemCount(item->id);
		if (stageCount > 0) stageSlots_.push_back({ item, stageCount, 0.0f });
		const int partCount = ledger->GetPartBreakItemCount(item->id);
		if (partCount > 0) partBreakSlots_.push_back({ item, partCount, 0.0f });
	}
	float t = kSlotStart;
	for (RewardSlot& s : stageSlots_) { s.startTime = t; t += kSlotStagger; }
	for (RewardSlot& s : partBreakSlots_) { s.startTime = t; t += kSlotStagger; }
}

void ResultScene::SetupCaptureSprite() {
	captureSprite_.reset();
	const StageClearRecord* record = GetGameSession()->GetStageClearRecord();
	if (!record->HasCapture() || !GetSpriteManager()) return;

	// B が RegisterExternalTexture で登録した名前で引く（§3）。寿命は B の持ち主
	captureSprite_ = std::make_unique<SpriteInstance>();
	captureSprite_->Initialize(GetSpriteManager(), record->GetCaptureTextureName(), "ResultCapture");
	captureSize_ = { static_cast<float>(record->GetCaptureWidth()), static_cast<float>(record->GetCaptureHeight()) };
}

float ResultScene::CountUpEndTime() const {
	float end = kEtherStart + kEtherCountTime;
	for (const RewardSlot& s : stageSlots_) end = (std::max)(end, s.startTime + kSlotCountTime);
	for (const RewardSlot& s : partBreakSlots_) end = (std::max)(end, s.startTime + kSlotCountTime);
	return end;
}

void ResultScene::Finalize() {
	captureSprite_.reset();
}

// ===== 選ぶ =====

int ResultScene::SelectableCount() const {
	return static_cast<int>(stageSlots_.size() + partBreakSlots_.size());
}

ResultScene::RewardSlot* ResultScene::SelectableAt(int index) {
	return const_cast<RewardSlot*>(static_cast<const ResultScene*>(this)->SelectableAt(index));
}

const ResultScene::RewardSlot* ResultScene::SelectableAt(int index) const {
	if (index < 0) return nullptr;
	const size_t i = static_cast<size_t>(index);
	if (i < stageSlots_.size()) return &stageSlots_[i];
	if (i - stageSlots_.size() < partBreakSlots_.size()) return &partBreakSlots_[i - stageSlots_.size()];
	return nullptr;
}

bool ResultScene::IsPartBreakIndex(int index) const {
	return index >= static_cast<int>(stageSlots_.size()) && index < SelectableCount();
}

void ResultScene::Select(int index, bool playSound) {
	index = std::clamp(index, 0, ButtonIndex());
	if (index == selected_) return;
	selected_ = index;
	if (playSound) SoundManager::GetInstance()->Play2DSound("se_ui_cursor");
}

void ResultScene::MoveHorizontal(int dir) {
	// マスの間を順に（ステージ報酬 → 部位破壊報酬、端で折り返す）。「ステージ選択へ」の上では動かない
	const int count = SelectableCount();
	if (count == 0 || selected_ >= count) return;
	const int next = (selected_ + dir + count) % count;
	const int local = IsPartBreakIndex(next) ? next - static_cast<int>(stageSlots_.size()) : next;
	lastColumn_ = local % kSlotColumns;
	Select(next, true);
}

void ResultScene::MoveVertical(int dir) {
	// 段の一覧（中身のある段だけ）：{ 最初の選択番号, マスの数 }。最後に「ステージ選択へ」
	struct Row { int first; int count; };
	std::vector<Row> rows;
	auto addRows = [&rows](int offset, size_t size) {
		for (size_t i = 0; i < size; i += kSlotColumns) {
			rows.push_back({ offset + static_cast<int>(i), static_cast<int>((std::min)(size - i, static_cast<size_t>(kSlotColumns))) });
		}
	};
	addRows(0, stageSlots_.size());
	addRows(static_cast<int>(stageSlots_.size()), partBreakSlots_.size());
	rows.push_back({ ButtonIndex(), 1 });

	int current = static_cast<int>(rows.size()) - 1;
	for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
		if (selected_ >= rows[r].first && selected_ < rows[r].first + rows[r].count) { current = r; break; }
	}
	const int target = current + dir;
	if (target < 0 || target >= static_cast<int>(rows.size())) return;
	const Row& row = rows[static_cast<size_t>(target)];
	if (row.first == ButtonIndex()) {
		Select(ButtonIndex(), true);
		return;
	}
	Select(row.first + (std::min)(lastColumn_, row.count - 1), true);
}

ResultScene::PanelLayout ResultScene::MakeLayout(float slideX) const {
	PanelLayout l;
	const float gridW = kSlotSize * kSlotColumns + kSlotGap * (kSlotColumns - 1);
	l.panelX = kPanelX + slideX;
	l.innerX = l.panelX + (kPanelW - gridW) * 0.5f;
	l.innerW = gridW;
	l.stageHeaderY = kPanelY + kStageHeaderTopPad;
	l.stageGridY = l.stageHeaderY + kHeaderToGridGap;
	l.stageRows = RowsFor(stageSlots_.size(), kStageSlotRows);
	l.partHeaderY = l.stageGridY + GridHeight(l.stageRows) + kSectionGap;
	l.partGridY = l.partHeaderY + kHeaderToGridGap;
	l.partRows = RowsFor(partBreakSlots_.size(), kPartBreakSlotRows);
	l.descY = l.partGridY + GridHeight(l.partRows) + kSectionGap;
	l.buttonY = kPanelY + kPanelH - kButtonBottomPad - kButtonH;
	l.descH = l.buttonY - kDescToButtonGap - l.descY;
	return l;
}

ResultScene::Rect ResultScene::SlotRect(const PanelLayout& layout, int selectableIndex) const {
	const bool part = IsPartBreakIndex(selectableIndex);
	const int local = part ? selectableIndex - static_cast<int>(stageSlots_.size()) : selectableIndex;
	const float gridY = part ? layout.partGridY : layout.stageGridY;
	return { { layout.innerX + (local % kSlotColumns) * (kSlotSize + kSlotGap), gridY + (local / kSlotColumns) * (kSlotSize + kSlotGap) },
		{ kSlotSize, kSlotSize } };
}

ResultScene::Rect ResultScene::ButtonRect(const PanelLayout& layout) const {
	return { { layout.innerX, layout.buttonY }, { layout.innerW, kButtonH } };
}

// ===== 売る =====

ResultScene::Rect ResultScene::SellArrowRect(const PanelLayout& layout, int dir) const {
	// 売る数の ▲（dir > 0）と ▼（dir < 0）。数の上と下に置く（小窓は説明の欄に重ねて出す）
	const float centerX = layout.innerX + layout.innerW * 0.5f - kSellAmountOffsetX;
	const float y = (dir > 0) ? layout.descY + kSellUpArrowY : layout.descY + kSellDownArrowY;
	return { { centerX - kSellArrowHalfW, y }, { kSellArrowW, kSellArrowH } };
}

ResultScene::Rect ResultScene::SellButtonRect(const PanelLayout& layout, bool confirm) const {
	const float w = 200.0f;
	const float y = layout.descY + layout.descH - kSellButtonBottomPad - kSellButtonH;
	const float centerX = layout.innerX + layout.innerW * 0.5f;
	const float x = confirm ? centerX - w - kSellButtonHalfGap : centerX + kSellButtonHalfGap;
	return { { x, y }, { w, kSellButtonH } };
}

void ResultScene::OpenSell() {
	const RewardSlot* slot = SelectableAt(selected_);
	if (!slot || !slot->item || slot->count <= 0) return;
	if (slot->item->sellPrice <= 0) {
		SoundManager::GetInstance()->Play2DSound("se_ui_cancel");   // 売れない素材
		return;
	}
	sellOpen_ = true;
	sellAmount_ = 1;
	sellFocusCancel_ = false;
	SoundManager::GetInstance()->Play2DSound("se_ui_decide");
}

void ResultScene::UpdateSell(InputActionMap* actions, const UIPointer& pointer) {
	const RewardSlot* slot = SelectableAt(selected_);
	if (!slot || !slot->item) {
		sellOpen_ = false;
		return;
	}
	const PanelLayout layout = MakeLayout(0.0f);
	auto changeAmount = [this, slot](int delta) {
		const int next = std::clamp(sellAmount_ + delta, 1, slot->count);
		if (next == sellAmount_) return;
		sellAmount_ = next;
		SoundManager::GetInstance()->Play2DSound("se_ui_cursor");
	};

	auto focus = [this](bool cancelButton) {
		if (sellFocusCancel_ == cancelButton) return;
		sellFocusCancel_ = cancelButton;
		SoundManager::GetInstance()->Play2DSound("se_ui_cursor");
	};

	// ↑↓：売る数、←→：［売る］［やめる］
	if (Triggered(actions, Action::MenuUp)) changeAmount(+1);
	if (Triggered(actions, Action::MenuDown)) changeAmount(-1);
	if (Triggered(actions, Action::MenuLeft)) focus(false);
	if (Triggered(actions, Action::MenuRight)) focus(true);

	// 決定は選んでいるボタン。キャンセルはいつでもやめる
	bool sell = false;
	bool cancel = Triggered(actions, Action::MenuCancel);
	if (Triggered(actions, Action::MenuConfirm)) {
		if (sellFocusCancel_) cancel = true;
		else sell = true;
	}

	// マウス：ボタンに乗せたら選ぶ、クリックで実行。▲▼ のクリックで数を増減
	if (pointer.valid) {
		const bool overSell = SellButtonRect(layout, true).Contains(pointer.position);
		const bool overCancel = SellButtonRect(layout, false).Contains(pointer.position);
		if (pointer.moved || pointer.clicked) {
			if (overSell) focus(false);
			else if (overCancel) focus(true);
		}
		if (pointer.clicked) {
			if (SellArrowRect(layout, +1).Contains(pointer.position)) changeAmount(+1);
			else if (SellArrowRect(layout, -1).Contains(pointer.position)) changeAmount(-1);
			else if (overSell) sell = true;
			else if (overCancel) cancel = true;
		}
	}

	if (sell) {
		ExecuteSell();
	} else if (cancel) {
		sellOpen_ = false;
		SoundManager::GetInstance()->Play2DSound("se_ui_cancel");
	}
}

void ResultScene::ExecuteSell() {
	RewardSlot* slot = SelectableAt(selected_);
	sellOpen_ = false;
	if (!slot || !slot->item) return;
	const int amount = std::clamp(sellAmount_, 1, slot->count);
	const int gain = slot->item->sellPrice * amount;
	if (gain <= 0) return;

	// 所持へ足す（無限の扱いの間は表示は 99,999 のまま。「+N」で見せる）。素材の数はこの画面の表示だけ減らす（集計は展示用）
	GetGameSession()->GetEtherState()->AddHand(static_cast<float>(gain));
	soldEtherPopup_ = (soldPopupTime_ < kSoldPopupTime) ? soldEtherPopup_ + gain : gain;
	soldPopupTime_ = 0.0f;
	SoundManager::GetInstance()->Play2DSound("se_ui_decide");

	slot->count -= amount;
	if (slot->count <= 0) {
		// 売りきったマスは消して詰める
		const bool part = IsPartBreakIndex(selected_);
		if (part) partBreakSlots_.erase(partBreakSlots_.begin() + (selected_ - static_cast<int>(stageSlots_.size())));
		else stageSlots_.erase(stageSlots_.begin() + selected_);
		if (SelectableCount() == 0) selected_ = ButtonIndex();
		else selected_ = (std::min)(selected_, SelectableCount() - 1);
	}
}

void ResultScene::GoToStageSelect() {
	SoundManager::GetInstance()->Play2DSound("se_ui_decide");
	// ポーズの「ステージセレクトに戻る」（⑩）と同じく、コクピットの起動から
	SceneManager::GetInstance()->ChangeScene("STAGESELECT");
}

// ===== 更新 =====

void ResultScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	const float dt = GetDxCore()->GetDeltaTime();
	time_ += dt;
	backgroundToggleTime_ += dt;
	soldPopupTime_ += dt;
	const float targetAlpha = backgroundOnly_ ? 0.0f : 1.0f;
	uiAlpha_ += (targetAlpha - uiAlpha_) * (std::min)(1.0f, dt * kUiAlphaFollowSpeed);

	camera_->Update();

	InputActionMap* actions = GetInput()->GetActionMap();
	const UIPointer pointer = UIPointer::FromInput(GetInput());

	// 売る小窓の間は小窓だけが入力を読む
	if (sellOpen_) {
		UpdateSell(actions, pointer);
		return;
	}

	// 背景を見る：キャンセルで UI を隠す ↔ 戻す。隠している間は決定・クリックでも戻る
	if (Triggered(actions, Action::MenuCancel)
		|| (backgroundOnly_ && (Triggered(actions, Action::MenuConfirm) || (pointer.valid && pointer.clicked)))) {
		backgroundOnly_ = !backgroundOnly_;
		backgroundToggleTime_ = 0.0f;
		SoundManager::GetInstance()->Play2DSound(backgroundOnly_ ? "se_ui_cursor" : "se_ui_cancel");
		return;
	}
	if (backgroundOnly_) return;

	const bool confirm = Triggered(actions, Action::MenuConfirm);
	const bool leave = Triggered(actions, Action::Pause);
	const bool click = pointer.valid && pointer.clicked;

	// 数え上げの途中なら、まず最後まで飛ばす
	if (IsCounting()) {
		if (confirm || leave || click) time_ = CountUpEndTime();
		return;
	}

	// 十字キーで選ぶ
	if (Triggered(actions, Action::MenuLeft)) MoveHorizontal(-1);
	if (Triggered(actions, Action::MenuRight)) MoveHorizontal(+1);
	if (Triggered(actions, Action::MenuUp)) MoveVertical(-1);
	if (Triggered(actions, Action::MenuDown)) MoveVertical(+1);

	// マウス：乗せたら選ぶ、クリックで決定
	bool clickedTarget = false;
	if (pointer.valid && (pointer.moved || click)) {
		const PanelLayout layout = MakeLayout(0.0f);
		for (int i = 0; i < SelectableCount(); ++i) {
			if (!SlotRect(layout, i).Contains(pointer.position)) continue;
			const int local = IsPartBreakIndex(i) ? i - static_cast<int>(stageSlots_.size()) : i;
			lastColumn_ = local % kSlotColumns;
			Select(i, true);
			clickedTarget = click;
			break;
		}
		if (ButtonRect(layout).Contains(pointer.position)) {
			Select(ButtonIndex(), true);
			clickedTarget = click;
		}
	}

	if (confirm || clickedTarget) {
		if (selected_ == ButtonIndex()) GoToStageSelect();
		else OpenSell();
		return;
	}
	if (leave) {
		GoToStageSelect();
		return;
	}
}

// ===== 描く =====

void ResultScene::Draw() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float screenW = static_cast<float>(GetDxCore()->GetUIWidth());
	const float screenH = static_cast<float>(GetDxCore()->GetUIHeight());

	DrawBackground(screenW, screenH);

	const float ui = Clamp01(uiAlpha_);
	if (ui > kUiVisibleAlpha) {
		DrawRecordPanel(ui);
		DrawEarnedEther(ui);
		DrawHandEther(ui);
		DrawRewardPanel(ui);
	}
	DrawHint(screenW, screenH, ui);
	tr->Flush();
}

void ResultScene::DrawBackground(float screenW, float screenH) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float zoom = 1.0f + kZoomAmount * EaseOutCubic(time_ / kZoomTime);

	if (captureSprite_ && captureSize_.x > 0.0f && captureSize_.y > 0.0f) {
		// 画面の縦横比で切り抜いて（はみ出す側を削る）、寄る分だけ真ん中を小さく切り抜く。
		// キャプチャは描画解像度（GetRenderWidth/Height）、画面は UI 座標なので画面全体に引き伸ばす（§3）
		const float screenAspect = screenW / screenH;
		Vector2 crop = captureSize_;
		if (crop.x / crop.y > screenAspect) crop.x = crop.y * screenAspect;
		else crop.y = crop.x / screenAspect;
		crop = { crop.x / zoom, crop.y / zoom };
		captureSprite_->SetPosition({ 0.0f, 0.0f });
		captureSprite_->SetSize({ screenW, screenH });
		captureSprite_->SetTextureLeftTop({ (captureSize_.x - crop.x) * 0.5f, (captureSize_.y - crop.y) * 0.5f });
		captureSprite_->SetTextureSize(crop);
		captureSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		GetSpriteManager()->DrawSetting();
		captureSprite_->Update();
		captureSprite_->Draw();
	} else {
		DrawFallbackBackground(screenW, screenH);
	}

	// 文字を読みやすくする暗がり（背景を見る間は消す）
	const float ui = Clamp01(uiAlpha_);
	if (ui > kUiVisibleAlpha) {
		tr->DrawRect({ 0.0f, 0.0f }, { screenW, screenH }, { 0.0f, 0.0f, 0.0f, kDimAlpha * ui });
		// 左上と右側を少し濃く（細い帯を重ねてなめらかに）
		constexpr int kStrips = 16;
		for (int i = 0; i < kStrips; ++i) {
			const float t = static_cast<float>(i) / kStrips;
			const float w = kSideShadeWidth / kStrips;
			tr->DrawRect({ t * kSideShadeWidth, 0.0f }, { w + 1.0f, screenH }, { 0.0f, 0.0f, 0.0f, kSideShadeAlpha * (1.0f - t) * ui });
			tr->DrawRect({ screenW - (t + 1.0f / kStrips) * kSideShadeWidth, 0.0f }, { w + 1.0f, screenH },
				{ 0.0f, 0.0f, 0.0f, kSideShadeAlpha * (1.0f - t) * ui });
		}
	}
}

void ResultScene::DrawFallbackBackground(float screenW, float screenH) {
	// 撃破の瞬間の 1 枚が無いとき（F2 のデバッグ遷移・B が無い間）：暗い紺の縦グラデーション＋ぼかした光の帯
	TextRenderer* tr = TextRenderer::GetInstance();
	constexpr int kRows = 30;
	const float rowH = screenH / kRows;
	for (int i = 0; i < kRows; ++i) {
		const float t = static_cast<float>(i) / (kRows - 1);
		const Vector4 c{ kFallbackBottomR + kFallbackRangeR * (1.0f - t), kFallbackBottomG + kFallbackRangeG * (1.0f - t), kFallbackBottomB + kFallbackRangeB * (1.0f - t), 1.0f };
		tr->DrawRect({ 0.0f, rowH * i }, { screenW, rowH + 1.0f }, c);
	}

	// 光の帯：細い横長の矩形を中心ほど濃く重ねる。ゆっくり上下に揺れる
	struct Band { float y; float height; Vector4 color; float speed; float phase; };
	const Band bands[] = {
		{ 0.38f, 140.0f, { 0.0f, 0.641f, 1.0f, 0.10f }, 0.21f, 0.0f },   // エーテルの水色
		{ 0.62f, 90.0f, { 1.0f, 0.82f, 0.45f, 0.07f }, 0.17f, 1.7f },    // 金
		{ 0.20f, 60.0f, { 0.6f, 0.75f, 1.0f, 0.05f }, 0.27f, 3.1f },
	};
	constexpr int kLayers = 10;
	for (const Band& b : bands) {
		const float cy = screenH * b.y + std::sin(time_ * b.speed + b.phase) * kBandSwayPixels;
		for (int k = 0; k < kLayers; ++k) {
			const float s = 1.0f - static_cast<float>(k) / kLayers;   // 外側ほど太く薄く
			const float h = b.height * s;
			tr->DrawRect({ 0.0f, cy - h * 0.5f }, { screenW, h }, b.color);
		}
	}
}

// ----- 左上：記録 -----

void ResultScene::DrawRecordPanel(float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float appear = EaseOutCubic((time_ - kRecordAppear) / kAppearTime);
	if (appear <= 0.0f) return;
	const float a = appear * uiAlpha;
	const float x = kRecordX - (1.0f - appear) * kSlideDistance;
	const float y = kRecordY;
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, a };

	tr->DrawRect({ x, y }, { kRecordW, kRecordH }, WithAlpha(kPanelColor, a));
	tr->DrawRect({ x, y }, { kAccentBarWidth, kRecordH }, WithAlpha(kGold, kAccentBarAlpha * a));

	tr->DrawText("STAGE CLEAR", { x + kRecordPadX, y + kRecordTitleY }, kRecordTitleScale, WithAlpha(kGold, a), kTextOutline, shadow);
	tr->DrawText(stageName_, { x + kRecordPadX, y + kStageNameY }, kStageNameScale, { 1.0f, 1.0f, 1.0f, a }, kTextOutlineBold, shadow);
	tr->DrawRect({ x + kRecordPadX, y + kRecordRuleY }, { kRecordW - kRecordRuleInsetTotal, kRecordRuleThickness }, WithAlpha(kGold, kRecordRuleAlpha * a));

	char purityBuf[32];
	std::snprintf(purityBuf, sizeof(purityBuf), "%.1f", purity_);
	char timeBuf[32];
	{
		const int cs = static_cast<int>(clearTime_ * kCentisecondsPerSecondF);
		const int mm = (std::min)(cs / kCentisecondsPerMinute, kMaxDisplayMinutes);
		std::snprintf(timeBuf, sizeof(timeBuf), "%02d' %02d\" %02d", mm, (cs / kCentisecondsPerSecond) % kSecondsPerMinute, cs % kCentisecondsPerSecond);
	}
	const std::string scoreText = WithCommas(score_);
	const std::string killText = WithCommas(killCount_);

	struct Row { const char* label; const char* value; };
	const Row rows[] = {
		{ "純度", purityBuf },
		{ "クリアタイム", timeBuf },
		{ "スコア", scoreText.c_str() },
		{ "撃破数", killText.c_str() },
	};
	constexpr float kLabelScale = 0.6f;
	constexpr float kValueScale = 0.8f;
	constexpr float kRowH = 46.0f;
	float ry = y + kRecordRowsTop;
	for (size_t i = 0; i < std::size(rows); ++i) {
		// 1 行ずつ少し遅れて出す
		const float rowA = a * Clamp01((time_ - kRecordAppear - kRecordRowStagger * static_cast<float>(i)) / kRecordRowFadeTime);
		tr->DrawText(rows[i].label, { x + kRecordRowPadX, ry + kRecordLabelOffsetY }, kLabelScale, { kLabelTextR, kLabelTextG, kLabelTextB, rowA }, kTextOutline,
			{ 0.0f, 0.0f, 0.0f, rowA });
		const float vw = tr->MeasureWidth(rows[i].value, kValueScale);
		tr->DrawText(rows[i].value, { x + kRecordW - kRecordRowPadX - vw, ry }, kValueScale, { 1.0f, 1.0f, 1.0f, rowA }, kTextOutline,
			{ 0.0f, 0.0f, 0.0f, rowA });
		ry += kRowH;
	}
}

void ResultScene::DrawEarnedEther(float uiAlpha) {
	// 記録の下：このステージで手に入れたエーテル（数え上がる）
	TextRenderer* tr = TextRenderer::GetInstance();
	const float appear = EaseOutCubic((time_ - kRecordAppear - kEarnedAppearDelay) / kAppearTime);
	if (appear <= 0.0f) return;
	const float a = appear * uiAlpha;
	const float x = kRecordX - (1.0f - appear) * kSlideDistance;
	const LootItemDef& ether = LootCatalog::GetInstance()->GetEther();
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, a };

	tr->DrawRect({ x, kEarnedY }, { kRecordW, kEarnedH }, WithAlpha(kPanelColor, a));
	tr->DrawRect({ x, kEarnedY }, { kAccentBarWidth, kEarnedH }, WithAlpha(ether.color, kAccentBarAlpha * a));
	DrawItemIcon(ether, { x + kRecordPadX, kEarnedY + kEarnedIconY }, kSmallIconSize, a);
	tr->DrawText("獲得エーテル", { x + kEarnedLabelX, kEarnedY + kEarnedLabelY }, kSmallTextScale, { kEtherLabelR, kEtherLabelG, kEtherLabelB, a }, kTextOutline, shadow);

	const float progress = Clamp01((time_ - kEtherStart) / kEtherCountTime);
	const int shown = static_cast<int>(static_cast<float>(earnedEther_) * EaseOutCubic(progress) + 0.5f);
	const std::string text = "+" + WithCommas(shown);
	const float scale = 0.95f;
	const float w = tr->MeasureWidth(text, scale);
	tr->DrawText(text, { x + kRecordW - kRecordRowPadX - w, kEarnedY + kEarnedValueY }, scale, WithAlpha(ether.color, a), kTextOutlineBold, shadow);
}

// ----- 右上：所持エーテル -----

void ResultScene::DrawHandEther(float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float appear = EaseOutCubic((time_ - kPanelAppear) / kAppearTime);
	if (appear <= 0.0f) return;
	const float a = appear * uiAlpha;
	const float x = kPanelX + (1.0f - appear) * kSlideDistance;
	const LootItemDef& ether = LootCatalog::GetInstance()->GetEther();
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, a };

	tr->DrawRect({ x, kHandY }, { kPanelW, kHandH }, WithAlpha(kPanelColor, a));
	tr->DrawRect({ x, kHandY + kHandH - kHandUnderlineH }, { kPanelW, kHandUnderlineH }, WithAlpha(ether.color, kHandUnderlineAlpha * a));
	DrawItemIcon(ether, { x + kHandIconX, kHandY + kHandIconY }, kSmallIconSize, a);
	tr->DrawText("所持エーテル", { x + kHandLabelX, kHandY + kHandLabelY }, kSmallTextScale, { kEtherLabelR, kEtherLabelG, kEtherLabelB, a }, kTextOutline, shadow);

	// 所持が無限の扱いの間は 99,999（賭ける画面と同じ）
	const EtherState* state = GetGameSession()->GetEtherState();
	const int hand = state->IsInfiniteHand() ? kHandDisplayMax
		: (std::min)(static_cast<int>(std::lround(state->GetHand())), kHandDisplayMax);
	const std::string text = WithCommas(hand);
	const float scale = 1.0f;
	const float w = tr->MeasureWidth(text, scale);
	const float valueX = x + kPanelW - kHandPadRight - w;
	tr->DrawText(text, { valueX, kHandY + kHandValueY }, scale, WithAlpha(ether.color, a), kTextOutlineBold, shadow);

	// 売った分の「+N」：数字の左に出て、少し上がりながら消える
	if (soldPopupTime_ < kSoldPopupTime && soldEtherPopup_ > 0) {
		const float t = soldPopupTime_ / kSoldPopupTime;
		const float pa = a * (1.0f - Clamp01((t - kPopupFadeStart) / kPopupFadeSpan)) * Clamp01(soldPopupTime_ / kPopupFadeIn);
		const std::string popup = "+" + WithCommas(soldEtherPopup_);
		const float ps = 0.7f;
		const float pw = tr->MeasureWidth(popup, ps);
		tr->DrawText(popup, { valueX - kPopupGap - pw, kHandY + kHandLabelY - kPopupRise * EaseOutCubic(t) }, ps,
			{ 1.0f, kPopupColorG, kPopupColorB, pa }, kTextOutline, { 0.0f, 0.0f, 0.0f, pa });
	}
}

// ----- 右のパネル：報酬のマスと説明 -----

void ResultScene::DrawRewardPanel(float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float appear = EaseOutCubic((time_ - kPanelAppear) / kAppearTime);
	if (appear <= 0.0f) return;
	const float a = appear * uiAlpha;
	const PanelLayout layout = MakeLayout((1.0f - appear) * kSlideDistance);
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, a };

	tr->DrawRect({ layout.panelX, kPanelY }, { kPanelW, kPanelH }, WithAlpha(kPanelColor, a));

	auto header = [&](const char* text, float y) {
		tr->DrawText(text, { layout.innerX, y }, kSmallTextScale, WithAlpha(kGold, a), kTextOutline, shadow);
		tr->DrawRect({ layout.innerX, y + kHeaderRuleY }, { layout.innerW, kHeaderRuleThickness }, WithAlpha(kGold, kHeaderRuleAlpha * a));
	};

	header("ステージ報酬", layout.stageHeaderY);
	DrawSlotGrid(stageSlots_, { layout.innerX, layout.stageGridY }, kStageSlotRows, 0, a);

	// 部位破壊報酬（壊していなければ空のマス）
	header("部位破壊報酬", layout.partHeaderY);
	DrawSlotGrid(partBreakSlots_, { layout.innerX, layout.partGridY }, kPartBreakSlotRows,
		static_cast<int>(stageSlots_.size()), a);

	if (sellOpen_) DrawSellWindow(layout, a);
	else DrawDescription(layout, a);
	DrawStageSelectButton(layout, a);
}

void ResultScene::DrawSlotGrid(const std::vector<RewardSlot>& slots, const Vector2& origin, int minRows,
	int selectableOffset, float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const int total = RowsFor(slots.size(), minRows) * kSlotColumns;

	for (int i = 0; i < total; ++i) {
		const Vector2 pos{ origin.x + (i % kSlotColumns) * (kSlotSize + kSlotGap), origin.y + (i / kSlotColumns) * (kSlotSize + kSlotGap) };
		const bool filled = i < static_cast<int>(slots.size());

		if (!filled) {
			// 空のマス
			tr->DrawRect(pos, { kSlotSize, kSlotSize }, { 1.0f, 1.0f, 1.0f, kEmptySlotFillAlpha * uiAlpha });
			DrawFrame(pos, { kSlotSize, kSlotSize }, 1.0f, { 1.0f, 1.0f, 1.0f, kEmptySlotFrameAlpha * uiAlpha });
			continue;
		}

		const RewardSlot& slot = slots[static_cast<size_t>(i)];
		const float appear = Clamp01((time_ - slot.startTime + kSlotAppearLead) / kSlotAppearTime);
		tr->DrawRect(pos, { kSlotSize, kSlotSize }, { 1.0f, 1.0f, 1.0f, kSlotFillAlpha * uiAlpha });
		DrawFrame(pos, { kSlotSize, kSlotSize }, 1.0f, { 1.0f, 1.0f, 1.0f, kSlotFrameAlpha * uiAlpha });
		if (appear <= 0.0f) continue;
		const float a = appear * uiAlpha;

		// 出たときに少し大きい所から収まる
		const float pop = 1.0f + kSlotPopScale * (1.0f - EaseOutCubic(appear));
		const float iconSize = kSlotSize * 0.5f * pop;
		DrawItemIcon(*slot.item, { pos.x + (kSlotSize - iconSize) * 0.5f, pos.y + kSlotIconOffsetY + (kSlotSize * 0.5f - iconSize) * 0.5f },
			iconSize, a);

		const float progress = Clamp01((time_ - slot.startTime) / kSlotCountTime);
		const int shown = static_cast<int>(static_cast<float>(slot.count) * progress + 0.5f);
		char buf[16];
		std::snprintf(buf, sizeof(buf), "×%d", shown);
		const float cs = 0.5f;
		const float cw = tr->MeasureWidth(buf, cs);
		tr->DrawText(buf, { pos.x + kSlotSize - kSlotCountPadRight - cw, pos.y + kSlotSize - kSlotCountPadBottom }, cs, { 1.0f, 1.0f, 1.0f, a }, kTextOutline,
			{ 0.0f, 0.0f, 0.0f, a });

		// 選んでいるマスの枠（ゆっくり明滅）
		if (selectableOffset + i == selected_) {
			const float pulse = kPulseMinAlpha + kPulseAlphaRange * std::sin(time_ * kPulseSpeed);
			DrawFrame({ pos.x - kSelectFrameOffset, pos.y - kSelectFrameOffset }, { kSlotSize + kSelectFrameGrow, kSlotSize + kSelectFrameGrow }, kSelectFrameThickness, WithAlpha(kGold, pulse * a));
		}
	}
}

void ResultScene::DrawItemIcon(const LootItemDef& item, const Vector2& pos, float size, float alpha) {
	// アイコンの画像はまだ無い（23 番 §2.1）。色付きの図形で代わりにする。画像ができたらここだけ差し替える
	TextRenderer* tr = TextRenderer::GetInstance();
	const Vector4& c = item.color;
	tr->DrawRect(pos, { size, size }, { c.x * kIconBaseDarken, c.y * kIconBaseDarken, c.z * kIconBaseDarken, alpha });
	const float inset = size * kIconInsetRatio;
	tr->DrawRect({ pos.x + inset, pos.y + inset }, { size - inset * 2.0f, size - inset * 2.0f }, WithAlpha(c, alpha));
	// 左上のつや
	tr->DrawRect({ pos.x + inset, pos.y + inset }, { (size - inset * 2.0f) * kIconGlossW, (size - inset * 2.0f) * kIconGlossH },
		{ 1.0f, 1.0f, 1.0f, kIconGlossAlpha * alpha });
}

void ResultScene::DrawDescription(const PanelLayout& layout, float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const Vector2 origin{ layout.innerX, layout.descY };
	const float width = layout.innerW;
	tr->DrawRect(origin, { width, layout.descH }, { 0.0f, 0.0f, 0.0f, kDescBgAlpha * uiAlpha });
	DrawFrame(origin, { width, layout.descH }, 1.0f, { 1.0f, 1.0f, 1.0f, kDescFrameAlpha * uiAlpha });
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, uiAlpha };

	const RewardSlot* slot = SelectableAt(selected_);
	if (!slot || !slot->item) {
		const char* text = (SelectableCount() == 0) ? "素材は手に入らなかった" : "素材を選ぶと説明が出る";
		tr->DrawText(text, { origin.x + kDescPad, origin.y + kDescPad }, kDescEmptyScale, { kDescEmptyGray, kDescEmptyGray, kDescEmptyGray, kDescEmptyAlpha * uiAlpha }, kTextOutline, shadow);
		return;
	}
	// まだ出ていないマスの説明は出さない（数え上げの順を崩さない）
	const float a = uiAlpha * Clamp01((time_ - slot->startTime + kSlotAppearLead) / kSlotAppearTime);
	if (a <= 0.0f) return;
	const Vector4 textShadow{ 0.0f, 0.0f, 0.0f, a };
	const LootItemDef& item = *slot->item;

	DrawItemIcon(item, { origin.x + kDescPad, origin.y + kDescPad }, kDescIconSize, a);
	tr->DrawText(item.displayName, { origin.x + kDescNameX, origin.y + kDescTopRowY }, kDescNameScale, { 1.0f, 1.0f, 1.0f, a }, kTextOutline, textShadow);

	char rarityBuf[32];
	std::snprintf(rarityBuf, sizeof(rarityBuf), "レア度 %d", item.rarity);
	tr->DrawText(rarityBuf, { origin.x + kDescRarityX, origin.y + kDescSecondRowY }, kDescSmallScale, WithAlpha(RarityColor(item.rarity), a), kTextOutline,
		textShadow);

	char countBuf[48];
	std::snprintf(countBuf, sizeof(countBuf), "%s ×%d", IsPartBreakIndex(selected_) ? "部位破壊" : "今回", slot->count);
	const float cw = tr->MeasureWidth(countBuf, kDescCountScale);
	tr->DrawText(countBuf, { origin.x + width - kDescPad - cw, origin.y + kDescTopRowY }, kDescCountScale, { kDescCountGray, kDescCountGray, kDescCountGray, a }, kTextOutline,
		textShadow);
	if (item.sellPrice > 0) {
		const std::string price = "売値 " + WithCommas(item.sellPrice);
		const float pw = tr->MeasureWidth(price, kDescSmallScale);
		tr->DrawText(price, { origin.x + width - kDescPad - pw, origin.y + kDescSecondRowY }, kDescSmallScale, { kPriceR, kPriceG, kPriceB, a }, kTextOutline,
			textShadow);
	}

	tr->DrawRect({ origin.x + kDescPad, origin.y + kDescRuleY }, { width - kDescRuleInsetTotal, 1.0f }, { 1.0f, 1.0f, 1.0f, kDescRuleAlpha * a });
	float ly = origin.y + kDescTextTop;
	for (std::string_view line : SplitLines(item.description)) {
		tr->DrawText(line, { origin.x + kDescPad, ly }, kDescSmallScale, { kDescTextR, kDescTextG, kDescTextB, a }, kTextOutline, textShadow);
		ly += kDescLineHeight;
	}
}

void ResultScene::DrawSellWindow(const PanelLayout& layout, float uiAlpha) {
	// 売る数の小窓（説明の欄に重ねる）：素材の名前 → 左に ▲ 数 ▼・右に合計 → [売る] [やめる]
	TextRenderer* tr = TextRenderer::GetInstance();
	const RewardSlot* slot = SelectableAt(selected_);
	if (!slot || !slot->item) return;
	const LootItemDef& item = *slot->item;
	const Vector2 origin{ layout.innerX, layout.descY };
	const float width = layout.innerW;
	const float a = uiAlpha;
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, a };

	tr->DrawRect(origin, { width, layout.descH }, { kSellBgR, kSellBgG, kSellBgB, kSellBgAlpha * a });
	DrawFrame(origin, { width, layout.descH }, kSellFrameThickness, WithAlpha(kGold, kSellFrameAlpha * a));

	DrawItemIcon(item, { origin.x + kDescPad, origin.y + kSellIconY }, kSmallIconSize, a);
	const std::string title = item.displayName + " を売る";
	tr->DrawText(title, { origin.x + kSellTitleX, origin.y + kSellTitleY }, kSellTitleScale, { 1.0f, 1.0f, 1.0f, a }, kTextOutline, shadow);

	// 左：▲ 数 / 持っている数 ▼（↑↓・クリックで増減）
	const Rect up = SellArrowRect(layout, +1);
	const Rect down = SellArrowRect(layout, -1);
	const bool canUp = sellAmount_ < slot->count;
	const bool canDown = sellAmount_ > 1;
	auto arrow = [&](const Rect& r, const char* glyph, bool enabled) {
		const float gw = tr->MeasureWidth(glyph, kSellArrowScale);
		tr->DrawText(glyph, { r.pos.x + (r.size.x - gw) * 0.5f, r.pos.y + kSellArrowGlyphY }, kSellArrowScale,
			WithAlpha(kGold, (enabled ? 1.0f : kSellArrowDisabledAlpha) * a), kTextOutline, shadow);
	};
	arrow(up, "▲", canUp);
	arrow(down, "▼", canDown);
	char amountBuf[32];
	std::snprintf(amountBuf, sizeof(amountBuf), "%d / %d", sellAmount_, slot->count);
	const float amountCenterX = up.pos.x + up.size.x * 0.5f;
	const float aw = tr->MeasureWidth(amountBuf, kSellAmountScale);
	tr->DrawText(amountBuf, { amountCenterX - aw * 0.5f, layout.descY + kSellAmountY }, kSellAmountScale, { 1.0f, 1.0f, 1.0f, a }, kTextOutline, shadow);

	// 右：合計
	const LootItemDef& ether = LootCatalog::GetInstance()->GetEther();
	const float totalCenterX = origin.x + width * 0.5f + kSellTotalOffsetX;
	const char* totalLabel = "合計";
	const float lw = tr->MeasureWidth(totalLabel, kSellTotalLabelScale);
	tr->DrawText(totalLabel, { totalCenterX - lw * 0.5f, layout.descY + kSellTotalLabelY }, kSellTotalLabelScale, { kLabelTextR, kLabelTextG, kLabelTextB, a }, kTextOutline, shadow);
	const std::string total = "エーテル +" + WithCommas(item.sellPrice * sellAmount_);
	const float tw = tr->MeasureWidth(total, kSellTotalScale);
	tr->DrawText(total, { totalCenterX - tw * 0.5f, layout.descY + kSellTotalY }, kSellTotalScale, WithAlpha(ether.color, a), kTextOutline, shadow);

	// [売る] [やめる]：←→・マウスで選んだほうに明滅する枠
	auto button = [&](const Rect& r, const char* label, bool focused) {
		tr->DrawRect(r.pos, r.size, focused ? WithAlpha(kGold, kFocusedFillAlpha * a) : Vector4{ 1.0f, 1.0f, 1.0f, kButtonFillAlpha * a });
		if (focused) {
			const float pulse = kPulseMinAlpha + kPulseAlphaRange * std::sin(time_ * kPulseSpeed);
			DrawFrame({ r.pos.x - kSelectFrameOffset, r.pos.y - kSelectFrameOffset }, { r.size.x + kSelectFrameGrow, r.size.y + kSelectFrameGrow }, kSelectFrameThickness, WithAlpha(kGold, pulse * a));
		} else {
			DrawFrame(r.pos, r.size, 1.0f, { 1.0f, 1.0f, 1.0f, kSellButtonFrameAlpha * a });
		}
		const float bw = tr->MeasureWidth(label, kSellButtonTextScale);
		tr->DrawText(label, { r.pos.x + (r.size.x - bw) * 0.5f, r.pos.y + kSellButtonTextY }, kSellButtonTextScale, { 1.0f, 1.0f, 1.0f, a }, kTextOutline, shadow);
	};
	button(SellButtonRect(layout, true), "売る", !sellFocusCancel_);
	button(SellButtonRect(layout, false), "やめる", sellFocusCancel_);
}

void ResultScene::DrawStageSelectButton(const PanelLayout& layout, float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const Rect r = ButtonRect(layout);
	const bool selected = (selected_ == ButtonIndex()) && !sellOpen_;
	const float a = uiAlpha * (IsCounting() ? kCountingButtonAlpha : 1.0f);
	tr->DrawRect(r.pos, r.size, selected ? WithAlpha(kGold, kFocusedFillAlpha * a) : Vector4{ 1.0f, 1.0f, 1.0f, kButtonFillAlpha * a });
	if (selected) {
		const float pulse = kPulseMinAlpha + kPulseAlphaRange * std::sin(time_ * kPulseSpeed);
		DrawFrame({ r.pos.x - kSelectFrameOffset, r.pos.y - kSelectFrameOffset }, { r.size.x + kSelectFrameGrow, r.size.y + kSelectFrameGrow }, kSelectFrameThickness, WithAlpha(kGold, pulse * a));
	} else {
		DrawFrame(r.pos, r.size, 1.0f, { 1.0f, 1.0f, 1.0f, kStageButtonFrameAlpha * a });
	}
	const char* label = "ステージ選択へ";
	const float lw = tr->MeasureWidth(label, kStageButtonTextScale);
	tr->DrawText(label, { r.pos.x + (r.size.x - lw) * 0.5f, r.pos.y + kStageButtonTextY }, kStageButtonTextScale, { 1.0f, 1.0f, 1.0f, a }, kTextOutline,
		{ 0.0f, 0.0f, 0.0f, a });
}

// ----- 下：操作の案内 -----

void ResultScene::DrawHint(float screenW, float screenH, float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float hs = 0.55f;

	if (backgroundOnly_) {
		// 背景を見ている間：戻り方だけ少しの間出して消す
		const float a = 1.0f - Clamp01((backgroundToggleTime_ - kBgHintShowSeconds) / kBgHintFadeSeconds);
		if (a <= 0.0f) return;
		const char* hint = "キャンセル：戻る";
		const float hw = tr->MeasureWidth(hint, hs);
		tr->DrawText(hint, { (screenW - hw) * 0.5f, screenH - kBgHintBottom }, hs, { 1.0f, 1.0f, 1.0f, kBgHintAlpha * a }, kTextOutline,
			{ 0.0f, 0.0f, 0.0f, a });
		return;
	}

	const float appear = Clamp01((time_ - kPanelAppear) / kHintAppearTime) * uiAlpha;
	if (appear <= 0.0f) return;
	const char* hint = nullptr;
	if (IsCounting()) hint = "決定：スキップ　　キャンセル：背景を見る";
	else if (sellOpen_) hint = "↑↓：売る数　　←→：売る／やめる　　決定：選んだほう　　キャンセル：やめる";
	else if (selected_ == ButtonIndex()) hint = "十字キー：素材を選ぶ　　決定：ステージ選択へ　　キャンセル：背景を見る";
	else hint = "十字キー：素材を選ぶ　　決定：売る　　キャンセル：背景を見る　　Esc：ステージ選択へ";
	const float hw = tr->MeasureWidth(hint, hs);
	tr->DrawRect({ 0.0f, screenH - kHintBarTop }, { screenW, kHintBarH }, { 0.0f, 0.0f, 0.0f, kHintBarAlpha * appear });
	tr->DrawText(hint, { (screenW - hw) * 0.5f, screenH - kHintTextBottom }, hs, { 1.0f, 1.0f, 1.0f, kHintTextAlpha * appear }, kTextOutline,
		{ 0.0f, 0.0f, 0.0f, appear });
}

void ResultScene::DrawFrame(const Vector2& pos, const Vector2& size, float thickness, const Vector4& color) {
	TextRenderer* tr = TextRenderer::GetInstance();
	tr->DrawRect(pos, { size.x, thickness }, color);
	tr->DrawRect({ pos.x, pos.y + size.y - thickness }, { size.x, thickness }, color);
	tr->DrawRect({ pos.x, pos.y }, { thickness, size.y }, color);
	tr->DrawRect({ pos.x + size.x - thickness, pos.y }, { thickness, size.y }, color);
}

Camera* ResultScene::GetCamera() {
	return camera_.get();
}
