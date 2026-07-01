#include "InspectorWindow.h"
#include "Components/Gameplay.h"
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
        EntityTag currentTag = Gameplay::Of(selected).GetTag();
        std::string currentName(GetTagName(currentTag));
        if (ImGui::BeginCombo("Tag", currentName.c_str())) {
            for (int i = 0; i < static_cast<int>(EntityTag::Count); ++i) {
                EntityTag t = static_cast<EntityTag>(i);
                bool sel = (t == currentTag);
                if (ImGui::Selectable(std::string(GetTagName(t)).c_str(), sel)) {
                    Gameplay::Of(selected).SetTag(t);
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

    // ----- タグごとに表示する設定項目を決める -----
    // 各タグに必要なコンポーネントだけを Inspector / Prefab 保存に出す。
    const EntityTag inspTag = Gameplay::Of(selected).GetTag();
    const bool isPlayer       = (inspTag == EntityTag::Player);
    const bool isPlayerBullet = (inspTag == EntityTag::PlayerBullet);
    const bool isPlayerMelee  = (inspTag == EntityTag::PlayerMelee);
    const bool isEnemy        = (inspTag == EntityTag::Enemy);
    const bool isEnemyAttack  = (inspTag == EntityTag::EnemyAttack);
    const bool isBoss         = (inspTag == EntityTag::Boss);

    const bool showHP            = isPlayer || isEnemy || isBoss;
    const bool showAttackPower   = isPlayer;                        // 攻撃力の実数値（プレイヤーのみ）
    const bool showRawDamage     = isEnemy || isEnemyAttack || isBoss; // 固定ダメージ（敵側）
    const bool showAtkMultiplier = isPlayerBullet;                  // 攻撃倍率（弾）。近接は MeleeParams 側で倍率を持つ
    const bool showBullet        = isPlayerBullet || isEnemyAttack; // 弾パラメータ
    const bool showMelee         = isPlayerMelee;                   // 近接パラメータ（持続/オフセット/コンボ/本・持続あて）
    const bool showCarrier       = isEnemy || isBoss;
    const bool showMovement      = isEnemy;                         // 登場/移動の方法と速度（敵のみ）
    const bool showCharge        = isPlayer;
    const bool showPrecision     = isPlayer;
    const bool showBulletSlots   = isPlayer;
    const bool showScore         = isEnemy || isBoss;
    const bool showEffects       = isPlayer || isPlayerBullet || isPlayerMelee
                                 || isEnemy || isEnemyAttack || isBoss;
    const bool showBattle = showHP || showAttackPower || showRawDamage || showAtkMultiplier
                         || showBullet || showMelee || showCarrier || showMovement || showCharge || showPrecision
                         || showBulletSlots || showScore;

    // ----- コライダー（タグが衝突可能、かつ 3D エンティティの場合のみ表示） -----
    {
        const EntityTag tag = Gameplay::Of(selected).GetTag();
        if (CollisionMatrix::IsCollidableTag(tag) && selected->GetEditableTranslate()) {
            if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
                Collider& c = Gameplay::Of(selected).GetCollider();
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

    // ----- バトル系コンポーネント（タグごとに必要な項目だけ表示） -----
    if (showBattle) {
        if (ImGui::CollapsingHeader("Battle", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool needSep = false;
            auto sep = [&]() { if (needSep) ImGui::Separator(); needSep = true; };

            // HP（Player / Enemy / Boss）
            if (showHP) {
                sep();
                HP& hp = Gameplay::Of(selected).GetHP();
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
            }

            // AttackPower（Player のみ：攻撃力の実数値）
            if (showAttackPower) {
                sep();
                bool hasAP = Gameplay::Of(selected).HasAttackPower();
                if (ImGui::Checkbox("AttackPower Enabled", &hasAP)) {
                    Gameplay::Of(selected).SetHasAttackPower(hasAP);
                }
                if (hasAP) {
                    int ap = Gameplay::Of(selected).GetAttackPower();
                    if (ImGui::DragInt("Attack Power", &ap, 1, 0, 99999)) {
                        Gameplay::Of(selected).SetAttackPower(ap);
                    }
                }
                ImGui::TextDisabled("(弾・近接の攻撃倍率と掛けて敵へのダメージになる)");
            }

            // 固定ダメージ（Enemy / EnemyAttack / Boss）
            if (showRawDamage) {
                sep();
                DamageDealer& dd = Gameplay::Of(selected).GetDamageDealer();
                ImGui::Checkbox("Damage Enabled", &dd.enabled);
                if (dd.enabled) {
                    ImGui::DragInt("Damage", &dd.damage, 1, 0, 99999);
                    ImGui::TextDisabled("(敵側の固定ダメージ)");
                }
            }

            // 攻撃倍率（PlayerBullet / PlayerMelee：攻撃力 × 倍率 = 敵へのダメージ）
            if (showAtkMultiplier) {
                sep();
                DamageDealer& dd = Gameplay::Of(selected).GetDamageDealer();
                ImGui::Checkbox("Damage Enabled", &dd.enabled);
                if (dd.enabled) {
                    ImGui::DragFloat("Attack Multiplier", &dd.multiplier, 0.05f, 0.0f, 100.0f, "%.2f");
                    ImGui::TextDisabled("(発射/ヒット時に AttackPower * Multiplier を Damage に焼き込む)");
                }
            }

            // BulletParams（PlayerBullet / EnemyAttack）
            if (showBullet) {
                sep();
                BulletParams& bp = Gameplay::Of(selected).GetBulletParams();
                ImGui::Checkbox("BulletParams Enabled", &bp.enabled);
                if (bp.enabled) {
                    ImGui::DragFloat("Bullet Speed", &bp.speed, 0.5f, 0.0f, 1000.0f, "%.2f");
                    ImGui::DragFloat("Bullet Lifetime", &bp.lifetime, 0.1f, 0.0f, 60.0f, "%.2f sec");
                    ImGui::DragFloat("Homing (nearest)", &bp.homingStrength, 0.05f, 0.0f, 20.0f, "%.2f /sec");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("軽ホーミング：レティクル付近の最近敵へ向かう強さ");
                    }
                    ImGui::DragFloat("Strong Homing (locked)", &bp.strongHomingStrength, 0.05f, 0.0f, 20.0f, "%.2f /sec");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("強ホーミング：レティクルが重なってロック中の敵へ向かう強さ");
                    }
                    ImGui::DragFloat("Collider Growth /m", &bp.colliderGrowth, 0.005f, 0.0f, 1.0f, "%.3f");
                    ImGui::Checkbox("Penetrate", &bp.penetrate);
                    if (bp.penetrate) {
                        ImGui::DragFloat("Penetrate Damage Rate", &bp.penetrateDamageRate, 0.01f, 0.0f, 5.0f, "%.2f sec");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("貫通中、同じ敵に多段ヒットする間隔 (秒)");
                        }
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
                }
            }

            // MeleeParams（PlayerMelee：持続・オフセット・コンボ・本/持続あて倍率）
            if (showMelee) {
                sep();
                MeleeParams& mp = Gameplay::Of(selected).GetMeleeParams();
                ImGui::Checkbox("MeleeParams Enabled", &mp.enabled);
                if (mp.enabled) {
                    ImGui::DragFloat("Startup", &mp.startup, 0.01f, 0.0f, 5.0f, "%.2f sec");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("入力から判定が発生するまでの時間");
                    ImGui::DragFloat("Active Duration", &mp.activeDuration, 0.01f, 0.0f, 5.0f, "%.2f sec");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("判定の持続時間");
                    ImGui::DragFloat("Recovery", &mp.recovery, 0.01f, 0.0f, 5.0f, "%.2f sec");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("判定終了から次の行動（射撃/回避/近接）までの後隙");
                    ImGui::DragFloat3("Offset (R/U/F)", &mp.offset.x, 0.05f, -50.0f, 50.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("aim 基準オフセット 右(X)/上(Y)/前(Z)");
                    ImGui::DragFloat("Combo Window", &mp.comboWindow, 0.01f, 0.0f, 5.0f, "%.2f sec");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("後隙終了後、次段入力を受け付ける猶予");
                    ImGui::Separator();
                    ImGui::DragFloat("Clean Window", &mp.cleanWindow, 0.01f, 0.0f, 5.0f, "%.2f sec");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("判定発生からこの秒数までを「本あて」扱い");
                    ImGui::DragFloat("Clean Multiplier", &mp.cleanMultiplier, 0.05f, 0.0f, 100.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("本あて倍率：AttackPower × これ = ダメージ");
                    ImGui::DragFloat("Late Multiplier", &mp.lateMultiplier, 0.05f, 0.0f, 100.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("持続あて（遅れ当て）倍率");
                }
            }

            // CarrierParams（Enemy / Boss）
            if (showCarrier) {
                sep();
                CarrierParams& cp = Gameplay::Of(selected).GetCarrierParams();
                ImGui::Checkbox("CarrierParams Enabled", &cp.enabled);
                if (cp.enabled) {
                    ImGui::DragFloat("Child Lifetime", &cp.childLifetimeSec, 0.5f, 0.5f, 120.0f, "%.1f sec");
                    ImGui::DragFloat("Child Wander Radius", &cp.childWanderRadius, 0.5f, 0.5f, 50.0f, "%.1f");
                    ImGui::DragFloat("Child Move Speed", &cp.childMoveSpeed, 0.5f, 0.0f, 50.0f, "%.1f /sec");
                }
            }

            // MovementParams（Enemy：登場/移動の方法と速度）
            if (showMovement) {
                sep();
                MovementParams& mv = Gameplay::Of(selected).GetMovementParams();
                ImGui::Checkbox("MovementParams Enabled", &mv.enabled);
                if (mv.enabled) {
                    const char* kMovementNames[] = { "SplineFollow", "ScreenHover", "Drift", "Static" };
                    int mtIdx = static_cast<int>(mv.movementType);
                    if (ImGui::Combo("Movement Type", &mtIdx, kMovementNames, IM_ARRAYSIZE(kMovementNames))) {
                        mv.movementType = static_cast<MovementType>(mtIdx);
                    }
                    ImGui::DragFloat("Move Speed", &mv.moveSpeed, 0.5f, 0.0f, 200.0f, "%.1f /sec");
                    if (mv.movementType == MovementType::ScreenHover) {
                        ImGui::DragFloat("Hover Approach Speed", &mv.hoverApproachSpeed, 0.5f, 0.0f, 200.0f, "%.1f /sec");
                        ImGui::DragFloat("Hover Hold Duration", &mv.hoverHoldDuration, 0.1f, 0.0f, 60.0f, "%.1f sec");
                    }
                }
            }

            // ChargeParams（Player）
            if (showCharge) {
                sep();
                ChargeParams& chp = Gameplay::Of(selected).GetChargeParams();
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
            }

            // PrecisionParams（Player：精密射撃モード中の弾性能加算）
            if (showPrecision) {
                sep();
                PrecisionParams& pp = Gameplay::Of(selected).GetPrecisionParams();
                ImGui::Checkbox("Precision Enabled", &pp.enabled);
                if (pp.enabled) {
                    ImGui::DragFloat("Precision Speed +", &pp.speedAdd, 0.5f, 0.0f, 2000.0f, "%.1f /sec");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("精密モード中、全弾の弾速に加算");
                    }
                    ImGui::DragFloat("Precision Homing +", &pp.homingAdd, 0.05f, 0.0f, 20.0f, "%.2f /sec");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("精密モード中、強ホーミング（ロック弾）の強さに加算");
                    }
                }
            }

            // 弾プレハブスロット（Player）：normal / charge1 / charge2
            if (showBulletSlots) {
                sep();
                ImGui::TextDisabled("Bullet Prefab Slots");
                auto& bulletPrefabs = Gameplay::Of(selected).GetBulletPrefabs();
                // 弾スロットと近接スロット（攻撃スロットとして同じ map を流用）
                const char* slotNames[8] = {
                    "normal", "charge1", "charge2",
                    "melee_w1", "melee_w2", "melee_w3", "melee_w4", "melee_strong"
                };
                for (const char* slot : slotNames) {
                    ImGui::PushID(slot);
                    ImGui::TextUnformatted(slot);
                    ImGui::SameLine(90.0f);
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
            }

            // ScoreValue（Enemy / Boss）
            if (showScore) {
                sep();
                int sv = Gameplay::Of(selected).GetScoreValue();
                if (ImGui::DragInt("Score Value", &sv, 1, 0, 99999)) {
                    Gameplay::Of(selected).SetScoreValue(sv);
                }
                ImGui::TextDisabled("(撃破時の獲得スコア。0 で加点なし)");
            }
        }
        ImGui::Separator();
    }

    // ----- Effects スロット欄（タグが使うものだけ表示） -----
    // 任意のスロット名 → EffectManager 登録名のマップを編集する。
    // DnD ターゲット（SceneEditor の Effects 一覧から）と手入力の両対応。
    if (showEffects) {
        if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& effects = Gameplay::Of(selected).GetEffects();

            // タグに応じた推奨スロット名
            std::vector<const char*> suggested;
            if (isPlayer) {
                // charge系 + hurt（被弾時：自分が攻撃を受けた瞬間）
                suggested = { "charge_start", "charge_hold", "charge_start2", "charge_hold2", "hurt" };
            } else if (isPlayerBullet || isEnemyAttack) {
                // 弾：trail（弾追従ループ）/ hit（着弾：この攻撃が当てた側で出す）
                suggested = { "trail", "hit" };
            } else if (isPlayerMelee) {
                // swing（振り：判定追従）/ hit（着弾：この攻撃が当てた側で出す）
                suggested = { "swing", "hit" };
            } else if (isEnemy || isBoss) {
                // hurt（被弾時：攻撃を受けた瞬間）/ death（撃破時）
                suggested = { "hurt", "death" };
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
                    def.tag = Gameplay::Of(selected).GetTag();

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

                    const auto& col = Gameplay::Of(selected).GetCollider();
                    if (col.enabled) {
                        def.hasCollider = true;
                        def.colliderShape = col.shape;
                        def.colliderOffset = col.offset;
                        def.colliderRadius = col.radius;
                        def.colliderHalfExtents = col.halfExtents;
                        def.colliderCapsuleRadius = col.capsuleRadius;
                        def.colliderCapsuleHeight = col.capsuleHeight;
                    }

                    // バトル系：タグが使う項目だけプレハブに保存する
                    const HP& hp = Gameplay::Of(selected).GetHP();
                    if (showHP && hp.enabled) {
                        def.hasHP = true;
                        def.maxHP = hp.maxHP;
                    }
                    // ダメージ：敵側=固定ダメージ / 自機攻撃=攻撃倍率（どちらも DamageDealer）
                    const DamageDealer& dd = Gameplay::Of(selected).GetDamageDealer();
                    if ((showRawDamage || showAtkMultiplier) && dd.enabled) {
                        def.hasDamageDealer = true;
                        def.damage = dd.damage;
                        def.attackMultiplier = dd.multiplier;
                    }
                    if (showAttackPower && Gameplay::Of(selected).HasAttackPower()) {
                        def.hasAttackPower = true;
                        def.attackPower = Gameplay::Of(selected).GetAttackPower();
                    }
                    const BulletParams& bp = Gameplay::Of(selected).GetBulletParams();
                    if (showBullet && bp.enabled) {
                        def.hasBullet            = true;
                        def.bulletSpeed          = bp.speed;
                        def.bulletLifetime       = bp.lifetime;
                        def.bulletHomingStrength = bp.homingStrength;
                        def.bulletStrongHomingStrength = bp.strongHomingStrength;
                        def.bulletColliderGrowth = bp.colliderGrowth;
                        def.bulletPenetrate           = bp.penetrate;
                        def.bulletPenetrateDamageRate = bp.penetrateDamageRate;
                        def.bulletPenetrateEffect     = bp.penetrateEffect;
                    }
                    const MeleeParams& mp = Gameplay::Of(selected).GetMeleeParams();
                    if (showMelee && mp.enabled) {
                        def.hasMelee            = true;
                        def.meleeStartup        = mp.startup;
                        def.meleeActiveDuration = mp.activeDuration;
                        def.meleeOffset         = mp.offset;
                        def.meleeComboWindow    = mp.comboWindow;
                        def.meleeRecovery       = mp.recovery;
                        def.meleeCleanWindow     = mp.cleanWindow;
                        def.meleeCleanMultiplier = mp.cleanMultiplier;
                        def.meleeLateMultiplier  = mp.lateMultiplier;
                    }
                    const CarrierParams& cp = Gameplay::Of(selected).GetCarrierParams();
                    if (showCarrier && cp.enabled) {
                        def.hasCarrier               = true;
                        def.carrierChildLifetimeSec  = cp.childLifetimeSec;
                        def.carrierChildWanderRadius = cp.childWanderRadius;
                        def.carrierChildMoveSpeed    = cp.childMoveSpeed;
                    }
                    const MovementParams& mv = Gameplay::Of(selected).GetMovementParams();
                    if (showMovement && mv.enabled) {
                        def.hasMovement        = true;
                        def.movementType       = mv.movementType;
                        def.moveSpeed          = mv.moveSpeed;
                        def.hoverApproachSpeed = mv.hoverApproachSpeed;
                        def.hoverHoldDuration  = mv.hoverHoldDuration;
                    }
                    const ChargeParams& chp = Gameplay::Of(selected).GetChargeParams();
                    if (showCharge && chp.enabled) {
                        def.hasCharge        = true;
                        def.chargeStage1Time = chp.stage1Time;
                        def.chargeStage2Time = chp.stage2Time;
                        def.chargeFireRate   = chp.fireRate;
                    }
                    const PrecisionParams& prp = Gameplay::Of(selected).GetPrecisionParams();
                    if (showPrecision && prp.enabled) {
                        def.hasPrecision       = true;
                        def.precisionSpeedAdd  = prp.speedAdd;
                        def.precisionHomingAdd = prp.homingAdd;
                    }
                    // ScoreValue（Enemy/Boss のみ反映）
                    if (showScore) def.scoreValue = Gameplay::Of(selected).GetScoreValue();
                    // エフェクトスロットをまるごと保存
                    def.effects = Gameplay::Of(selected).GetEffects();
                    // 弾プレハブスロットをまるごと保存
                    def.bulletPrefabs = Gameplay::Of(selected).GetBulletPrefabs();
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
