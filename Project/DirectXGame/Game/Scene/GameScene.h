#pragma once
#include "Scene.h"
#include "SceneSerializer.h"   // SceneData / SceneEntityDesc（保存・読込のフックで使う）
#include "Beam/IPlayerBeamHost.h" // PlayerBeamHit（ビームの判定の結果）

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>

// 前方宣言
class IImGuiEditable;
class SplineCurveActor;
class EnemyController;
class GameSession;
class BossGravityOrb;

/// <summary>
/// ゲーム用シーン基底。エンジン足場 Scene に、本作のゲームロジック
/// （プレイヤー弾・近接、敵・敵弾・敵コントローラ、プレハブ生成、スプライン）を加える。
/// 具体シーン（Title / StagePlay / Hub / Result / Demo）はこれを継承する。
/// </summary>
class GameScene : public Scene {
public:
	GameScene();
	~GameScene() override;

	/// <summary>ボスの必殺技 GravityOrb の段取り（岩・演出）を返す。ボス戦のあるシーンだけが持つ</summary>
	/// <returns>段取り。このシーンに無ければ nullptr</returns>
	virtual BossGravityOrb* GetBossGravityOrb() { return nullptr; }

	//====================
	// 1 回のプレイの状態（SceneManager がシーンを作るたびに渡す）
	//====================
	/// <summary>シーンをまたいで残るプレイの状態を設定する（Initialize より前に SceneManager が呼ぶ）</summary>
	/// <param name="gameSession">Game が持つプレイの状態（参照のみ）</param>
	void SetGameSession(GameSession* gameSession) { gameSession_ = gameSession; }
	/// <summary>シーンをまたいで残るプレイの状態を取得する</summary>
	/// <returns>プレイの状態</returns>
	GameSession* GetGameSession() const { return gameSession_; }

	//====================
	// プレハブ生成（Scene の no-op を override）
	//====================
	/// <summary>
	/// プレハブ名でシーンに動的配置（PrefabManager から PrefabDef を引いて生成）。
	/// コライダー・HP・攻撃・弾・近接・移動・エフェクトなどプレハブの属性もコピーする。
	/// </summary>
	/// <param name="prefabName">プレハブ名</param>
	/// <param name="worldPos">置く位置（ワールド座標）</param>
	/// <returns>作ったエンティティ。プレハブが無い・作れなければ nullptr</returns>
	IImGuiEditable* InstantiatePrefab(const std::string& prefabName, const Vector3& worldPos = {}) override;

	//====================
	// スプライン動的管理（Scene の no-op を override）
	//====================
	/// <summary>既定の形のスプラインを 1 本足し、制御点を worldPos だけずらす（名前は "Spline (n)" で重ならないように付ける）</summary>
	/// <param name="tagInt">スプラインの役割のタグ（EntityTag を int にした値）</param>
	/// <param name="worldPos">制御点に足すずらし（ワールド座標）</param>
	void AddDynamicSpline(int tagInt, const Vector3& worldPos = {}) override;
	/// <summary>名前の一致するスプラインを遅延破棄して一覧から外す（無ければ何もしない）</summary>
	/// <param name="name">スプラインの名前</param>
	void RemoveDynamicSpline(const std::string& name) override;
	/// <summary>シーン内の SplineCurveActor を名前で取得</summary>
	/// <param name="name">スプラインの名前</param>
	/// <returns>見つかったスプライン。無ければ nullptr</returns>
	SplineCurveActor* FindDynamicSplineByName(const std::string& name);

	//====================
	// プレイヤー弾 / 近接
	//====================
	/// <summary>
	/// プレイヤー弾を1発スポーン。Primitive プレハブから生成し、velocity と寿命を関連付ける。
	/// ダメージは発射時に攻撃力 × プレハブの倍率で確定し、1 発が同じ持ち主に当たるのは 1 回（貫通弾はクールタイムごと）。
	/// </summary>
	/// <param name="pos">発射位置</param>
	/// <param name="direction">進む向き（正規化済み）</param>
	/// <param name="speed">弾速 [m/s]</param>
	/// <param name="lifetime">寿命 [秒]</param>
	/// <param name="colliderGrowthPerMeter">進んだ距離 1m あたりに判定の半径を太らせる量 [m]</param>
	/// <param name="homingTarget">ホーミングの目標（nullptr ならまっすぐ）</param>
	/// <param name="homingStrength">ホーミングの強さ（向きを寄せる指数減衰の速さ [1/秒]）</param>
	/// <param name="maxTravelDistance">これだけ進んだら消す距離 [m]（0 なら距離で消さない）</param>
	/// <param name="prefabName">弾のプレハブ名（Primitive 種別）</param>
	/// <param name="attackPower">撃った側の攻撃力</param>
	/// <param name="sizeScale">判定＋見た目のサイズ倍率（メイン武器の弾の大きさ）</param>
	virtual void SpawnPlayerBullet(const Vector3& pos, const Vector3& direction,
		float speed = 80.0f, float lifetime = 2.0f,
		float colliderGrowthPerMeter = 0.0f,
		IImGuiEditable* homingTarget = nullptr,
		float homingStrength = 0.0f,
		float maxTravelDistance = 0.0f,
		const std::string& prefabName = "TemporaryPlayerBullet",
		int attackPower = 0,
		float sizeScale = 1.0f);

	/// <summary>
	/// プレイヤー近接攻撃の判定を1つスポーン。owner に追従させて持続させる。
	/// 同じ持ち主には 1 回だけ当たり、出てから cleanWindow 以内なら本あて（強いダメージ）になる。
	/// </summary>
	/// <param name="owner">判定を追従させる持ち主（自機）</param>
	/// <param name="right">判定のオフセットを展開する右向き</param>
	/// <param name="up">判定のオフセットを展開する上向き</param>
	/// <param name="forward">判定のオフセットを展開する前向き（判定もこの向きへ回す）</param>
	/// <param name="prefabName">近接のプレハブ名（Primitive 種別）</param>
	/// <param name="attackPower">攻撃力</param>
	/// <param name="extraWorldOffset">生成位置へ加算するワールドオフセット（体の中心へ持ち上げ等）</param>
	/// <param name="sizeScale">判定＋見た目のサイズ倍率（STG=1.0 / ボス戦=小さめ 等）</param>
	/// <param name="rehitInterval">同じ相手にもう一度当たれるまでの時間 [秒]（0 以下なら 1 回だけ。UpdateMelees の rehitDeltaTime で数える）</param>
	virtual void SpawnPlayerMelee(IImGuiEditable* owner,
		const Vector3& right, const Vector3& up, const Vector3& forward,
		const std::string& prefabName,
		int attackPower,
		const Vector3& extraWorldOffset = { 0.0f, 0.0f, 0.0f },
		float sizeScale = 1.0f,
		float rehitInterval = 0.0f);

	//====================
	// 敵 / 敵弾 / 敵コントローラ
	//====================
	/// <summary>
	/// 動的エンティティを破棄キューへ移送する。ゲーム状態（弾・敵）の dangling 参照を掃除した上で
	/// 基底 Scene::DestroyDynamicEntity に実体除去を委譲する。
	/// </summary>
	/// <param name="e">破棄するエンティティ（nullptr なら何もしない）</param>
	void DestroyDynamicEntity(IImGuiEditable* e) override;

	/// <summary>
	/// 指定プレハブから敵を1体スポーンし、指定スプラインに沿って進ませる（体力に純度の倍率を掛ける）。
	/// </summary>
	/// <param name="prefabName">敵のプレハブ名</param>
	/// <param name="spline">沿わせるスプライン（制御点 2 つ以上）</param>
	/// <param name="speed">スプラインの進み [t/秒]</param>
	/// <param name="removeAtEnd">終端に着いたら破棄するなら true</param>
	/// <param name="initialT">スプラインの始めの位置（0〜1 に収める）</param>
	/// <param name="waveEntryIndex">Wave のエントリ番号（Wave 由来でなければ -1）</param>
	/// <returns>出した敵。スプラインが不正・プレハブを作れなければ nullptr</returns>
	virtual IImGuiEditable* SpawnEnemyOnSpline(const std::string& prefabName,
		SplineCurveActor* spline, float speed = 0.1f, bool removeAtEnd = true,
		float initialT = 0.0f,
		int   waveEntryIndex = -1);

	/// <summary>敵弾をスポーンする（EnemyAttack タグ付き。自機に当たると消える）。負の引数はプレハブの "bullet" の値、無ければ既定値を使う</summary>
	/// <param name="pos">発射位置</param>
	/// <param name="direction">進む向き（正規化済み）</param>
	/// <param name="speed">弾速 [m/s]（負ならプレハブの値）</param>
	/// <param name="lifetime">寿命 [秒]（負ならプレハブの値）</param>
	/// <param name="prefabName">弾のプレハブ名（Primitive 種別）</param>
	/// <param name="homingTarget">ホーミングの目標（nullptr ならまっすぐ）</param>
	/// <param name="homingStrength">ホーミングの強さ [1/秒]（負ならプレハブの値）</param>
	void SpawnEnemyBullet(const Vector3& pos, const Vector3& direction,
		float speed = -1.0f, float lifetime = -1.0f,
		const std::string& prefabName = "EnemyBullet",
		IImGuiEditable* homingTarget = nullptr,
		float homingStrength = -1.0f);

	/// <summary>
	/// 敵弾が狙う点。自機は原点が足元なので、当たり判定のオフセットぶん上げた体の中心を返す（それ以外は位置そのまま）。
	/// 偏差射撃の狙いとホーミングの目標の両方で使う。
	/// </summary>
	/// <param name="target">狙う相手</param>
	/// <returns>狙う点（ワールド座標）。target が nullptr・位置を持たなければ原点</returns>
	static Vector3 GetShotAimPoint(IImGuiEditable* target);

	/// <summary>指定プレハブをスプラインなしで指定座標に直接スポーンする（体力に純度の倍率を掛ける）</summary>
	/// <param name="prefabName">敵のプレハブ名</param>
	/// <param name="pos">置く位置</param>
	/// <returns>出した敵。作れなければ nullptr</returns>
	IImGuiEditable* SpawnEnemyAt(const std::string& prefabName, const Vector3& pos);

	/// <summary>指定エンティティと他 Enemy との衝突半径オーバーラップを解消する（相互排斥。自分だけを重なりの半分押す）</summary>
	/// <param name="self">押す側のエンティティ</param>
	void ApplyEnemyRepulsion(IImGuiEditable* self);

	/// <summary>EnemyController を登録する（コマンドからのランタイム追加用。更新ループの後にまとめて加わる）</summary>
	/// <param name="ctrl">登録するコントローラ（所有権を移す）</param>
	void RegisterEnemyController(std::unique_ptr<EnemyController> ctrl);

#ifdef USE_IMGUI
	/// <summary>動的オブジェクト判定。エンジン管理分に加えてスプラインも確認する。</summary>
	/// <param name="editable">調べるエンティティ</param>
	/// <returns>シーンの動的オブジェクトなら true</returns>
	bool IsDynamicObject(IImGuiEditable* editable) const override;
#endif

	//====================
	// シーンの保存 / 読込
	// 実処理はここに一本化してある（JSON の形は SceneSerializer が持つ）。
	// 派生シーンは下の protected フックだけを override して固有の差分を差し込む。
	//====================
	/// <summary>今の配置を集めて JSON ファイルへ書き出す（ShouldSkipOnSave で外した物は書かない）</summary>
	/// <param name="filePath">書き出す JSON ファイルのパス</param>
	/// <returns>書けたら true</returns>
	bool SaveSceneToJson(const std::string& filePath) override;
	/// <summary>JSON を読み切ってから今の動的エンティティを消して作り直す（読めなければ今のシーンを壊さない）</summary>
	/// <param name="filePath">読む JSON ファイルのパス</param>
	/// <returns>読めたら true</returns>
	bool LoadSceneFromJson(const std::string& filePath) override;

	/// <summary>
	/// 現在のシーン内容を JSON 文字列にする（ファイルには書かない）。
	/// エディタ側が「前フレームから変わったか」を検出する差分ハッシュ源に使う。
	/// ShouldSkipOnSave で除外される一時オブジェクト（弾・敵・UI）は含まれないので、
	/// プレイ中の動きでは変化せず、ユーザーが保存対象を編集したときだけ変わる。
	/// </summary>
	/// <returns>SaveSceneToJson と同じ形の JSON 文字列</returns>
	std::string SerializeSceneToString() const override;

protected:
	//====================
	// 保存 / 読込のシーン固有フック（既定は何もしない）
	//====================

	/// <summary>JSON の "scene" に書く名前</summary>
	/// <returns>シーン名（既定は "Scene"）</returns>
	virtual std::string GetSceneJsonName() const { return "Scene"; }

	/// <summary>保存から外すエンティティか（一時オブジェクト等）。既定は何も外さない</summary>
	/// <param name="entity">調べるエンティティ</param>
	/// <param name="tag">そのエンティティのタグ</param>
	/// <returns>true なら保存しない</returns>
	virtual bool ShouldSkipOnSave(const IImGuiEditable* entity, EntityTag tag) const;

	/// <summary>復元しないエントリか（古い save の混入除け等）。既定は何も外さない</summary>
	/// <param name="desc">読んだエントリ</param>
	/// <returns>true なら復元しない</returns>
	virtual bool ShouldSkipOnLoad(const SceneEntityDesc& desc) const;

	/// <summary>
	/// 読み込みでは作らず、LoadStreamGroup まで取っておくエントリか（11 番 L9）。
	/// 既定は false（streamGroup が付いていてもすぐ作る）。一度 LoadStreamGroup したグループはすぐ作る
	/// </summary>
	/// <param name="desc">読んだエントリ（streamGroup 付き）</param>
	/// <returns>true なら取っておく</returns>
	virtual bool ShouldStreamOnLoad(const SceneEntityDesc& desc) const { (void)desc; return false; }

	//====================
	// 区間ごとのストリーミング（11 番 L9）
	//====================
	/// <summary>取っておいた group のエントリを今すぐ全部作る（同期）。以後この group はすぐ作る</summary>
	/// <param name="group">streamGroup 名</param>
	/// <returns>作ったエントリの数</returns>
	size_t LoadStreamGroup(const std::string& group);
	/// <summary>LoadStreamGroup 済みか</summary>
	/// <param name="group">streamGroup 名</param>
	/// <returns>読み込み済みなら true</returns>
	bool IsStreamGroupLoaded(const std::string& group) const { return loadedStreamGroups_.contains(group); }
	/// <summary>まだ作っていない group のエントリ数</summary>
	/// <param name="group">streamGroup 名</param>
	/// <returns>取っておいたエントリの数</returns>
	size_t GetPendingStreamCount(const std::string& group) const;
	/// <summary>
	/// 作った group の配置を消し、作った時のエントリを「まだ作っていない」へ戻す（以後 LoadStreamGroup で作り直せる）。
	/// モデルは ModelManager のキャッシュに残る（解放はしない）
	/// </summary>
	/// <param name="group">streamGroup 名</param>
	/// <returns>消した配置の数（エディタで先に消された物は数えない）</returns>
	size_t UnloadStreamGroup(const std::string& group);
	/// <summary>プレハブの静的モデルを ModelManager に読んでおく（撃つ弾のプレハブも 1 段たどる。後から InstantiatePrefab した時に読み込みで止まらないように）</summary>
	/// <param name="prefabs">プレハブ名の一覧</param>
	void PreloadPrefabModels(const std::vector<std::string>& prefabs);

	/// <summary>動的エンティティを消した直後・復元の前。参照メンバのリセット用。</summary>
	virtual void OnBeforeSceneLoad() {}

	/// <summary>復元が終わった後。player_ の貼り直しや UI 再構築用。</summary>
	virtual void OnAfterSceneLoad() {}

	/// <summary>全コンテナ → SceneData（まだ作っていないストリーミングのエントリも含める）。ShouldSkipOnSave で除外できる。</summary>
	/// <param name="out">書き込み先</param>
	void CollectSceneData(SceneData& out) const;

	/// <summary>SceneData → 全コンテナ。ShouldSkipOnLoad で除外できる。</summary>
	/// <param name="data">作るシーンの記述</param>
	void ApplySceneData(const SceneData& data);

	//====================
	// 配置を何フレームかに分けて読む（StagePlay の③IN の降下。16 番 F7）。
	// LoadSceneFromJson と同じ順（読む → 消す → OnBeforeSceneLoad → 作る）で、OnAfterSceneLoad は呼び出し側が呼ぶ
	//====================
	/// <summary>読んで、今の動的エンティティを消すところまで（OnBeforeSceneLoad も呼ぶ）</summary>
	/// <param name="filePath">読む JSON ファイルのパス</param>
	/// <returns>読めたら true。読めなければ false（何も消さない）</returns>
	bool BeginSceneLoadFromJson(const std::string& filePath);
	/// <summary>エンティティを budgetMs まで作る（最低 1 個）</summary>
	/// <param name="budgetMs">このフレームに使ってよい時間 [ms]</param>
	/// <returns>全部作り終えたら（または読み込み中でなければ）true</returns>
	bool ApplySceneLoadStep(double budgetMs);
	/// <summary>分けて読む途中か</summary>
	/// <returns>BeginSceneLoadFromJson の後、作り終えていなければ true</returns>
	bool IsSceneLoadPending() const { return sceneLoadPending_; }

	/// <summary>動的エンティティとスプラインを全て遅延破棄してコンテナを空にする（取っておいたストリーミングのエントリも捨てる）。</summary>
	void ClearDynamicEntities();

private:
	/// <summary>エンティティ 1 個を作る（ApplySceneData / ApplySceneLoadStep の中身）。種類ごとの数と時間を足す</summary>
	/// <param name="d">作るエントリ（取っておく物ならここで取っておく）</param>
	void ApplySceneEntity(const SceneEntityDesc& d);
	/// <summary>種類ごとに作った数と時間をセッションログへ書く</summary>
	void LogSceneEntityStats();
	static constexpr int kSceneKindCount = 6;
	int    sceneKindCount_[kSceneKindCount] = {};
	double sceneKindMs_[kSceneKindCount] = {};
	SceneData pendingSceneData_;
	size_t pendingSceneIndex_ = 0;
	bool sceneLoadPending_ = false;
	// ShouldStreamOnLoad で取っておいたエントリ（保存では作った物と同じく書き出す）と、読み込み済みのグループ
	std::vector<SceneEntityDesc> pendingStreamEntities_;
	std::unordered_set<std::string> loadedStreamGroups_;
	// streamGroup 付きで作った配置と、作った時のエントリ（UnloadStreamGroup で消して取っておきへ戻すため）
	struct StreamCreated {
		IImGuiEditable* entity = nullptr;
		SceneEntityDesc desc;
	};
	std::vector<StreamCreated> streamCreated_;
protected:

	//====================
	// 弾 / 近接 / 敵の更新（派生シーンの Update から呼ぶ）
	//====================
	/// <summary>弾をホーミング・移動（自機の弾は敵の手前で止めてすり抜けを防ぐ）させ、寿命切れ・到達距離超えを消す</summary>
	/// <param name="deltaTime">世界の経過時間 [秒]</param>
	/// <param name="playerBulletDeltaTime">0 以上なら、自機の弾（PlayerBullet）だけこの dt で進める（機動系の RB のスロー中も自機の弾は等速）</param>
	void UpdateBullets(float deltaTime, float playerBulletDeltaTime = -1.0f);
	/// <summary>線分 from→to が Enemy/Boss の球・カプセルのコライダー（弾半径ぶん膨らませる）に最初に入る所で弾を止める位置を求める</summary>
	/// <param name="from">線分の始点（今の弾の位置）</param>
	/// <param name="to">線分の終点（このフレームに進む先）</param>
	/// <param name="bulletRadius">弾の判定の半径 [m]</param>
	/// <returns>止める位置の割合 0..1。当たらなければ -1</returns>
	float FindFirstEnemyHitOnSegment(const Vector3& from, const Vector3& to, float bulletRadius) const;
	/// <summary>太さのある線分（カプセル）に重なっている Enemy/Boss の球・カプセルのコライダーをすべて集める（ビームの判定）</summary>
	/// <param name="from">線分の始点</param>
	/// <param name="to">線分の終点</param>
	/// <param name="radius">線分の太さ（半径）[m]</param>
	/// <param name="out">当たった敵を追加する先（持ち主は Gameplay::DamageRootOf）</param>
	void CollectEnemiesOnSegment(const Vector3& from, const Vector3& to, float radius, std::vector<PlayerBeamHit>& out) const;
	/// <summary>自機のビームが敵に当たった：HP を減らし、ヒットエフェクト（と初めて当たった時だけ被弾エフェクト・音）を出す</summary>
	/// <param name="hit">当たった敵</param>
	/// <param name="damage">入れるダメージ</param>
	/// <param name="hitEffect">当たった所に出すエフェクト名（空なら出さない）</param>
	/// <param name="firstHit">このビームがこの敵に初めて当たったか</param>
	void ApplyPlayerBeamHit(const PlayerBeamHit& hit, int damage, const std::string& hitEffect, bool firstHit);
	/// <summary>HP が尽きた動的エンティティ（Player 以外）を死亡エフェクト・爆発音付きで破棄する</summary>
	void SweepDeadEntities();
	/// <summary>近接の判定を持ち主に追従させ、持続切れを消す（振りエフェクトも止める）</summary>
	/// <param name="deltaTime">経過時間 [秒]</param>
	/// <param name="rehitDeltaTime">もう一度当たれるまでの時間を進める経過時間 [秒]（負なら deltaTime。ヒットストップで止めたくない時に実時間を渡す）</param>
	void UpdateMelees(float deltaTime, float rehitDeltaTime = -1.0f);
	/// <summary>スプラインに沿う敵を進め、終端に着いた敵をコントローラへ知らせて外す（removeAtEnd なら破棄）</summary>
	/// <param name="deltaTime">経過時間 [秒]</param>
	void UpdateMovingEnemies(float deltaTime);
	/// <summary>敵のコントローラを更新する（視錐台と偏差射撃用の自機の速度を毎フレーム 1 回求めて渡す。ビルボードの向きもここ）</summary>
	/// <param name="deltaTime">経過時間 [秒]</param>
	/// <param name="player">自機（nullptr 可）</param>
	/// <param name="stageTimeSec">ステージの経過時間 [秒]</param>
	void UpdateEnemyControllers(float deltaTime, IImGuiEditable* player, float stageTimeSec);

	/// <summary>
	/// 攻撃命中時のエフェクト再生。攻撃側プレハブの "hit"（着弾エフェクト）と
	/// 被弾側プレハブの "hurt"（被弾エフェクト）を同じ位置で両方再生する（敵・ボスへの命中は効果音も）。
	/// 未設定のスロットはスキップする。attacker / target が nullptr の側もスキップ。
	/// </summary>
	/// <param name="attacker">攻撃側（弾・近接の判定）</param>
	/// <param name="target">被弾側</param>
	/// <param name="pos">エフェクトを出す位置</param>
	void PlayHitEffects(IImGuiEditable* attacker, IImGuiEditable* target, const Vector3& pos);

	/// <summary>プレイヤー近接が敵/ボスに当たった直後に呼ばれる（必殺技ゲージ加算など派生シーン側の処理用）。</summary>
	/// <param name="target">当たった相手</param>
	/// <param name="prefabName">近接のプレハブ名（弱/強の見分けに使う）</param>
	/// <param name="clean">本あて（出てから cleanWindow 以内）なら true</param>
	virtual void OnPlayerMeleeHit([[maybe_unused]] IImGuiEditable* target, [[maybe_unused]] const std::string& prefabName, [[maybe_unused]] bool clean) {}

	/// <summary>動的スプラインの DebugDraw キュー積み。</summary>
	void DrawDynamicSplinesDebug();

	//====================
	// 派生シーンから戦闘の状態を触るための関数（弾・近接・移動する敵・コントローラの表は private）
	//====================
	/// <summary>生きている敵弾（EnemyAttack・寿命が残っている）を 1 つずつ渡す</summary>
	/// <param name="fn">敵弾を受け取る関数</param>
	void ForEachEnemyBullet(const std::function<void(PrimitiveInstance* bullet)>& fn) const;
	/// <summary>プリミティブが弾（自機の弾・敵弾のどちらか）か</summary>
	/// <param name="primitive">調べるプリミティブ</param>
	/// <returns>弾なら true</returns>
	bool IsBullet(const PrimitiveInstance* primitive) const;
	/// <summary>弾を消す（UpdateBullets で片付く）。ポインタの比較だけで探すので、集めた後に消えた弾でも安全</summary>
	/// <param name="bullet">消す弾（弾でなければ何もしない）</param>
	void KillBullet(const IImGuiEditable* bullet);
	/// <summary>生きている敵弾をすべて消す（ボス撃破の後など）</summary>
	void KillAllEnemyBullets();
	/// <summary>弾・近接・移動する敵・敵のコントローラをすべて捨てる（シーンのロードの前・Wave のやり直し）</summary>
	void ClearCombatRuntime();
	/// <summary>移動する敵（スプライン追従・画面内停止）を 1 体ずつ渡す</summary>
	/// <param name="fn">敵と Wave のエントリ番号を受け取る関数</param>
	void ForEachMovingEnemyEntry(const std::function<void(IImGuiEditable* entity, int waveEntryIndex)>& fn) const;
	/// <summary>カメラ相対で置く敵（画面内停止・固定）を移動する敵の表へ入れる（スプラインなし・速度 0）</summary>
	/// <param name="entity">敵</param>
	/// <param name="waveEntryIndex">Wave のエントリ番号</param>
	void AddStationaryMovingEnemy(IImGuiEditable* entity, int waveEntryIndex);
	/// <summary>移動する敵にコントローラを紐付ける（終端の通知・ビルボードの同期に使う）</summary>
	/// <param name="entity">敵</param>
	/// <param name="ctrl">その敵のコントローラ</param>
	void AttachControllerToMovingEnemy(IImGuiEditable* entity, EnemyController* ctrl);
	/// <summary>Wave のエントリ番号からコントローラを探す</summary>
	/// <param name="waveEntryIndex">Wave のエントリ番号</param>
	/// <returns>コントローラ（無ければ nullptr）</returns>
	EnemyController* FindEnemyControllerByWave(int waveEntryIndex) const;
	/// <summary>敵のコントローラを 1 つずつ渡す</summary>
	/// <param name="fn">コントローラを受け取る関数</param>
	void ForEachEnemyController(const std::function<void(const EnemyController& ctrl)>& fn) const;
	/// <summary>動的スプライン（シーンの持ち物。読むだけ）</summary>
	/// <returns>スプラインの一覧</returns>
	const std::vector<std::unique_ptr<SplineCurveActor>>& GetDynamicSplineList() const { return dynamicSplines_; }
	/// <summary>動的スプラインを 1 本足して返す（AddDynamicSpline と同じ。足した物をすぐ設定したい時）</summary>
	/// <param name="tag">スプラインの役割のタグ</param>
	/// <returns>足したスプライン</returns>
	SplineCurveActor* AddDynamicSplineActor(EntityTag tag);
	/// <summary>敵弾の偏差射撃でプレイヤー自身の移動を先読みする割合（0=今の位置 / 1=完全予測）</summary>
	/// <returns>割合</returns>
	float GetEnemyShotLeadRate() const { return enemyShotLeadRate_; }
	/// <summary>敵弾の偏差射撃でプレイヤー自身の移動を先読みする割合を変える（調整値の読み込み・ImGui）</summary>
	/// <param name="rate">割合（0〜1）</param>
	void SetEnemyShotLeadRate(float rate) { enemyShotLeadRate_ = rate; }

private:
	// シーンをまたいで残るプレイの状態（Game が持つ。参照のみ）
	GameSession* gameSession_ = nullptr;

	struct BulletRuntime {
		PrimitiveInstance* primitive = nullptr; // シーンの一覧（GetDynamicPrimitives）内の生ポインタ
		Vector3 velocity{ 0.0f, 0.0f, 0.0f };
		float speed = 0.0f;
		float remainingLifetime = 0.0f;
		Vector3 originPos{ 0.0f, 0.0f, 0.0f };
		float baseColliderRadius = 0.0f;
		float colliderGrowthPerMeter = 0.0f;
		IImGuiEditable* homingTarget = nullptr;
		float homingStrength = 0.0f;
		float maxTravelDistance = 0.0f;
		bool        penetrate = false;
		float       penetrateDamageRate = 0.2f;
		std::string penetrateEffect;
		int         hitDamage = 0;   // 1 回当たったときのダメージ（攻撃力 × プレハブの倍率。発射時に確定）
		// 貫通弾の多段ヒット用クールタイム。キーはダメージの持ち主（Gameplay::DamageRootOf）なので、
		// ボスの腕→本体と通り抜けても本体に入るのはクールタイムにつき 1 回
		std::unordered_map<IImGuiEditable*, float> hitCooldowns;
		bool        hasHit = false; // 通常弾：最初の 1 体に当たった（同じフレームに重なった別の判定には入れない）
		uint64_t    trailEffectHandle = 0;
	};
	// 弾の一覧。キーは弾の見た目のプリミティブ（弾の実体そのもの）。
	// 当たったときの処理・必殺技・破棄はどれも「このプリミティブの弾」を引くので O(1) で引ける表にする。
	// 並び順はどこも使わないので順序付きの std::map ではなく unordered_map
	std::unordered_map<PrimitiveInstance*, BulletRuntime> bullets_;
	/// <summary>プリミティブから弾を引く（中身を読むので、生きていると分かっている弾にだけ使う）</summary>
	/// <param name="primitive">弾の見た目のプリミティブ</param>
	/// <returns>弾の状態。弾でなければ nullptr</returns>
	BulletRuntime* FindBullet(PrimitiveInstance* primitive);

	struct MeleeRuntime {
		PrimitiveInstance* primitive = nullptr;
		IImGuiEditable* owner = nullptr;
		Vector3 worldOffset{ 0.0f, 0.0f, 0.0f };
		float remainingLifetime = 0.0f;
		float elapsed = 0.0f;
		float cleanWindow = 0.08f;
		int   cleanDamage = 0;
		int   lateDamage = 0;
		uint64_t    swingEffectHandle = 0;
		std::string prefabName;  // どの近接（弱/強）かを派生シーンが見分けるため
		// この判定で既に当てた相手。キーはダメージの持ち主なので、ボスの腕と本体に同時に触れても 1 回
		std::unordered_set<IImGuiEditable*> hitTargets;
		// 同じ相手にもう一度当たれるまでの時間 [秒]（0 以下なら hitTargets で 1 回だけ）と、相手ごとの残り
		float rehitInterval = 0.0f;
		std::unordered_map<IImGuiEditable*, float> rehitTimers;
	};
	std::vector<MeleeRuntime> melees_;

	struct MovingEnemy {
		IImGuiEditable* entity = nullptr;
		SplineCurveActor* spline = nullptr;
		float t = 0.0f;
		float speed = 0.1f;
		bool  removeAtEnd       = true;
		bool  billboardToPlayer = false;
		int   waveEntryIndex    = -1;
		class EnemyController* controller = nullptr;
	};
	std::vector<MovingEnemy> movingEnemies_;

	std::vector<std::unique_ptr<EnemyController>> enemyControllers_;

	// 敵弾の偏差射撃（UpdateEnemyControllers が毎フレームのカメラ/プレイヤー位置差から速度を出す）
	float   enemyShotLeadRate_ = 0.35f;
	Vector3 prevAimPlayerPos_{ 0.0f, 0.0f, 0.0f };
	Vector3 prevAimCameraPos_{ 0.0f, 0.0f, 0.0f };
	bool    hasPrevAimSample_ = false;
	// イテレータ無効化防止：UpdateEnemyControllers 中の RegisterEnemyController はここに溜まり、
	// ループ終了後に enemyControllers_ にマージされる。
	std::vector<std::unique_ptr<EnemyController>> pendingEnemyControllers_;

	// 動的スプライン（プレハブ含む）
	std::vector<std::unique_ptr<SplineCurveActor>> dynamicSplines_;
};
