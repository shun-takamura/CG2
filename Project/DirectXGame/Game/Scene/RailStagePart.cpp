#include "RailStagePart.h"

#include "Camera.h"
#include "Components/EntityTag.h"
#include "Components/Gameplay.h"
#include "Components/Prefab.h"
#include "Components/PrefabManager.h"
#include "Enemy/EnemyCommandFactory.h"
#include "Enemy/EnemyController.h"
#include "IImGuiEditable.h"
#include "Json/JsonValue.h"
#include "LogBuffer.h"
#include "MathUtility.h"
#include "Score/ScoreManager.h"
#include "Spline/CameraRotKey.h"
#include "Spline/RailAimController.h"
#include "Spline/RailCameraController.h"
#include "Spline/SplineCurveActor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#ifdef _DEBUG
#include "ImGuiManager.h"
#include "ViewportWindow.h"
#include "imgui.h"
#endif

RailStagePart::RailStagePart() = default;
RailStagePart::~RailStagePart() = default;

void RailStagePart::Initialize(IRailStageHost* host, Camera* camera) {
	host_ = host;
	camera_ = camera;

	// レールカメラ用スプライン（位置）。
	// 実体はシーンの dynamicSplines_ 側に置く＝シーン JSON に載るので、
	// エンジン内エディタでも Blender でも編集できる（従来はここに直書きで、
	// カメラを動かすたびに C++ の編集とリビルドが必要だった）。
	// ここの値はシーンにまだ CameraPathSpline が無いときの種。
	defaultCameraPoints_ = {
		{   0.0f, 5.0f,   0.0f },
		{  10.0f, 5.0f,  20.0f },
		{  20.0f, 8.0f,  40.0f },
		{  30.0f, 5.0f,  60.0f },
		{  40.0f, 5.0f,  80.0f },
	};

	// レールカメラコントローラ（向きは回転キーフレーム列 cameraRotKeys_。初期は空）
	railCamera_ = std::make_unique<RailCameraController>();
	railCamera_->Initialize(camera_);
	RebindCameraPath();
	railCamera_->SetRotKeys(&cameraRotKeys_);
	railCamera_->SetSpeed(railCameraSpeed_);
	// ラップ/スナップバック防止：SeekMax 到達後の凍結を尊重するため loop しない
	// （主防御は UpdateCamera 内の SeekMax しきい値判定。これは副防御）。
	railCamera_->SetLoop(false);

	// 向きオーサリング用の見回しローテータ（入力配線・UI は Tuning 側で）
	railAim_ = std::make_unique<RailAimController>();


	// ウェーブ定義ロード（存在しなければ空のまま=何も湧かない）
	if (std::filesystem::exists(wavePath_)) {
		if (WaveDefIO::LoadFromFile(wavePath_, currentWave_)) {
			spawnFired_.assign(currentWave_.entries.size(), false);
			retreatFired_.assign(currentWave_.entries.size(), false);
			killAtT_.assign(currentWave_.entries.size(), -1.0f);
		}
	}
}

void RailStagePart::RebindCameraPath() {
	if (!host_) return;
	// シーンのロードで dynamicSplines_ は作り直されるため、以前のポインタは解放済み。
	// ここで取り直す（無ければ既定値を種にシーン側が作る）。
	cameraPath_ = host_->EnsureCameraPathSpline(defaultCameraPoints_);
	if (railCamera_) railCamera_->SetCameraPath(cameraPath_);
}

void RailStagePart::MarkWaveFileSynced() {
	std::error_code ec;
	auto t = std::filesystem::last_write_time(wavePath_, ec);
	if (ec) return;
	waveLastWriteTime_ = t;
	waveWatchInitialized_ = true;
}

void RailStagePart::RefreshWaveIfChanged() {
	if (!autoReloadWave_) return;

	std::error_code ec;
	auto t = std::filesystem::last_write_time(wavePath_, ec);
	if (ec) return;  // ファイルがまだ無い等

	if (!waveWatchInitialized_) {
		waveLastWriteTime_ = t;
		waveWatchInitialized_ = true;
		return;
	}
	if (t == waveLastWriteTime_) return;

	// 読み込みの成否に関わらず記録を進める（失敗時に毎フレ再試行しない）。
	// 書き込み途中の JSON を掴んだ場合は false が返るだけで currentWave_ は壊れない。
	// 書き込み完了で時刻が再び動くので、次のフレームで拾い直せる。
	waveLastWriteTime_ = t;
	ReloadWaveNow();
}

void RailStagePart::ReloadWaveNow() {
	WaveDef loaded;
	if (!WaveDefIO::LoadFromFile(wavePath_, loaded)) return;
	currentWave_ = std::move(loaded);

	// 配置を確認したいので撃破済みの記録は捨てて全部出し直す。
	// （Seek が時刻からスポーン/退避フラグを再計算するので、ここでは初期化だけ）
	spawnFired_.assign(currentWave_.entries.size(), false);
	retreatFired_.assign(currentWave_.entries.size(), false);
	killAtT_.assign(currentWave_.entries.size(), -1.0f);

	// 現在時刻で組み直す（既存の敵を掃除し、今いるべき敵を新しい定義で復元）
	Seek(GetStageSeconds());
	MarkWaveFileSynced();  // 読み直した内容が最新＝直後に再検出しない
	LogBuffer::Instance().Add("Wave reloaded: " + wavePath_, LogBuffer::Level::Info);
}

bool RailStagePart::UpdateCamera(class InputActionMap* actions, float scaledDt, float seekMaxSec) {
	(void)actions;
	if (!host_) return false;

	// レールカメラ向きキーは t 昇順を常に保つ（Inspector の t 編集もここで反映）
	if (cameraRotKeys_.size() > 1) {
		std::sort(cameraRotKeys_.begin(), cameraRotKeys_.end(),
			[](const std::unique_ptr<CameraRotKey>& a, const std::unique_ptr<CameraRotKey>& b) {
				return a->t < b->t;
			});
	}

	// Aim オーサリング：ゲームを完全フリーズ（全 TimeGroup を 0）しつつ Update 自体は通常どおり回す。
	if (aimAuthoring_ && !prevAimAuthoring_) {
		// 立ち上がり：現在の TimeScale を退避し、見回し開始姿勢を現在のカメラから seed
		for (int i = 0; i < static_cast<int>(TimeGroup::Count); ++i) {
			prevTimeScales_[i] = host_->GetTimeScale(static_cast<TimeGroup>(i));
		}
		if (railAim_ && camera_) {
			railAim_->SetEuler(camera_->GetRotate());
			camera_->StopShake();
		}
	} else if (!aimAuthoring_ && prevAimAuthoring_) {
		// 立ち下がり：退避した TimeScale を復元
		for (int i = 0; i < static_cast<int>(TimeGroup::Count); ++i) {
			host_->SetTimeScale(static_cast<TimeGroup>(i), prevTimeScales_[i]);
		}
	}
	prevAimAuthoring_ = aimAuthoring_;

	if (aimAuthoring_) {
		// フリーズ維持（毎フレーム 0 を当てる）
		for (int i = 0; i < static_cast<int>(TimeGroup::Count); ++i) {
			host_->SetTimeScale(static_cast<TimeGroup>(i), 0.0f);
		}
	}

	// スプライン可視化は不要（走行スプラインはシーンの dynamicSplines_ が持つので
	// GameScene::Draw 側が他のスプラインと同じように DrawDebug する）

	// レール走行（Aim オーサリング中はレール位置に固定し、見回し入力で向きを上書き）
	if (railCamera_) {
		// SeekMax 到達：このフレームは railCamera_->Update() を呼ばない＝直前ポーズで凍結
		if (seekMaxSec > 0.0f && GetStageSeconds() >= seekMaxSec) {
			return true;
		}

		if (aimAuthoring_ && cameraPath_ && railAim_ && camera_) {
			const Vector3 eye = cameraPath_->Sample(railCamera_->GetProgress());
#ifdef _DEBUG
			auto* vp = ImGuiManager::Instance().GetViewportWindow();
			if (vp && vp->IsHovered()) {
				ImGuiIO& io = ImGui::GetIO();
				if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
					if (io.KeyAlt) railAim_->AddRoll(io.MouseDelta.x);
					else           railAim_->AddYawPitch(io.MouseDelta.x, io.MouseDelta.y);
				}
			}
#endif
			railAim_->Apply(camera_, eye);
		} else {
			railCamera_->Update(scaledDt);
		}
	}

	return false;
}

void RailStagePart::UpdateWaveAndEnemies(float worldDt) {
	if (!host_) return;

	// Blender から敵配置を Export したらその場で反映する（変化時だけ走るので軽い）
	RefreshWaveIfChanged();

	const bool gameFrozen = worldDt <= 0.0001f;

	if (!gameFrozen) {
		// SweepDeadEntities の前に、HP がゼロになった敵のスポーンエントリに kill t を記録
		const float currentT = railCamera_ ? railCamera_->GetProgress() : 0.0f;
		host_->ForEachMovingEnemy([&](IImGuiEditable* entity, int waveEntryIndex) {
			if (waveEntryIndex < 0) return;
			if (static_cast<size_t>(waveEntryIndex) >= killAtT_.size()) return;
			if (killAtT_[waveEntryIndex] >= 0.0f) return; // 既記録
			if (Gameplay::Of(entity).GetHP().IsDead()) {
				killAtT_[waveEntryIndex] = currentT;
				// 撃破で加点（プレハブ側で設定された scoreValue を使う）
				ScoreManager::GetInstance()->AddScore(Gameplay::Of(entity).GetScoreValue());
			}
		});

		// HP がゼロになった敵などを破棄キューへ
		host_->SweepDeadEntities();
	}

	// スポーン：カメラ進行度 t でエントリをトリガー
	if (!gameFrozen) {
		const float currentT = railCamera_ ? railCamera_->GetProgress() : 0.0f;
		// ステージ開始からの経過秒（進行度 t を全体尺で割る）。スポーン/退避判定の基準。
		const float nowSec = (railCameraSpeed_ > 1e-8f) ? currentT / railCameraSpeed_ : 0.0f;
		for (size_t i = 0; i < currentWave_.entries.size(); ++i) {
			const WaveEntry& we = currentWave_.entries[i];

			// スポーントリガー
			if (i < spawnFired_.size() && !spawnFired_[i] && nowSec >= we.triggerSec) {
				const PrefabDef* pdef = PrefabManager::GetInstance()->Find(we.prefab);
				// 移動方法がカメラ相対（ScreenHover/Static）か、エントリにカメラオフセット指定があれば
				// スプラインを使わずカメラ相対位置に出現させる。
				const bool cameraRelative = we.useCameraOffset ||
					(pdef && pdef->hasMovement &&
						(pdef->movementType == MovementType::ScreenHover ||
						 pdef->movementType == MovementType::Static));

				// このトリガーで湧いた敵を集める（positions[] 指定時は複数体になりうる）
				std::vector<IImGuiEditable*> spawnedList;

				if (!we.positions.empty() && !cameraRelative) {
					// ワールド固定敵（Blender 配置の固定砲台・地上設置物など）。
					// 各座標に 1 体ずつ置き、位置はコントローラが固定する（spline=null/speed=0）。
					for (const auto& wp : we.positions) {
						if (IImGuiEditable* s = host_->SpawnEnemyAt(we.prefab, wp)) {
							host_->RegisterStationaryMovingEnemy(s, static_cast<int>(i));
							spawnedList.push_back(s);
						}
					}
				} else if (cameraRelative) {
					const Vector3 wpos = CameraOffsetToWorld(we.cameraOffset);
					if (IImGuiEditable* s = host_->SpawnEnemyAt(we.prefab, wpos)) {
						// 撃破検知・コントローラ紐付け・Seek 掃除を共通化するため
						// movingEnemies_ にも登録（spline=null/speed=0 なので位置はコントローラが制御）。
						host_->RegisterStationaryMovingEnemy(s, static_cast<int>(i));
						spawnedList.push_back(s);
					}
				} else if (!we.splineId.empty()) {
					SplineCurveActor* sp = host_->FindDynamicSplineByName(we.splineId);
					if (sp) {
						// Rusher は終端で止まる（removeAtEnd=false）
						const bool removeAtEnd = (we.enemyType != "Rusher");
						// traverse_sec [秒] → スプライン速度 [spline_t/sec]。速度 = 1 / 踏破秒。
						const float enemySpeed = (we.traverseSec > 1e-4f)
							? (1.0f / we.traverseSec) : 0.0f;
						if (IImGuiEditable* s = host_->SpawnEnemyOnSpline(we.prefab, sp, enemySpeed,
							removeAtEnd, 0.0f, static_cast<int>(i))) {
							spawnedList.push_back(s);
						}
					} else {
						LogBuffer::Instance().Add(
							std::string("Wave: spline not found: ") + we.splineId,
							LogBuffer::Level::Warning);
					}
				}

				// 湧いた敵ごとに EnemyController を生成してコマンドを設定
				for (IImGuiEditable* spawned : spawnedList) {
					auto ctrl = std::make_unique<EnemyController>();
					ctrl->entity_           = spawned;
					ctrl->waveEntryIndex_   = static_cast<int>(i);
					ctrl->billboardToPlayer_ = (we.enemyType != "Carrier");
					ctrl->triggerSec_       = we.triggerSec;
					ctrl->shootIntervalSec_ = we.shootIntervalSec;
					ctrl->spawnIntervalSec_ = we.spawnIntervalSec;
					ctrl->spawnLimit_       = we.spawnLimit;
					// 子敵は明示指定があればそれを、なければ自身のプレハブ／スプラインにフォールバック
					ctrl->childPrefab_      = we.childPrefab.empty()    ? we.prefab   : we.childPrefab;
					ctrl->childSplineId_    = we.childSplineId.empty()  ? we.splineId : we.childSplineId;
					// ScreenHover 用パラメータ（移動はプレハブ駆動）
					ctrl->hoverOffset_      = we.cameraOffset;
					if (pdef && pdef->hasMovement) {
						ctrl->hoverApproachSpeed_ = pdef->hoverApproachSpeed;
						ctrl->hoverHoldDuration_  = pdef->hoverHoldDuration;
					}
					ctrl->Init(EnemyCommandFactory::Create(we, pdef));

					// movingEnemies_ にコントローラを紐付け
					host_->LinkEnemyController(spawned, ctrl.get());
					host_->RegisterEnemyController(std::move(ctrl));
				}
				spawnFired_[i] = true;
			}

			// 退避トリガー
			if (i < retreatFired_.size() && i < spawnFired_.size()
				&& spawnFired_[i] && !retreatFired_[i]
				&& we.retreatSec >= 0.0f && nowSec >= we.retreatSec) {
				host_->TriggerRetreatForWaveEntry(static_cast<int>(i));
				retreatFired_[i] = true;
			}
		}
	}

	// 敵コントローラ更新（自由移動・ビルボード・退避完了処理）
	if (!gameFrozen) {
		const float cameraT = railCamera_ ? railCamera_->GetProgress() : 0.0f;
		const float stageSec = (railCameraSpeed_ > 1e-8f) ? cameraT / railCameraSpeed_ : 0.0f;
		host_->UpdateEnemyControllers(worldDt, host_->GetPlayer(), stageSec);
	}

	// スプライン追従敵の進行
	if (!gameFrozen) {
		host_->UpdateMovingEnemies(worldDt);
	}
}

void RailStagePart::Seek(float seconds) {
	// RailCamera の進行度を経過秒から再構築する（speed を尊重。loop はしない＝クランプ）
	if (railCamera_) {
		float t = seconds * railCameraSpeed_;
		t = std::clamp(t, 0.0f, 1.0f);
		railCamera_->SetProgress(t);
		// Seek 結果を即カメラに反映（dt=0 で Update）
		railCamera_->Update(0.0f);
		if (camera_) camera_->Update();
	}

	if (!host_) return;

	// ----- ゲーム状態を Seek 先に合わせてリセット -----
	// 現在生きている敵・弾・スプライン追従敵・敵コントローラをすべて掃除
	host_->ClearWaveRuntimeState();
	host_->ResetDodgeState();

	// スポーン/退避フラグ / kill t を Seek 先に合わせて再構築
	const float seekT = railCamera_ ? railCamera_->GetProgress() : 0.0f;

	if (spawnFired_.size() != currentWave_.entries.size())
		spawnFired_.assign(currentWave_.entries.size(), false);
	if (retreatFired_.size() != currentWave_.entries.size())
		retreatFired_.assign(currentWave_.entries.size(), false);
	if (killAtT_.size() != currentWave_.entries.size())
		killAtT_.assign(currentWave_.entries.size(), -1.0f);

	// seek 先より後の kill は未発生扱いに戻す
	for (size_t i = 0; i < killAtT_.size(); ++i) {
		if (killAtT_[i] > seekT) killAtT_[i] = -1.0f;
	}

	// スコアは Seek では再構築しない（開発時のみ Seek を使う想定。
	// 厳密にやるなら killAtT_ ごとに wave→prefab→scoreValue を引く必要があり実装コスト高）
	ScoreManager::GetInstance()->Reset();

	// seek 先の経過秒（進行度 t を全体尺で割る）
	const float seekSec = (railCameraSpeed_ > 1e-8f) ? seekT / railCameraSpeed_ : 0.0f;

	// フラグを seekSec から再構築
	for (size_t i = 0; i < currentWave_.entries.size(); ++i) {
		const WaveEntry& we = currentWave_.entries[i];
		spawnFired_[i]  = (seekSec >= we.triggerSec);
		retreatFired_[i] = (we.retreatSec >= 0.0f && seekSec >= we.retreatSec);
	}

	// 生存中の敵を正しい位置で復元（スプライン上 / カメラ相対）
	for (size_t i = 0; i < currentWave_.entries.size(); ++i) {
		const WaveEntry& we = currentWave_.entries[i];
		if (!spawnFired_[i]) continue;
		if (retreatFired_[i]) continue;
		if (killAtT_[i] >= 0.0f) continue;

		const PrefabDef* pdef = PrefabManager::GetInstance()->Find(we.prefab);
		const bool cameraRelative = we.useCameraOffset ||
			(pdef && pdef->hasMovement &&
				(pdef->movementType == MovementType::ScreenHover ||
				 pdef->movementType == MovementType::Static));

		std::vector<IImGuiEditable*> spawnedList;
		if (!we.positions.empty() && !cameraRelative) {
			// ワールド固定敵は seek で動かないのでそのまま各座標へ復元
			for (const auto& wp : we.positions) {
				if (IImGuiEditable* s = host_->SpawnEnemyAt(we.prefab, wp)) {
					host_->RegisterStationaryMovingEnemy(s, static_cast<int>(i));
					spawnedList.push_back(s);
				}
			}
		} else if (cameraRelative) {
			// カメラ相対敵は seek した瞬間のカメラ基準で再配置（停止状態から再開）。
			// Seek は開発ツールなので hover の経過時間までは厳密復元しない。
			if (IImGuiEditable* s = host_->SpawnEnemyAt(we.prefab, CameraOffsetToWorld(we.cameraOffset))) {
				host_->RegisterStationaryMovingEnemy(s, static_cast<int>(i));
				spawnedList.push_back(s);
			}
		} else {
			if (we.splineId.empty()) continue;
			// 経過秒 / 踏破秒 = スプライン上の進捗 t
			if (we.traverseSec < 1e-4f) continue;
			const float tOnSpline = std::clamp((seekSec - we.triggerSec) / we.traverseSec, 0.0f, 1.0f);
			if (tOnSpline >= 1.0f) continue;

			SplineCurveActor* sp = host_->FindDynamicSplineByName(we.splineId);
			if (!sp) continue;
			const bool removeAtEnd = (we.enemyType != "Rusher");
			const float enemySpeed = 1.0f / we.traverseSec;
			if (IImGuiEditable* s = host_->SpawnEnemyOnSpline(
				we.prefab, sp, enemySpeed, removeAtEnd, tOnSpline, static_cast<int>(i))) {
				spawnedList.push_back(s);
			}
		}

		// Seek 復元された敵にも EnemyController を作って AI を再開させる
		for (IImGuiEditable* spawned : spawnedList) {
			auto ctrl = std::make_unique<EnemyController>();
			ctrl->entity_           = spawned;
			ctrl->waveEntryIndex_   = static_cast<int>(i);
			ctrl->billboardToPlayer_ = (we.enemyType != "Carrier");
			ctrl->triggerSec_       = we.triggerSec;
			ctrl->shootIntervalSec_ = we.shootIntervalSec;
			ctrl->spawnIntervalSec_ = we.spawnIntervalSec;
			ctrl->spawnLimit_       = we.spawnLimit;
			ctrl->childPrefab_      = we.childPrefab.empty()   ? we.prefab   : we.childPrefab;
			ctrl->childSplineId_    = we.childSplineId.empty() ? we.splineId : we.childSplineId;
			ctrl->hoverOffset_      = we.cameraOffset;
			if (pdef && pdef->hasMovement) {
				ctrl->hoverApproachSpeed_ = pdef->hoverApproachSpeed;
				ctrl->hoverHoldDuration_  = pdef->hoverHoldDuration;
			}
			ctrl->Init(EnemyCommandFactory::Create(we, pdef));

			host_->LinkEnemyController(spawned, ctrl.get());
			host_->RegisterEnemyController(std::move(ctrl));
		}
	}
}

float RailStagePart::GetCameraProgressT() const {
	return railCamera_ ? railCamera_->GetProgress() : -1.0f;
}

float RailStagePart::GetStageSeconds() const {
	const float t = GetCameraProgressT();
	if (t < 0.0f) return 0.0f;
	return (railCameraSpeed_ > 1e-8f) ? (t / railCameraSpeed_) : 0.0f;
}

void RailStagePart::LoadFromJson(const JsonValue& root) {
	railCameraSpeed_ = static_cast<float>(
		root["camera"]["speed"].AsDouble(railCameraSpeed_));
	{
		// レールカメラ向きキーの復元
		const JsonValue& keys = root["camera"]["rotKeys"];
		if (keys.IsArray()) {
			cameraRotKeys_.clear();
			for (size_t i = 0; i < keys.Size(); ++i) {
				const JsonValue& ko = keys[i];
				auto key = std::make_unique<CameraRotKey>();
				key->t = static_cast<float>(ko["t"].AsDouble(0.0));

				const JsonValue& rot = ko["rotate"];
				if (rot.IsArray() && rot.Size() >= 3) {
					key->rotate = {
						static_cast<float>(rot[0].AsDouble(0.0)),
						static_cast<float>(rot[1].AsDouble(0.0)),
						static_cast<float>(rot[2].AsDouble(0.0)),
					};
				}

				const JsonValue& ease = ko["ease"];
				if (ease.IsObject()) {
					key->easeToNext.enabled = ease["enabled"].AsBool(false);
					const JsonValue& pts = ease["points"];
					if (pts.IsArray() && pts.Size() >= 2) {
						key->easeToNext.points.clear();
						for (size_t j = 0; j < pts.Size(); ++j) {
							const JsonValue& pr = pts[j];
							if (pr.IsArray() && pr.Size() >= 2) {
								key->easeToNext.points.push_back({
									static_cast<float>(pr[0].AsDouble(0.0)),
									static_cast<float>(pr[1].AsDouble(0.0)) });
							}
						}
					}
				}
				cameraRotKeys_.push_back(std::move(key));
			}
		}
	}
}

void RailStagePart::SaveToJson(JsonValue& root) const {
	JsonValue camObj = JsonValue::MakeObject();
	camObj["speed"] = static_cast<double>(railCameraSpeed_);
	{
		// レールカメラ向きキー（t / オイラー角 / 緩急カーブ）
		JsonValue keysArr = JsonValue::MakeArray();
		for (const auto& k : cameraRotKeys_) {
			if (!k) continue;
			JsonValue keyObj = JsonValue::MakeObject();
			keyObj["t"] = static_cast<double>(k->t);

			JsonValue rot = JsonValue::MakeArray();
			rot.Push(JsonValue(static_cast<double>(k->rotate.x)));
			rot.Push(JsonValue(static_cast<double>(k->rotate.y)));
			rot.Push(JsonValue(static_cast<double>(k->rotate.z)));
			keyObj["rotate"] = std::move(rot);

			JsonValue ease = JsonValue::MakeObject();
			ease["enabled"] = k->easeToNext.enabled;
			JsonValue pts = JsonValue::MakeArray();
			for (const auto& p : k->easeToNext.points) {
				JsonValue pair = JsonValue::MakeArray();
				pair.Push(JsonValue(static_cast<double>(p.x)));
				pair.Push(JsonValue(static_cast<double>(p.y)));
				pts.Push(std::move(pair));
			}
			ease["points"] = std::move(pts);
			keyObj["ease"] = std::move(ease);

			keysArr.Push(std::move(keyObj));
		}
		camObj["rotKeys"] = std::move(keysArr);
	}
	root["camera"] = std::move(camObj);
}

bool RailStagePart::OnViewportPrefabDrop(const std::string& prefabName, float relX, float relY) {
#ifdef _DEBUG
	if (!waveEditMode_ || !camera_) return false;

	// 画面相対座標→カメラを通るレイを作り、カメラ前方 waveDropDepth_ の点を採用
	const float ndcX = relX * 2.0f - 1.0f;
	const float ndcY = 1.0f - relY * 2.0f;
	const Matrix4x4 invVP = Inverse(camera_->GetViewProjectionMatrix());
	const Vector3 nearP = TransformCoordinate({ ndcX, ndcY, 0.0f }, invVP);
	const Vector3 farP  = TransformCoordinate({ ndcX, ndcY, 1.0f }, invVP);
	Vector3 dir{ farP.x - nearP.x, farP.y - nearP.y, farP.z - nearP.z };
	const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (len < 1e-5f) return false;
	dir = { dir.x / len, dir.y / len, dir.z / len };
	const Vector3 camPos = camera_->GetTranslate();
	const Vector3 worldPos{
		camPos.x + dir.x * waveDropDepth_,
		camPos.y + dir.y * waveDropDepth_,
		camPos.z + dir.z * waveDropDepth_,
	};

	// 新規ウェーブエントリを現在の経過秒で作成（カメラ相対配置）
	const float nowSec = (railCameraSpeed_ > 1e-8f && railCamera_)
		? railCamera_->GetProgress() / railCameraSpeed_ : 0.0f;
	WaveEntry e{};
	e.prefab          = prefabName;
	e.enemyType       = "Drone"; // 攻撃ロール既定（射撃）。一覧で変更可
	e.triggerSec      = nowSec;
	e.retreatSec      = -1.0f;
	e.useCameraOffset = true;
	e.cameraOffset    = WorldToCameraOffset(worldPos);
	currentWave_.entries.push_back(e);
	waveSelectedEntry_ = static_cast<int>(currentWave_.entries.size()) - 1;
	RebuildWaveRuntimeState();
	LogBuffer::Instance().Add(
		"Wave: added entry '" + prefabName + "' at " + std::to_string(e.triggerSec) + "s",
		LogBuffer::Level::Info);
	return true;
#else
	(void)prefabName; (void)relX; (void)relY;
	return false;
#endif
}

Vector3 RailStagePart::CameraOffsetToWorld(const Vector3& off) const {
	if (!camera_) return off;
	const Matrix4x4& w = camera_->GetWorldMatrix();
	const Vector3 c = camera_->GetTranslate();
	return {
		c.x + w.m[0][0] * off.x + w.m[1][0] * off.y + w.m[2][0] * off.z,
		c.y + w.m[0][1] * off.x + w.m[1][1] * off.y + w.m[2][1] * off.z,
		c.z + w.m[0][2] * off.x + w.m[1][2] * off.y + w.m[2][2] * off.z,
	};
}

Vector3 RailStagePart::WorldToCameraOffset(const Vector3& world) const {
	if (!camera_) return world;
	const Matrix4x4& w = camera_->GetWorldMatrix();
	const Vector3 c = camera_->GetTranslate();
	const Vector3 d{ world.x - c.x, world.y - c.y, world.z - c.z };
	// カメラのワールド行列は正規直交（無スケール）→ 転置が逆回転。各基底との内積でローカル化。
	return {
		d.x * w.m[0][0] + d.y * w.m[0][1] + d.z * w.m[0][2],
		d.x * w.m[1][0] + d.y * w.m[1][1] + d.z * w.m[1][2],
		d.x * w.m[2][0] + d.y * w.m[2][1] + d.z * w.m[2][2],
	};
}

void RailStagePart::OnImGuiTuning(bool& changed) {
#ifdef _DEBUG
	if (ImGui::CollapsingHeader("Rail Camera")) {
		ImGui::DragFloat("Speed (t/sec)", &railCameraSpeed_, 0.005f, 0.0f, 5.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			changed = true;
			if (railCamera_) railCamera_->SetSpeed(railCameraSpeed_);
		}

		ImGui::SeparatorText("Authoring（向きキー作成）");
		ImGui::Checkbox("Aim from rail（編集モード）", &aimAuthoring_);
		ImGui::TextDisabled("ON中: ゲーム完全フリーズ。3Dビュー上で 左ドラッグ=見回し / Alt+左ドラッグ=roll");

		if (railCamera_) {
			float p = railCamera_->GetProgress();
			if (ImGui::SliderFloat("Progress", &p, 0.0f, 1.0f, "%.3f")) {
				railCamera_->SetProgress(p);
			}

			char recLabel[64];
			std::snprintf(recLabel, sizeof(recLabel), "現在のカメラ向きを記録 (t=%.2f)", p);
			if (ImGui::Button(recLabel) && camera_) {
				auto key = std::make_unique<CameraRotKey>();
				key->t = p;
				key->rotate = camera_->GetRotate();
				CameraRotKey* raw = key.get();
				cameraRotKeys_.push_back(std::move(key));
				std::sort(cameraRotKeys_.begin(), cameraRotKeys_.end(),
					[](const std::unique_ptr<CameraRotKey>& a, const std::unique_ptr<CameraRotKey>& b) { return a->t < b->t; });
				ImGuiManager::Instance().SetSelected(raw);
				changed = true;
			}

			ImGui::SeparatorText("Keyframes");
			int deleteIdx = -1;
			for (int i = 0; i < static_cast<int>(cameraRotKeys_.size()); ++i) {
				CameraRotKey* k = cameraRotKeys_[i].get();
				ImGui::PushID(i);
				Vector3 d = RadToDeg(k->rotate);
				char label[96];
				std::snprintf(label, sizeof(label), "t=%.2f  (p%.0f y%.0f r%.0f)", k->t, d.x, d.y, d.z);
				const bool isSel = (ImGuiManager::Instance().GetSelected() == k);
				if (ImGui::Selectable(label, isSel)) {
					ImGuiManager::Instance().SetSelected(k);
					railCamera_->SetProgress(k->t);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("x")) deleteIdx = i;
				ImGui::PopID();
			}
			if (deleteIdx >= 0) {
				CameraRotKey* del = cameraRotKeys_[deleteIdx].get();
				if (ImGuiManager::Instance().GetSelected() == del) {
					ImGuiManager::Instance().SetSelected(nullptr);
				}
				cameraRotKeys_.erase(cameraRotKeys_.begin() + deleteIdx);
				changed = true;
			}
		}
	}

	DrawWaveEditorUI(changed);
#else
	(void)changed;
#endif
}

#ifdef _DEBUG
void RailStagePart::SaveWaveToDisk() {
	if (WaveDefIO::SaveToFile(wavePath_, currentWave_)) {
		MarkWaveFileSynced();  // 自分の保存で再読込を誘発しない（反響防止）
		LogBuffer::Instance().Add("Wave saved: " + wavePath_, LogBuffer::Level::Info);
	} else {
		LogBuffer::Instance().Add("Wave save FAILED: " + wavePath_, LogBuffer::Level::Warning);
	}
}

void RailStagePart::RebuildWaveRuntimeState() {
	// entries サイズに追従させ、現在の rail t でスポーン状態を作り直す。
	// Seek が敵/弾/コントローラを全クリアして seekT 基準で再構築してくれる。
	spawnFired_.assign(currentWave_.entries.size(), false);
	retreatFired_.assign(currentWave_.entries.size(), false);
	killAtT_.assign(currentWave_.entries.size(), -1.0f);
	const float seconds = (railCameraSpeed_ > 1e-8f && railCamera_)
		? railCamera_->GetProgress() / railCameraSpeed_ : 0.0f;
	Seek(seconds);
}

void RailStagePart::DrawWaveEditorUI(bool& changed) {
	if (!ImGui::CollapsingHeader("Wave Editor")) return;

	ImGui::Checkbox("Wave Edit Mode", &waveEditMode_);
	ImGui::TextDisabled("ON中: ビューポートへプレハブをドロップ＝現在tでエントリ追加");

	ImGui::Checkbox("Auto Reload Wave", &autoReloadWave_);
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Wave JSON が書き換わったら読み直して現在時刻で組み直す\n"
		                  "（Blender から敵配置を Export したら再起動なしで反映される）\n"
		                  "※読み直すと撃破済みの敵も出し直される");
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload Wave")) {
		ReloadWaveNow();  // 監視OFFでも手動で読み直せる
	}
	ImGui::DragFloat("Drop Depth (前方)", &waveDropDepth_, 0.5f, 1.0f, 300.0f, "%.1f");

	// エントリの時間系は全て秒(s)。レールカメラは進行度 t なので、UI 上のレール時刻だけ
	// 進行度⇔秒を換算する（t = 秒 × railCameraSpeed_、全体尺 = 1/railCameraSpeed_ 秒）。
	const float tps = (railCameraSpeed_ > 1e-8f) ? railCameraSpeed_ : (1.0f / 120.0f);
	const float totalSec = 1.0f / tps;
	const float nowSec = railCamera_ ? railCamera_->GetProgress() / tps : 0.0f;

	// rail 時刻スクラブ（配置タイミング合わせ・秒指定）
	if (railCamera_) {
		float curSec = nowSec;
		if (ImGui::SliderFloat("Rail 時刻 (s)", &curSec, 0.0f, totalSec, "%.2f s")) {
			// Seek は秒引数。スクラブで敵配置も追従させる
			Seek(curSec);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("/ %.1f s", totalSec);
		if (ImGui::Button("現在時刻で中央にエントリ追加")) {
			WaveEntry e{};
			e.prefab          = "dummy_enemy";
			e.enemyType       = "Drone";
			e.triggerSec      = nowSec;
			e.retreatSec      = -1.0f;
			e.useCameraOffset = true;
			e.cameraOffset    = { 0.0f, 0.0f, waveDropDepth_ }; // 正面 waveDropDepth_
			currentWave_.entries.push_back(e);
			waveSelectedEntry_ = static_cast<int>(currentWave_.entries.size()) - 1;
			RebuildWaveRuntimeState();
		}
	}

	ImGui::Separator();
	if (ImGui::Button("Save Wave")) { SaveWaveToDisk(); }
	ImGui::SameLine();
	ImGui::Text("(%s)", wavePath_.c_str());

	// ----- エントリ一覧 -----
	ImGui::SeparatorText("Entries");
	int deleteIdx = -1, moveUpIdx = -1, moveDownIdx = -1;
	for (int i = 0; i < static_cast<int>(currentWave_.entries.size()); ++i) {
		const WaveEntry& we = currentWave_.entries[i];
		ImGui::PushID(i);
		char label[160];
		std::snprintf(label, sizeof(label), "[%d] %s  %.2fs  %s",
			i, we.prefab.c_str(), we.triggerSec,
			we.useCameraOffset ? "camera" : (we.splineId.empty() ? "-" : we.splineId.c_str()));
		if (ImGui::Selectable(label, waveSelectedEntry_ == i)) {
			waveSelectedEntry_ = i;
			Seek(we.triggerSec); // Seek は秒引数
		}
		ImGui::SameLine(); if (ImGui::SmallButton("^")) moveUpIdx = i;
		ImGui::SameLine(); if (ImGui::SmallButton("v")) moveDownIdx = i;
		ImGui::SameLine(); if (ImGui::SmallButton("x")) deleteIdx = i;
		ImGui::PopID();
	}

	// ----- 選択エントリの編集 -----
	if (waveSelectedEntry_ >= 0 && waveSelectedEntry_ < static_cast<int>(currentWave_.entries.size())) {
		WaveEntry& we = currentWave_.entries[waveSelectedEntry_];
		ImGui::SeparatorText("Selected Entry");

		char prefabBuf[128];
		std::snprintf(prefabBuf, sizeof(prefabBuf), "%s", we.prefab.c_str());
		if (ImGui::InputText("Prefab", prefabBuf, sizeof(prefabBuf))) we.prefab = prefabBuf;

		const char* kEnemyTypes[] = { "Drone", "Carrier", "Rusher" };
		int etIdx = (we.enemyType == "Carrier") ? 1 : (we.enemyType == "Rusher") ? 2 : 0;
		if (ImGui::Combo("Enemy Type (攻撃ロール)", &etIdx, kEnemyTypes, IM_ARRAYSIZE(kEnemyTypes))) {
			we.enemyType = kEnemyTypes[etIdx];
		}

		// 出現タイミング（秒）
		if (ImGui::DragFloat("出現 (s)", &we.triggerSec, 0.05f, 0.0f, totalSec, "%.2f s")) changed = true;
		ImGui::SameLine();
		if (ImGui::SmallButton("now##trig")) we.triggerSec = nowSec;

		// 退避（秒）。チェックOFFで「退避なし」(-1)
		bool hasRetreat = (we.retreatSec >= 0.0f);
		if (ImGui::Checkbox("退避する", &hasRetreat)) {
			we.retreatSec = hasRetreat ? (we.triggerSec + 3.0f) : -1.0f;
			changed = true;
		}
		if (hasRetreat) {
			if (ImGui::DragFloat("退避 (s)", &we.retreatSec, 0.05f, 0.0f, totalSec, "%.2f s")) changed = true;
			ImGui::SameLine();
			if (ImGui::SmallButton("now##ret")) we.retreatSec = nowSec;
		}

		ImGui::DragInt("count", &we.count, 0.1f, 1, 20);

		// 射撃間隔（秒）。0 で射撃なし
		ImGui::DragFloat("射撃間隔 (s)", &we.shootIntervalSec, 0.02f, 0.0f, 30.0f, "%.2f s");

		// 配置方式：カメラ相対 / スプライン
		ImGui::Checkbox("Use Camera Offset (画面相対停止)", &we.useCameraOffset);
		if (we.useCameraOffset) {
			ImGui::DragFloat3("camera_offset (右/上/前)", &we.cameraOffset.x, 0.25f, -500.0f, 500.0f, "%.2f");
		} else {
			// スプライン選択コンボ（EnemyPathSpline 等の動的スプライン名から）
			const char* curr = we.splineId.empty() ? "(none)" : we.splineId.c_str();
			if (ImGui::BeginCombo("spline_id", curr)) {
				for (const auto& sp : host_->GetDynamicSplines()) {
					if (!sp) continue;
					const std::string nm = sp->GetName();
					if (ImGui::Selectable(nm.c_str(), nm == we.splineId)) we.splineId = nm;
				}
				ImGui::EndCombo();
			}
			// スプライン踏破にかける時間（秒）
			if (ImGui::DragFloat("踏破時間 (s)", &we.traverseSec, 0.1f, 0.1f, totalSec, "%.2f s")) {
				if (we.traverseSec < 0.1f) we.traverseSec = 0.1f;
			}
		}

		if (ImGui::Button("この変更を反映 (再構築)")) {
			RebuildWaveRuntimeState();
			changed = true;
		}
	}

	// 構造変更の後処理（リスト走査後にまとめて行う）
	bool structureChanged = false;
	if (deleteIdx >= 0) {
		currentWave_.entries.erase(currentWave_.entries.begin() + deleteIdx);
		if (waveSelectedEntry_ == deleteIdx) waveSelectedEntry_ = -1;
		else if (waveSelectedEntry_ > deleteIdx) --waveSelectedEntry_;
		structureChanged = true;
	}
	if (moveUpIdx > 0) {
		std::swap(currentWave_.entries[moveUpIdx], currentWave_.entries[moveUpIdx - 1]);
		if (waveSelectedEntry_ == moveUpIdx) --waveSelectedEntry_;
		structureChanged = true;
	}
	if (moveDownIdx >= 0 && moveDownIdx + 1 < static_cast<int>(currentWave_.entries.size())) {
		std::swap(currentWave_.entries[moveDownIdx], currentWave_.entries[moveDownIdx + 1]);
		if (waveSelectedEntry_ == moveDownIdx) ++waveSelectedEntry_;
		structureChanged = true;
	}
	if (structureChanged) {
		RebuildWaveRuntimeState();
		changed = true;
	}
}
#endif // _DEBUG
