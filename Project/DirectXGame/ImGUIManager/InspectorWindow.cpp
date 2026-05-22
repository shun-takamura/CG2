#include "InspectorWindow.h"
#include "ImGuiManager.h"
#include "SceneEditorWindow.h"
#include "Components/EntityTag.h"
#include "Components/CollisionMatrix.h"
#include "Components/SphereCollider.h"
#include "Components/HP.h"
#include "Components/DamageDealer.h"
#include "Components/Prefab.h"
#include "Components/PrefabManager.h"
#include "Object3DInstance.h"
#include "AnimatedObject3DInstance.h"
#include "PrimitiveInstance.h"
#include "LogBuffer.h"

#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

void InspectorWindow::OnDraw() {
#ifdef _DEBUG

    IImGuiEditable* selected = manager_->GetSelected();

    if (!selected) {
        ImGui::Text("No object selected");
        ImGui::TextDisabled("Select an object from Hierarchy");
        return;
    }

    // ----- 名前編集 -----
    {
        char nameBuf[256];
        const std::string current = selected->GetName();
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", current.c_str());
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf),
            ImGuiInputTextFlags_EnterReturnsTrue)) {
            selected->SetName(nameBuf);
        }
    }

    ImGui::Text("Type: %s", selected->GetTypeName().c_str());

    // ----- Tag 選択 -----
    {
        EntityTag currentTag = selected->GetTag();
        std::string currentName(GetTagName(currentTag));
        if (ImGui::BeginCombo("Tag", currentName.c_str())) {
            for (int i = 0; i < static_cast<int>(EntityTag::Count); ++i) {
                EntityTag t = static_cast<EntityTag>(i);
                bool sel = (t == currentTag);
                if (ImGui::Selectable(std::string(GetTagName(t)).c_str(), sel)) {
                    selected->SetTag(t);
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    // ----- Debug表示ON/OFF -----
    {
        bool visible = selected->IsVisibleInEditor();
        if (ImGui::Checkbox("Visible (Debug)", &visible)) {
            selected->SetVisibleInEditor(visible);
        }
    }

    ImGui::Separator();

    // ----- コライダー（タグが衝突可能、かつ 3D エンティティの場合のみ表示） -----
    {
        const EntityTag tag = selected->GetTag();
        if (CollisionMatrix::IsCollidableTag(tag) && selected->GetEditableTranslate()) {
            if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
                Collider& c = selected->GetCollider();
                ImGui::Checkbox("OnCollision", &c.enabled);
                if (c.enabled) {
                    // 形状コンボ
                    const char* shapeNames[] = { "Sphere", "OBB", "Capsule" };
                    int shapeIdx = static_cast<int>(c.shape);
                    if (ImGui::Combo("Shape", &shapeIdx, shapeNames, IM_ARRAYSIZE(shapeNames))) {
                        c.shape = static_cast<ColliderShape>(shapeIdx);
                    }

                    // 共通: オフセット
                    ImGui::DragFloat3("Offset", &c.offset.x, 0.05f);

                    // 形状別パラメータ
                    switch (c.shape) {
                    case ColliderShape::Sphere:
                        ImGui::DragFloat("Radius", &c.radius, 0.05f, 0.0f, 100.0f, "%.2f");
                        break;
                    case ColliderShape::OBB:
                        ImGui::DragFloat3("Half Extents", &c.halfExtents.x, 0.05f, 0.0f, 100.0f, "%.2f");
                        break;
                    case ColliderShape::Capsule:
                        ImGui::DragFloat("Capsule Radius", &c.capsuleRadius, 0.05f, 0.0f, 100.0f, "%.2f");
                        ImGui::DragFloat("Capsule Height", &c.capsuleHeight, 0.05f, 0.0f, 100.0f, "%.2f");
                        break;
                    }

                    ImGui::Checkbox("Show Debug", &c.showDebug);
                }
            }
            ImGui::Separator();
        }
    }

    // ----- HP / DamageDealer / AttackPower（バトル系コンポーネント） -----
    {
        if (ImGui::CollapsingHeader("Battle", ImGuiTreeNodeFlags_DefaultOpen)) {
            // HP
            HP& hp = selected->GetHP();
            ImGui::Checkbox("HP Enabled", &hp.enabled);
            if (hp.enabled) {
                ImGui::DragInt("Max HP", &hp.maxHP, 1, 1, 99999);
                if (hp.currentHP > hp.maxHP) hp.currentHP = hp.maxHP;
                ImGui::DragInt("Current HP", &hp.currentHP, 1, 0, hp.maxHP);
                const float frac = hp.maxHP > 0
                    ? static_cast<float>(hp.currentHP) / static_cast<float>(hp.maxHP)
                    : 0.0f;
                ImGui::ProgressBar(frac);
            }

            ImGui::Separator();

            // DamageDealer
            DamageDealer& dd = selected->GetDamageDealer();
            ImGui::Checkbox("DamageDealer Enabled", &dd.enabled);
            if (dd.enabled) {
                ImGui::DragInt("Damage", &dd.damage, 1, 0, 99999);
                ImGui::DragFloat("Attack Multiplier", &dd.multiplier, 0.05f, 0.0f, 100.0f, "%.2f");
                ImGui::TextDisabled("(プレイヤー攻撃: 発射時に AttackPower * Multiplier を Damage に焼き込む)");
            }

            ImGui::Separator();

            // AttackPower
            bool hasAP = selected->HasAttackPower();
            if (ImGui::Checkbox("AttackPower Enabled", &hasAP)) {
                selected->SetHasAttackPower(hasAP);
            }
            if (hasAP) {
                int ap = selected->GetAttackPower();
                if (ImGui::DragInt("Attack Power", &ap, 1, 0, 99999)) {
                    selected->SetAttackPower(ap);
                }
            }

            ImGui::Separator();

            // BulletParams（弾プレハブ用：速度・寿命・ホーミング・貫通）
            BulletParams& bp = selected->GetBulletParams();
            ImGui::Checkbox("BulletParams Enabled", &bp.enabled);
            if (bp.enabled) {
                ImGui::DragFloat("Bullet Speed", &bp.speed, 0.5f, 0.0f, 1000.0f, "%.2f");
                ImGui::DragFloat("Bullet Lifetime", &bp.lifetime, 0.1f, 0.0f, 60.0f, "%.2f sec");
                ImGui::DragFloat("Bullet Homing Strength", &bp.homingStrength, 0.05f, 0.0f, 20.0f, "%.2f /sec");
                ImGui::DragFloat("Collider Growth /m", &bp.colliderGrowth, 0.005f, 0.0f, 1.0f, "%.3f");
                ImGui::Checkbox("Penetrate", &bp.penetrate);
                if (bp.penetrate) {
                    ImGui::DragFloat("Penetrate Damage Rate", &bp.penetrateDamageRate, 0.01f, 0.0f, 5.0f, "%.2f sec");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("貫通中、同じ敵に多段ヒットする間隔 (秒)");
                    }
                    // 貫通中ダメージ時のエフェクト（SceneEditor の Effects 一覧から DnD）
                    char effBuf[128];
                    std::snprintf(effBuf, sizeof(effBuf), "%s", bp.penetrateEffect.c_str());
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::InputText("Penetrate Effect", effBuf, sizeof(effBuf))) {
                        bp.penetrateEffect = effBuf;
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(EFFECT_RES_DROP_PAYLOAD_TYPE)) {
                            const auto* pld = static_cast<const EffectResDropPayload*>(p->Data);
                            bp.penetrateEffect = pld->effectName;
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
                ImGui::TextDisabled("(SpawnEnemyBullet / SpawnPlayerBullet 時にプレハブの bullet から読まれる)");
            }

            ImGui::Separator();

            // CarrierParams（運び屋プレハブ用：子敵パラメータ）
            CarrierParams& cp = selected->GetCarrierParams();
            ImGui::Checkbox("CarrierParams Enabled", &cp.enabled);
            if (cp.enabled) {
                ImGui::DragFloat("Child Lifetime", &cp.childLifetimeSec, 0.5f, 0.5f, 120.0f, "%.1f sec");
                ImGui::DragFloat("Child Wander Radius", &cp.childWanderRadius, 0.5f, 0.5f, 50.0f, "%.1f");
                ImGui::DragFloat("Child Move Speed", &cp.childMoveSpeed, 0.5f, 0.0f, 50.0f, "%.1f /sec");
            }

            ImGui::Separator();

            // ChargeParams（プレイヤープレハブ用：チャージ時間 + 連射間隔）
            ChargeParams& chp = selected->GetChargeParams();
            ImGui::Checkbox("ChargeParams Enabled", &chp.enabled);
            if (chp.enabled) {
                ImGui::DragFloat("Charge Stage1 Time", &chp.stage1Time, 0.05f, 0.0f, 30.0f, "%.2f sec");
                ImGui::DragFloat("Charge Stage2 Time", &chp.stage2Time, 0.05f, 0.0f, 60.0f, "%.2f sec");
                ImGui::DragFloat("Fire Rate", &chp.fireRate, 0.01f, 0.0f, 5.0f, "%.2f sec");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("通常弾の連射間隔 (秒)");
                }
                ImGui::TextDisabled("(stage2Time は合計時間。stage1Time より大きい値を設定する)");
            }

            // 弾プレハブスロット（Player タグのみ）：通常 / charge1 / charge2
            if (selected->GetTag() == EntityTag::Player) {
                ImGui::Separator();
                ImGui::TextDisabled("Bullet Prefab Slots");
                auto& bulletPrefabs = selected->GetBulletPrefabs();
                const char* slotNames[3] = { "normal", "charge1", "charge2" };
                for (const char* slot : slotNames) {
                    ImGui::PushID(slot);
                    ImGui::TextUnformatted(slot);
                    ImGui::SameLine(80.0f);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "%s", bulletPrefabs[slot].c_str());
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::InputText("##bp", buf, sizeof(buf))) {
                        bulletPrefabs[slot] = buf;
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(PREFAB_DROP_PAYLOAD_TYPE)) {
                            const auto* pld = static_cast<const PrefabDropPayload*>(p->Data);
                            bulletPrefabs[slot] = pld->prefabName;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::PopID();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("SceneEditor の Prefab 一覧から弾プレハブを DnD で割り当て");
                }
            }

            // ScoreValue（Enemy/Boss タグの時だけ表示）
            const EntityTag scoreTag = selected->GetTag();
            if (scoreTag == EntityTag::Enemy || scoreTag == EntityTag::Boss) {
                ImGui::Separator();
                int sv = selected->GetScoreValue();
                if (ImGui::DragInt("Score Value", &sv, 1, 0, 99999)) {
                    selected->SetScoreValue(sv);
                }
                ImGui::TextDisabled("(撃破時の獲得スコア。0 で加点なし)");
            }
        }
        ImGui::Separator();
    }

    // ----- Effects スロット欄 -----
    // 任意のスロット名 → EffectManager 登録名のマップを編集する。
    // DnD ターゲット（SceneEditor の Effects 一覧から）と手入力の両対応。
    {
        if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& effects = selected->GetEffects();

            // タグに応じた推奨スロット名
            const EntityTag tag = selected->GetTag();
            std::vector<const char*> suggested;
            if (tag == EntityTag::Player) {
                suggested = { "charge1", "charge2" };
            } else if (CollisionMatrix::IsCollidableTag(tag) && tag != EntityTag::Player) {
                suggested = { "hit", "death" };
            }

            // 既存スロットを一覧表示（順序安定のためソート）
            std::vector<std::string> keys;
            keys.reserve(effects.size());
            for (auto& kv : effects) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());

            std::string slotToRemove;
            for (const auto& key : keys) {
                ImGui::PushID(key.c_str());

                ImGui::TextUnformatted(key.c_str());
                ImGui::SameLine(120.0f);

                char valBuf[128];
                std::snprintf(valBuf, sizeof(valBuf), "%s", effects[key].c_str());
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::InputText("##val", valBuf, sizeof(valBuf))) {
                    effects[key] = valBuf;
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(EFFECT_RES_DROP_PAYLOAD_TYPE)) {
                        const auto* pld = static_cast<const EffectResDropPayload*>(p->Data);
                        effects[key] = pld->effectName;
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    slotToRemove = key;
                }
                ImGui::PopID();
            }
            if (!slotToRemove.empty()) {
                effects.erase(slotToRemove);
            }

            // 推奨スロットをワンクリック追加
            ImGui::Separator();
            for (const char* s : suggested) {
                if (effects.count(s) == 0) {
                    ImGui::PushID(s);
                    if (ImGui::SmallButton((std::string("+ ") + s).c_str())) {
                        effects[s] = "";
                    }
                    ImGui::PopID();
                    ImGui::SameLine();
                }
            }
            static char newSlotBuf[64] = "";
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputText("##newSlot", newSlotBuf, sizeof(newSlotBuf));
            ImGui::SameLine();
            if (ImGui::SmallButton("Add slot") && newSlotBuf[0] != '\0') {
                effects[newSlotBuf] = "";
                newSlotBuf[0] = '\0';
            }
        }
        ImGui::Separator();
    }

    // 各オブジェクトが実装した編集UIを描画
    selected->OnImGuiInspector();

    // ----- Save as Prefab（Object3D / AnimatedObject3D / Primitive 対応） -----
    {
        const std::string typeName = selected->GetTypeName();
        const bool isObj3D    = (typeName == "Object3D");
        const bool isAnimated = (typeName == "AnimatedObject3D");
        const bool isPrimitive = (typeName == "Primitive");
        if (isObj3D || isAnimated || isPrimitive) {
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Prefab", ImGuiTreeNodeFlags_DefaultOpen)) {
                static char prefabNameBuf[128] = "";
                static IImGuiEditable* lastSelected = nullptr;
                if (selected != lastSelected) {
                    std::snprintf(prefabNameBuf, sizeof(prefabNameBuf), "%s",
                        selected->GetName().c_str());
                    lastSelected = selected;
                }
                ImGui::InputText("Prefab Name", prefabNameBuf, sizeof(prefabNameBuf));
                if (ImGui::Button("Save as Prefab")) {
                    PrefabDef def{};
                    def.name = prefabNameBuf;
                    def.tag = selected->GetTag();

                    if (isObj3D) {
                        def.kind = PrefabKind::Object3D;
                        def.isAnimated = false;
                        auto* obj = static_cast<Object3DInstance*>(selected);
                        def.modelDir = obj->GetDirectoryPath();
                        def.modelFile = obj->GetModelFileName();
                        def.defaultScale = obj->GetScale();
                        def.defaultRotate = obj->GetRotate();
                    } else if (isAnimated) {
                        def.kind = PrefabKind::Animated;
                        def.isAnimated = true;
                        auto* obj = static_cast<AnimatedObject3DInstance*>(selected);
                        def.modelDir = obj->GetDirectoryPath();
                        def.modelFile = obj->GetModelFileName();
                        def.defaultScale = obj->GetScale();
                        def.defaultRotate = obj->GetRotate();
                    } else { // isPrimitive
                        def.kind = PrefabKind::Primitive;
                        def.isAnimated = false;
                        auto* prim = static_cast<PrimitiveInstance*>(selected);
                        const auto& tr = prim->GetMesh().GetTransform();
                        def.defaultScale = tr.scale;
                        def.defaultRotate = tr.rotate;

                        auto& pp = def.primitiveParams;
                        pp.primitiveType = static_cast<int>(prim->GetPrimitiveType());
                        pp.texturePath   = prim->GetTextureFilePath();
                        pp.color         = prim->GetMesh().GetColor();
                        pp.blendMode     = static_cast<int>(prim->GetMesh().GetBlendMode());
                        pp.depthWrite    = prim->GetMesh().GetDepthWrite();
                        pp.alphaReference = prim->GetAlphaReference();
                        pp.cullBackface  = prim->GetCullBackface();
                        pp.samplerMode   = prim->GetSamplerMode();
                        pp.uvAutoScroll  = prim->GetAutoScroll();
                        pp.uvScrollSpeed = prim->GetScrollSpeed();
                        pp.uvOffset      = prim->GetManualUVOffset();
                        pp.uvScale       = prim->GetUVScale();
                        pp.uvFlipU       = prim->GetFlipU();
                        pp.uvFlipV       = prim->GetFlipV();
                        pp.billboardMode = prim->GetMesh().GetBillboardMode();
                        pp.timeGroup     = prim->GetTimeGroup();
                        pp.ringParams     = prim->GetRingParams();
                        pp.cylinderParams = prim->GetCylinderParams();
                        pp.helixParams    = prim->GetHelixParams();
                    }

                    const auto& col = selected->GetCollider();
                    if (col.enabled) {
                        def.hasCollider = true;
                        def.colliderShape = col.shape;
                        def.colliderOffset = col.offset;
                        def.colliderRadius = col.radius;
                        def.colliderHalfExtents = col.halfExtents;
                        def.colliderCapsuleRadius = col.capsuleRadius;
                        def.colliderCapsuleHeight = col.capsuleHeight;
                    }

                    // HP / DamageDealer / AttackPower をプレハブに保存
                    const HP& hp = selected->GetHP();
                    if (hp.enabled) {
                        def.hasHP = true;
                        def.maxHP = hp.maxHP;
                    }
                    const DamageDealer& dd = selected->GetDamageDealer();
                    if (dd.enabled) {
                        def.hasDamageDealer = true;
                        def.damage = dd.damage;
                        def.attackMultiplier = dd.multiplier;
                    }
                    if (selected->HasAttackPower()) {
                        def.hasAttackPower = true;
                        def.attackPower = selected->GetAttackPower();
                    }
                    const BulletParams& bp = selected->GetBulletParams();
                    if (bp.enabled) {
                        def.hasBullet            = true;
                        def.bulletSpeed          = bp.speed;
                        def.bulletLifetime       = bp.lifetime;
                        def.bulletHomingStrength = bp.homingStrength;
                        def.bulletColliderGrowth = bp.colliderGrowth;
                        def.bulletPenetrate           = bp.penetrate;
                        def.bulletPenetrateDamageRate = bp.penetrateDamageRate;
                        def.bulletPenetrateEffect     = bp.penetrateEffect;
                    }
                    const CarrierParams& cp = selected->GetCarrierParams();
                    if (cp.enabled) {
                        def.hasCarrier               = true;
                        def.carrierChildLifetimeSec  = cp.childLifetimeSec;
                        def.carrierChildWanderRadius = cp.childWanderRadius;
                        def.carrierChildMoveSpeed    = cp.childMoveSpeed;
                    }
                    const ChargeParams& chp = selected->GetChargeParams();
                    if (chp.enabled) {
                        def.hasCharge        = true;
                        def.chargeStage1Time = chp.stage1Time;
                        def.chargeStage2Time = chp.stage2Time;
                        def.chargeFireRate   = chp.fireRate;
                    }
                    // ScoreValue（インスタンスの値をそのままプレハブへ反映）
                    def.scoreValue = selected->GetScoreValue();
                    // エフェクトスロットをまるごと保存
                    def.effects = selected->GetEffects();
                    // 弾プレハブスロットをまるごと保存
                    def.bulletPrefabs = selected->GetBulletPrefabs();
                    std::string path = std::string(PrefabManager::GetPrefabDir())
                        + "/" + def.name + ".json";
                    bool ok = PrefabManager::Save(def, path);
                    LogBuffer::Instance().Add(
                        ok ? ("Prefab saved: " + path)
                           : ("Prefab save FAILED: " + path),
                        ok ? LogBuffer::Level::Info : LogBuffer::Level::Error);
                    if (ok) {
                        PrefabManager::GetInstance()->Rescan();
                    }
                }
            }
        }
    }

#endif // DEBUG
}
