#pragma once
#include "Vector3.h"

#include <random>
#include <string>
#include <unordered_map>
#include <vector>

class IImGuiEditable;

/// <summary>
/// ObstacleDebris がシーンに頼むこと（部品の出し入れだけ）。RailStagePart が IRailStageHost へ取り次ぐ
/// </summary>
class IObstacleDebrisHost {
public:
	virtual ~IObstacleDebrisHost() = default;
	/// <summary>プレハブを置く（当たり判定のない部品）</summary>
	/// <param name="prefab">部品のプレハブ名</param>
	/// <param name="pos">置く位置（ワールド座標）</param>
	/// <returns>置いた部品。置けなければ nullptr</returns>
	virtual IImGuiEditable* SpawnDebrisPiece(const std::string& prefab, const Vector3& pos) = 0;
	/// <summary>SpawnDebrisPiece で置いた部品を消す</summary>
	/// <param name="entity">消す部品</param>
	virtual void DestroyDebrisPiece(IImGuiEditable* entity) = 0;
};

/// <summary>
/// 谷の障害物（とがった岩・石柱の梁。4_StageGimmick.md §11）が壊れた後の崩れ方。
/// 壊れた岩を、元と同じ原点・向きで切り分けた部品に差し替える：
///   残る部品（根元）… その場に置いたまま（シークで片付ける）
///   落ちる部品    … 支点（pivot）のまわりに倒れ／垂れ、releaseDeg を越えたら外れて回りながら落ちる
///   破片          … 折れ口からはじけて放物線で落ちる
/// どの岩が何に割れるかと動きの数字は Resources/Json/Tuning/ObstacleDebris.json（シークのたびに読み直す）。
/// 倒れる・垂れるのは Z 軸まわりだけ（描画と当たり判定で回転の掛け順が違うため、障害物の向きは Z だけで決めている）。
/// </summary>
class ObstacleDebris {
public:
	/// <summary>部品の出し入れを頼むホストを受け取り、割れ方の JSON を読む</summary>
	/// <param name="host">部品を置く・消すホスト</param>
	void Initialize(IObstacleDebrisHost* host);
	/// <summary>重力・消す高さ・寿命と、岩ごとの割れ方を JSON から読み直す（ファイルが無い・読めなければ今の値のまま）</summary>
	void LoadTuning();

	/// <summary>prefab の岩を残る部品・落ちる部品・破片に差し替えて崩す</summary>
	/// <param name="prefab">壊れた岩のプレハブ名</param>
	/// <param name="pos">壊れた岩の位置</param>
	/// <param name="rotate">壊れた岩の向き [rad]</param>
	/// <param name="awayFrom">倒す向きを決める基準（自機の位置。ここから離れる向きへ倒す）</param>
	/// <returns>崩したら true。割れ方が JSON に無い・ホストが無ければ false（何もしない）</returns>
	bool Break(const std::string& prefab, const Vector3& pos, const Vector3& rotate, const Vector3& awayFrom);

	/// <summary>落ちる部品・破片を動かし、谷底より下や寿命切れを消す</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void Update(float dt);
	/// <summary>出した部品をすべて消す（シーク）</summary>
	void Clear();

	/// <summary>崩れた部品か（シーンの保存から外す）</summary>
	/// <param name="entity">調べるエンティティ</param>
	/// <returns>このクラスが出した部品なら true</returns>
	bool Owns(const IImGuiEditable* entity) const;

private:
	struct FallDef {
		std::string prefab;
		Vector3 pivot{ 0.0f, 0.0f, 0.0f }; // 支点（部品のローカル）
		Vector3 tip{ 0.0f, 1.0f, 0.0f };   // 倒れる向きを決める点（部品のローカル。これが下がる／離れる向きへ倒す）
		float hingeAccel = 2.5f;           // 支点まわりの角加速度 [rad/s²]
		float releaseDeg = 35.0f;          // これだけ倒れたら外れて落ちる
	};
	struct ChunkDef {
		std::string prefab;
		int count = 0;
		Vector3 origin{ 0.0f, 0.0f, 0.0f }; // 飛び出す所（岩のローカル）
		float spread = 2.0f;                // 飛び出す所のばらつき [m]
		float speedMin = 6.0f;
		float speedMax = 14.0f;
		float scaleMin = 0.6f;
		float scaleMax = 1.8f;
	};
	struct BreakDef {
		std::vector<std::string> stays;
		std::vector<FallDef> falls;
		ChunkDef chunks;
	};
	struct Moving {
		IImGuiEditable* entity = nullptr;
		Vector3 rotate{ 0.0f, 0.0f, 0.0f };
		Vector3 position{ 0.0f, 0.0f, 0.0f }; // 部品の原点
		Vector3 velocity{ 0.0f, 0.0f, 0.0f };
		Vector3 spin{ 0.0f, 0.0f, 0.0f };     // 外れた後の回転の速さ [rad/s]
		Vector3 centerLocal{ 0.0f, 0.0f, 0.0f }; // 外れた後に回る中心（部品のローカル）
		// 支点で倒れている間
		bool hinged = false;
		Vector3 pivotLocal{ 0.0f, 0.0f, 0.0f };
		Vector3 pivotWorld{ 0.0f, 0.0f, 0.0f };
		float baseRoll = 0.0f;
		float angle = 0.0f;
		float angularVelocity = 0.0f;
		float direction = 1.0f;
		float hingeAccel = 0.0f;
		float releaseAngle = 0.0f;
		float life = 0.0f;
	};

	/// <summary>支点のまわりに倒れる部品を置く（倒す向きは tip が下がる方、同じなら awayFrom から離れる方）</summary>
	/// <param name="def">落ちる部品の定義</param>
	/// <param name="pos">壊れた岩の位置</param>
	/// <param name="rotate">壊れた岩の向き [rad]</param>
	/// <param name="awayFrom">倒す向きを決める基準の位置</param>
	void SpawnFall(const FallDef& def, const Vector3& pos, const Vector3& rotate, const Vector3& awayFrom);
	/// <summary>折れ口から上寄りにはじける破片を count 個置く</summary>
	/// <param name="def">破片の定義</param>
	/// <param name="pos">壊れた岩の位置</param>
	/// <param name="rotate">壊れた岩の向き [rad]</param>
	void SpawnChunks(const ChunkDef& def, const Vector3& pos, const Vector3& rotate);
	/// <summary>動いている部品の位置と向きをエンティティへ書き戻す</summary>
	/// <param name="m">書き戻す部品</param>
	void Apply(Moving& m) const;
	/// <summary>一様乱数</summary>
	/// <param name="a">下限</param>
	/// <param name="b">上限</param>
	/// <returns>a〜b の値</returns>
	float RandRange(float a, float b);

	IObstacleDebrisHost* host_ = nullptr;
	std::unordered_map<std::string, BreakDef> defs_;
	std::vector<Moving> moving_;
	std::vector<IImGuiEditable*> stays_;
	std::mt19937 rng_{ 20261029u };

	float gravity_ = 25.0f;   // 見栄えのため実際より強め
	float killY_ = -160.0f;   // これより下に落ちたら消す（谷底は −150）
	float maxLife_ = 6.0f;
};
