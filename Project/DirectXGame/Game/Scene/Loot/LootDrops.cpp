#include "LootDrops.h"

#include "IImGuiEditable.h"
#include "Score/LootCatalog.h"
#include "Score/LootLedger.h"
#include "Score/DifficultyProfile.h"
#include <algorithm>
#include <cmath>

namespace {
	Vector3 Add(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	Vector3 Sub(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	Vector3 Scale(const Vector3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	float Length(const Vector3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
	constexpr float kTwoPi = 6.2831853f;
}

void LootDrops::Initialize(ILootDropsHost* host) {
	host_ = host;
}

float LootDrops::RandRange(float a, float b) {
	std::uniform_real_distribution<float> dist(a, b);
	return dist(rng_);
}

void LootDrops::DropFrom(const std::string& enemyPrefab, const Vector3& pos) {
	const LootCatalog* catalog = LootCatalog::GetInstance();
	const LootTable* table = catalog->FindTable(enemyPrefab);
	if (!table || !host_) return;

	Vector3 target = pos;
	host_->GetLootCollectPoint(target);
	target.y += catalog->GetMotion().aimHeight;

	// エーテル：1 粒にまとめて落とす。報酬の倍率（難易度）を掛ける
	if (table->ether > 0) {
		const int ether = DifficultyProfile::ScaleInt(table->ether, DifficultyProfile::GetInstance()->GetRewardScale());
		const LootItemDef& e = catalog->GetEther();
		Spawn(LootLedger::kEtherId, e.prefab, ether, e.scale, e.spinSpeed, pos, target);
	}
	// 素材：1 個ずつ落とす（拾うたびに右下の数が増えて見える）
	for (const LootRoll& roll : table->rolls) {
		if (RandRange(0.0f, 1.0f) >= roll.chance) continue;
		const LootItemDef* item = catalog->FindItem(roll.itemId);
		if (!item) continue;
		for (int i = 0; i < roll.count; ++i) {
			Spawn(item->id, item->prefab, 1, item->scale, item->spinSpeed, pos, target);
		}
	}
}

void LootDrops::Spawn(const std::string& itemId, const std::string& prefab, int amount, float scale, float spinSpeed,
	const Vector3& pos, const Vector3& target) {
	const LootMotionParams& m = LootCatalog::GetInstance()->GetMotion();
	// 上限を越えたら見せずに拾ったことにする（取りこぼさない）
	if (static_cast<int>(pieces_.size()) >= m.maxPieces || prefab.empty()) {
		LootLedger::GetInstance()->Add(itemId, amount);
		return;
	}
	IImGuiEditable* entity = host_->SpawnLootPiece(prefab, pos);
	if (!entity) {
		LootLedger::GetInstance()->Add(itemId, amount);
		return;
	}

	Piece p;
	p.entity = entity;
	p.itemId = itemId;
	p.amount = amount;
	p.offset = Sub(pos, target);
	// 弾ける向き：上半分の球の中でばらつかせる
	const float yaw = RandRange(0.0f, kTwoPi);
	const float up = RandRange(0.2f, 1.0f);
	const float side = std::sqrt((std::max)(0.0f, 1.0f - up * up));
	p.velocity = Scale({ std::cos(yaw) * side, up, std::sin(yaw) * side }, RandRange(m.popSpeedMin, m.popSpeedMax));
	p.rotate = { RandRange(0.0f, kTwoPi), RandRange(0.0f, kTwoPi), 0.0f };
	p.spin = { spinSpeed * RandRange(0.3f, 0.6f), spinSpeed, 0.0f };
	p.scale = scale;
	entity->SetScale({ 0.0f, 0.0f, 0.0f });
	entity->SetRotate(p.rotate);
	pieces_.push_back(p);
}

void LootDrops::Update(float dt) {
	if (pieces_.empty() || !host_ || dt <= 0.0f) return;
	const LootMotionParams& m = LootCatalog::GetInstance()->GetMotion();
	Vector3 target{};
	const bool hasTarget = host_->GetLootCollectPoint(target);
	target.y += m.aimHeight;

	for (Piece& p : pieces_) {
		p.age += dt;
		if (p.age < m.popTime || !hasTarget) {
			// 弾ける：勢いを指数で落とす
			p.offset = Add(p.offset, Scale(p.velocity, dt));
			p.velocity = Scale(p.velocity, std::exp(-m.popDamping * dt));
		} else {
			// 吸い寄せる：残りの距離を縮め、最低の速さも保つ
			const float homeTime = p.age - m.popTime;
			const float rate = m.homeRate + m.homeAccel * homeTime;
			const float len = Length(p.offset);
			float next = len * std::exp(-rate * dt);
			next = (std::min)(next, len - m.homeMinSpeed * dt);
			next = (std::max)(next, 0.0f);
			p.offset = len > 1e-5f ? Scale(p.offset, next / len) : Vector3{};
		}
		p.rotate = Add(p.rotate, Scale(p.spin, dt));

		const float len = Length(p.offset);
		if ((p.age >= m.popTime && len <= m.collectRadius) || p.age >= m.maxLifetime) {
			Collect(p);
			continue;
		}

		// 出たときに大きくなり、自機の近くで少し縮む
		const float appear = m.appearTime > 0.0f ? (std::min)(p.age / m.appearTime, 1.0f) : 1.0f;
		const float shrink = std::clamp(len / (m.collectRadius * 3.0f), 0.4f, 1.0f);
		const float s = p.scale * appear * shrink;
		if (Vector3* t = p.entity->GetEditableTranslate()) *t = Add(target, p.offset);
		p.entity->SetRotate(p.rotate);
		p.entity->SetScale({ s, s, s });
	}
	pieces_.erase(std::remove_if(pieces_.begin(), pieces_.end(), [](const Piece& p) { return p.entity == nullptr; }),
		pieces_.end());
}

void LootDrops::Collect(Piece& p) {
	LootLedger::GetInstance()->Add(p.itemId, p.amount);
	if (p.entity && host_) host_->DestroyLootPiece(p.entity);
	p.entity = nullptr;
}

void LootDrops::CollectAll() {
	for (Piece& p : pieces_) Collect(p);
	pieces_.clear();
}

void LootDrops::Clear() {
	if (host_) {
		for (Piece& p : pieces_) {
			if (p.entity) host_->DestroyLootPiece(p.entity);
		}
	}
	pieces_.clear();
}

bool LootDrops::Owns(const IImGuiEditable* entity) const {
	if (!entity) return false;
	for (const Piece& p : pieces_) {
		if (p.entity == entity) return true;
	}
	return false;
}
