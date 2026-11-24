#include "DemoScene.h"
#include "Components/Gameplay.h"
#include "SceneManager.h"
#include <algorithm>

//============================
// 自作ヘッダーのインクルード
//============================
#include "DebugCamera.h"
#include "WindowsApplication.h"
#include "DirectXCore.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "Camera.h"
#include "SRVManager.h"
#include "ParticleManager.h"
#include "GPUParticleManager.h"
#include "Effect/EffectManager.h"
#include "SoundManager.h"
#include "ControllerInput.h"
#include "MouseInput.h"
#include "InputManager.h"
#include "Debug.h"
#include "LightManager.h"
#include "KeyboardInput.h"
#include "TextureManager.h"
#include"Game.h"
#include "ImGuiManager.h"
#include "Skybox.h"
#include "Primitive/PrimitiveMesh.h"
#include "Primitive/PrimitiveGenerator.h"
#include "Easing.h"
#include "Interpolator.h"
#include "Log.h"
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "LogBuffer.h"
#include "Primitive/DebugDraw.h"
#include "Spline/SplineCurveActor.h"
#include "Components/PrefabManager.h"
#include "Components/Prefab.h"
#include <numbers>
#include <sstream>
#include <filesystem>
#include "AnimatedModelInstance.h"
#include "AnimatedObject3DInstance.h"
#include "ModelManager.h"
#include "IImGuiEditable.h"
#include "LineRenderer.h"
#include "MathUtility.h"
#include "Fracture/FracturedMeshRenderer.h"
#include "Fracture/VoronoiFracture.h"

// =============================================================
// 岩のボロノイ分割の確認（ボス必殺技で割る岩）
// 左＝割る前 / 中＝割れて広がる / 右＝割れてディゾルブ。ImGui「岩のボロノイ分割」で割り方と見た目を変える
// =============================================================
struct DemoScene::FracturePreview {
	static constexpr const char* kModels[] = {
		"Resources/Models/Rocks/BossRock_A.glb",
		"Resources/Models/Rocks/BossRock_B.glb",
		"Resources/Models/Rocks/BossRock_C.glb",
	};
	static constexpr int kRockCount = 3;
	// 岩どうしの間隔 [m]
	static constexpr float kRockSpacing = 6.0f;
	// 岩を置く高さ [m]
	static constexpr float kRockHeight = 2.5f;
	// 自動で回す速さ [rad/s]
	static constexpr float kAutoRotateSpeed = 0.3f;

	FracturedMeshRenderer renderer;
	VoronoiFracture::Settings settings;
	int modelIndex = 0;
	bool built = false;
	size_t fragmentCount = 0;
	size_t vertexCount = 0;
	uint32_t capFailures = 0;
	float buildMs = 0.0f;
	std::vector<Vector3> spinAxes;
	std::vector<FracturedMeshRenderer::FragmentPose> explodedPoses;
	std::vector<FracturedMeshRenderer::FragmentPose> dissolvePoses;
	float rockScale = 2.0f;
	float explode = 0.4f;
	float spin = 0.5f;
	float dissolve = 0.6f;
	float yaw = 0.0f;
	bool autoRotate = true;

	/// <summary>選んだモデルを今の設定で割り直す</summary>
	void Rebuild() {
		built = false;
		VoronoiFracture::SourceMesh mesh;
		if (!VoronoiFracture::LoadSourceMesh(kModels[modelIndex], mesh)) {
			Log(std::string("[FracturePreview] モデルを読めない: ") + kModels[modelIndex] + "\n");
			renderer.SetMesh(VoronoiFracture::Result{});
			return;
		}
		const VoronoiFracture::Result result = VoronoiFracture::Build(mesh, settings);
		renderer.SetMesh(result);
		built = result.IsValid();
		fragmentCount = result.fragments.size();
		vertexCount = result.vertices.size();
		capFailures = result.capFailures;
		buildMs = result.buildMilliseconds;
		Log("[FracturePreview] fragments=" + std::to_string(fragmentCount) + " verts=" + std::to_string(vertexCount) +
			" capFailures=" + std::to_string(capFailures) + " ms=" + std::to_string(buildMs) + "\n");

		std::mt19937 rng(settings.randomSeed);
		std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
		spinAxes.resize(fragmentCount);
		for (Vector3& axis : spinAxes) {
			const Vector3 v{ dist(rng), dist(rng), dist(rng) };
			axis = Length(v) > 1e-3f ? Normalize(v) : Vector3{ 0.0f, 1.0f, 0.0f };
		}
	}

	/// <summary>広がり・回転・ディゾルブの量から破片の姿勢を作る</summary>
	void UpdatePoses() {
		const auto& fragments = renderer.GetFragments();
		explodedPoses.resize(fragments.size());
		dissolvePoses.resize(fragments.size());
		for (size_t i = 0; i < fragments.size(); ++i) {
			const Vector3& c = fragments[i].center;
			const float len = Length(c);
			const Vector3 dir = len > 1e-4f ? Vector3{ c.x / len, c.y / len, c.z / len } : Vector3{ 0.0f, 1.0f, 0.0f };
			const float distance = explode * (0.5f + len);
			FracturedMeshRenderer::FragmentPose pose;
			pose.offset = { dir.x * distance, dir.y * distance, dir.z * distance };
			pose.rotation = MakeRotateAxisAngleQuaternion(spinAxes[i], spin * explode * kPi);
			explodedPoses[i] = pose;
			pose.visibility = dissolve;
			dissolvePoses[i] = pose;
		}
	}

	/// <summary>3 個の岩を描く（3D の描画の途中で呼ぶ）</summary>
	void Draw(const Camera& camera) {
		if (!built) return;
		UpdatePoses();
		std::vector<FracturedMeshRenderer::RockDraw> rocks(kRockCount);
		for (int k = 0; k < kRockCount; ++k) {
			Transform t{ { rockScale, rockScale, rockScale }, { 0.0f, yaw, 0.0f },
				{ (static_cast<float>(k) - 1.0f) * kRockSpacing, kRockHeight, 0.0f } };
			rocks[k].world = MakeAffineMatrix(t);
			rocks[k].textureScale = rockScale;
		}
		rocks[1].broken = true;
		rocks[1].fragments = &explodedPoses;
		rocks[2].broken = true;
		rocks[2].fragments = &dissolvePoses;
		renderer.Draw(camera, rocks);
	}

#ifdef USE_IMGUI
	/// <summary>割り方と見た目の ImGui</summary>
	void OnImGui() {
		if (!ImGui::Begin("岩のボロノイ分割")) {
			ImGui::End();
			return;
		}
		bool rebuild = false;
		rebuild |= ImGui::Combo("モデル", &modelIndex, "BossRock_A\0BossRock_B\0BossRock_C\0");
		int seedCount = static_cast<int>(settings.seedCount);
		if (ImGui::SliderInt("種点の数", &seedCount, 2, 64)) {
			settings.seedCount = static_cast<uint32_t>(seedCount);
			rebuild = true;
		}
		int randomSeed = static_cast<int>(settings.randomSeed);
		if (ImGui::InputInt("乱数の種", &randomSeed)) {
			settings.randomSeed = static_cast<uint32_t>((std::max)(randomSeed, 0));
			rebuild = true;
		}
		rebuild |= ImGui::SliderFloat("種点の最小間隔", &settings.minSeedSpacing, 0.0f, 1.0f);
		if (ImGui::Button("割り直す")) rebuild = true;
		if (rebuild) Rebuild();

		ImGui::Text("破片 %zu 個 / 頂点 %zu / %.1f ms", fragmentCount, vertexCount, buildMs);
		if (capFailures > 0) {
			ImGui::TextColored({ 1.0f, 0.4f, 0.3f, 1.0f }, "閉じなかった切り口: %u", capFailures);
		} else {
			ImGui::Text("閉じなかった切り口: 0");
		}

		ImGui::SeparatorText("動き（確認用）");
		ImGui::SliderFloat("広がり", &explode, 0.0f, 2.0f);
		ImGui::SliderFloat("回転", &spin, 0.0f, 2.0f);
		ImGui::SliderFloat("ディゾルブ（右）", &dissolve, 0.0f, 1.0f);
		ImGui::SliderFloat("大きさ", &rockScale, 0.5f, 5.0f);
		ImGui::Checkbox("自動で回す", &autoRotate);

		ImGui::SeparatorText("見た目");
		renderer.OnImGui();
		ImGui::End();
	}
#endif
};

DemoScene::DemoScene() {
	std::random_device rd;
	randomEngine_.seed(rd());
}

DemoScene::~DemoScene() {
}

namespace {
	// JSON の自己テスト：書いて読み戻す "version" の値
	constexpr int kJsonTestVersion = 2;
	// JSON の自己テスト：書き出す "scale" の値
	constexpr double kJsonTestScale = 1.5;
	// JSON の自己テスト：配列の 2 番目に入れて読み戻す値
	constexpr int kJsonTestListSecond = 2;
	// JSON の自己テスト：配列の 3 番目に入れる値
	constexpr int kJsonTestListThird = 3;
	// JSON の自己テスト：整形出力の字下げ [スペースの数]
	constexpr int kJsonPrettyIndentWidth = 2;
	// デモのカメラの初めの高さ [m]
	constexpr float kDemoCameraPosY = 15.0f;
	// デモのカメラを原点から手前（−Z）へ離す距離 [m]
	constexpr float kDemoCameraBackZ = 20.0f;
	// デモのカメラの俯角 [rad]
	constexpr float kDemoCameraPitch = 0.5f;
	// SneakWalk の環境マップの映り込みの強さ
	constexpr float kSneakWalkEnvCoefficient = 0.5f;
	// DebugDraw 確認：グリッドの大きさ [m]
	constexpr float kDebugGridSize = 20.0f;
	// DebugDraw 確認：グリッドの灰色の明るさ
	constexpr float kDebugGridGray = 0.3f;
	// DebugDraw 確認：原点の十字の大きさ [m]
	constexpr float kOriginCrossSize = 0.5f;
	// DebugDraw 確認：球の中心 X [m]
	constexpr int kDebugSpherePosX = 3;
	// DebugDraw 確認：球の色 G（黄色）
	constexpr float kDebugSphereColorG = 0.9f;
	// DebugDraw 確認：球の色 B（黄色）
	constexpr float kDebugSphereColorB = 0.2f;
	// DebugDraw 確認：AABB の最小 X の大きさ（−4） [m]
	constexpr int kDebugAabbMinX = 4;
	// DebugDraw 確認：AABB の最大 X の大きさ（−2） [m]
	constexpr int kDebugAabbMaxX = 2;
	// DebugDraw 確認：AABB の最大 Y [m]
	constexpr int kDebugAabbMaxY = 2;
	// DebugDraw 確認：AABB の色の R と B（緑）
	constexpr float kDebugAabbColorRB = 0.4f;
	// DebugDraw 確認：OBB の中心 Z の大きさ（−4） [m]
	constexpr int kDebugObbPosZ = 4;
	// DebugDraw 確認：OBB の Y の半分の大きさ [m]
	constexpr float kDebugObbHalfSizeY = 0.5f;
	// DebugDraw 確認：OBB の色 R（水色）
	constexpr float kDebugObbColorR = 0.4f;
	// DebugDraw 確認：OBB の色 G（水色）
	constexpr float kDebugObbColorG = 0.8f;
	// DebugDraw 確認：スプラインの色 B（薄い黄色）
	constexpr float kDebugSplineColorB = 0.4f;
	// DebugDraw 確認：スプラインを分ける数
	constexpr int kDebugSplineSegments = 24;
	// DebugDraw 確認：制御点の十字の大きさ [m]
	constexpr float kControlPointCrossSize = 0.25f;
	// DebugDraw 確認：制御点の十字の色の G と B（薄い赤）
	constexpr float kControlPointCrossColorGB = 0.6f;
	// DebugDraw 確認：レイの始点 Y [m]
	constexpr int kDebugRayOriginY = 5;
	// DebugDraw 確認：レイの始点 Z の大きさ（−8） [m]
	constexpr int kDebugRayOriginZ = 8;
	// DebugDraw 確認：レイの長さ [m]
	constexpr float kDebugRayLength = 5.0f;
	// Y ボタンで試すコントローラーの振動の強さ（0〜65535）
	constexpr int kVibrationStrength = 32000;
	// 3 キーの 3D サウンドを鳴らす位置 X の大きさ（−5） [m]
	constexpr float kSound3DPosX = 5.0f;
	// 3 キーの 3D サウンドを鳴らす位置 Y [m]
	constexpr float kSound3DPosY = 5.0f;
	// 3 キーの 3D サウンドを鳴らす位置 Z [m]
	constexpr float kSound3DPosZ = 5.0f;
	// 円周上を回る音源の高さ [m]
	constexpr float kOrbitSoundHeight = 3.0f;
	// 円周上を回る音源の聴こえる距離のスケール
	constexpr float kOrbitSoundDistanceScale = 30.0f;
	// HitEffect の板の初めの横幅の倍率（縦長にする）
	constexpr float kHitPlaneInitialWidth = 0.05f;
	// HitEffect の板の寿命 [秒]
	constexpr float kHitPlaneLifeTime = 0.6f;
	// HitEffect の板が寿命の間に大きくなる量（1＋この値＝3 倍）
	constexpr float kHitPlaneScaleGrowth = 2.0f;
	/// <summary>
	/// JSONライブラリの動作確認用セルフテスト（パース・書き出し・往復・エラー位置）。
	/// 結果はOutputDebugStringに出力される（VS出力ウィンドウで確認）
	/// </summary>
	void RunJsonSelfTest() {
		Log("===== Json Self Test =====\n");

		// 1) コメント付きJSONをパース
		const char* source = R"(
{
    // これは行コメント
    "name": "Pit",      // プレイヤー名
    "hp": 100,
    "ratio": 0.75,
    /* これは
       ブロックコメント */
    "alive": true,
    "tags": ["fast", "ranged", "human"],
    "binds": {
        "fire": "RT",
        "dodge": "A",
        "melee_strong": "Y"
    },
    "escaped": "tab=\there\nand newline",
    "unicode": "あ"
}
)";
		auto parsed = JsonParser::Parse(source);
		if (!parsed.success) {
			std::ostringstream oss;
			oss << "[FAIL] Parse error at line " << parsed.errorLine
				<< " col " << parsed.errorColumn << ": " << parsed.errorMessage << "\n";
			Log(oss.str());
			return;
		}
		Log("[OK] Parsed comment-aware JSON\n");

		// 2) 値を取得して検証
		const JsonValue& root = parsed.value;
		{
			std::ostringstream oss;
			oss << "  name        = " << root["name"].AsString() << "\n"
				<< "  hp          = " << root["hp"].AsInt() << "\n"
				<< "  ratio       = " << root["ratio"].AsDouble() << "\n"
				<< "  alive       = " << (root["alive"].AsBool() ? "true" : "false") << "\n"
				<< "  tags.size   = " << root["tags"].Size() << "\n"
				<< "  tags[0]     = " << root["tags"][0].AsString() << "\n"
				<< "  binds.fire  = " << root["binds"]["fire"].AsString() << "\n"
				<< "  missing     = " << root["this_key_does_not_exist"].AsString("(default)") << "\n";
			Log(oss.str());
		}

		// 3) 書き込み（コメントは出力されない=純JSON準拠）
		JsonValue out = JsonValue::MakeObject();
		out["title"] = "CG2_0_1";
		out["version"] = kJsonTestVersion;
		out["scale"] = kJsonTestScale;
		out["enabled"] = false;

		JsonValue list = JsonValue::MakeArray();
		list.Push(JsonValue(1));
		list.Push(JsonValue(kJsonTestListSecond));
		list.Push(JsonValue(kJsonTestListThird));
		out["list"] = std::move(list);

		out["nested"]["inner"]["deep"] = "via operator[] chain";

		std::string pretty = JsonWriter::Write(out, { true, kJsonPrettyIndentWidth });
		Log("[OK] Pretty output:\n");
		Log(pretty);
		Log("\n");

		std::string compact = JsonWriter::Write(out, { false, 0 });
		Log("[OK] Compact output:\n");
		Log(compact);
		Log("\n");

		// 4) ラウンドトリップ: 書き出し→再パース→値一致確認
		auto reparsed = JsonParser::Parse(pretty);
		if (!reparsed.success) {
			Log("[FAIL] Roundtrip parse failed\n");
		} else {
			bool ok = reparsed.value["title"].AsString() == "CG2_0_1"
				&& reparsed.value["version"].AsInt() == kJsonTestVersion
				&& reparsed.value["list"][1].AsInt() == kJsonTestListSecond
				&& reparsed.value["nested"]["inner"]["deep"].AsString() == "via operator[] chain";
			Log(ok ? "[OK] Roundtrip values match\n" : "[FAIL] Roundtrip mismatch\n");
		}

		// 5) エラー位置の確認（わざと壊れたJSONを渡す）
		auto bad = JsonParser::Parse("{\n  \"a\": 1,\n  \"b\": ,\n}");
		if (!bad.success) {
			std::ostringstream oss;
			oss << "[OK] Detected malformed JSON at line " << bad.errorLine
				<< " col " << bad.errorColumn << " (" << bad.errorMessage << ")\n";
			Log(oss.str());
		} else {
			Log("[FAIL] Malformed JSON was incorrectly accepted\n");
		}

		Log("===== Json Self Test End =====\n");
	}
}

void DemoScene::Initialize() {
	RunJsonSelfTest();

	Game::GetPostEffect()->ResetEffects();

	// デバッグカメラは GameScene 側で必要時に生成（GetDebugCamera で生成）

	// カメラの生成
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, kDemoCameraPosY, -kDemoCameraBackZ });
	camera_->SetRotate({ kDemoCameraPitch, 0.0f, 0.0f });
	GetObject3DManager()->SetDefaultCamera(camera_.get());
	GetSkyboxManager()->SetDefaultCamera(camera_.get());

	// Skybox生成 DirectStrageのステージングバッファがデフォルトの32MBから
	// 256MBに増やしたけどもっとでかいやつはまたバッファを増やす必要あり
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(GetSkyboxManager(), GetDxCore(), "Resources/Cubemaps/passendorf_snow_8k.dds");

	// ここで環境マップをモデルにセット
	GetObject3DManager()->SetEnvironmentTexture("Resources/Cubemaps/passendorf_snow_8k.dds");

	// 岩のボロノイ分割の確認
	fracturePreview_ = std::make_unique<FracturePreview>();
	fracturePreview_->renderer.Initialize(GetDxCore(), GetSRVManager(), GetObject3DManager(), FracturePreview::kRockCount);
	fracturePreview_->Rebuild();

	// GPU Particle + EffectManager は Game が共通管理する（GameScene::UpdateGlobalEffects 経由で使用）

	// サウンドのロード
	SoundManager::GetInstance()->LoadFile("fanfare", "Resources/Sounds/fanfare.wav");

	auto sneakWalkModel = std::make_unique<Object3DInstance>();
	sneakWalkModel->Initialize(
		GetObject3DManager(),
		GetDxCore(),
		"Resources/Models/Animated/SneakWalk",
		"sneakWalk.mesh",
		"sneakWalk"
	);
	sneakWalkModel->SetScale({ 1.0f, 1.0f, 1.0f });
	sneakWalkModel->SetUseEnvironmentMap(true);
	sneakWalkModel->SetEnvironmentCoefficient(kSneakWalkEnvCoefficient);
	AdoptDynamicObject(std::move(sneakWalkModel));

	// sneakWalkのモデルとアニメーションの読み込み
	sneakWalk = std::make_unique<AnimatedModelInstance>();
	sneakWalk->Initialize(
		ModelManager::GetInstance()->GetModelCore(),
		"Resources/Models/Animated/SneakWalk",
		"sneakWalk.mesh"
	);

	// sneakWalkのインスタンスを作成
	sneakWalkInstance_= std::make_unique<AnimatedObject3DInstance>();
	sneakWalkInstance_->Initialize(
		GetObject3DManager(),
		GetSkinningComputeManager(),
		GetDxCore(),
		GetSRVManager(),
		sneakWalk.get(),
		"sneakWalk"
	);
	sneakWalkInstance_->SetScale({ 1.0f, 1.0f, 1.0f });
	sneakWalkInstance_->SetUseEnvironmentMap(true);
	sneakWalkInstance_->SetEnvironmentCoefficient(kSneakWalkEnvCoefficient);

	// Camera/RenderTextureはGameScene::GetCamera経由とGame::Initialize側で中央化済み
	// ImGui への GPUParticleManager 通知も Game::Initialize で行う

	// 前回保存したシーン配置があれば自動ロード（ファイル無しは何もしない）
	const std::string kAutoLoadPath = "Resources/Json/Scenes/demo.json";
	if (std::filesystem::exists(kAutoLoadPath)) {
		LoadSceneFromJson(kAutoLoadPath);
	}
}

void DemoScene::Finalize() {
	// EffectManager / GPUParticle の終了は Game::Finalize 側で実施
	ClearAllDynamicEntities();
	hitEffectPlanes_.clear();
	if (fracturePreview_) {
		fracturePreview_->renderer.Finalize();
		fracturePreview_.reset();
	}
}

void DemoScene::Update() {

	// DebugDraw 動作確認: グリッド + 球 + AABB + OBB + Cross + Spline
	{
		// Y=0 グリッド
		DebugDraw::Grid({ 0,0,0 }, kDebugGridSize, 1.0f, { kDebugGridGray, kDebugGridGray, kDebugGridGray, 1.0f });

		// 原点の十字（X=赤, Y=緑, Z=青 はやらず単色の小さい目印）
		DebugDraw::Cross({ 0,0,0 }, kOriginCrossSize, { 1.0f, 1.0f, 1.0f, 1.0f });

		// 黄色い球（半径1、segments=16）
		DebugDraw::Sphere({ kDebugSpherePosX, 1, 0 }, 1.0f, { 1.0f, kDebugSphereColorG, kDebugSphereColorB, 1.0f });

		// AABB
		DebugDraw::AABB({ -kDebugAabbMinX, 0, -1 }, { -kDebugAabbMaxX, kDebugAabbMaxY, 1 }, { kDebugAabbColorRB, 1.0f, kDebugAabbColorRB, 1.0f });

		// OBB（XY平面で45度回転、Z軸はそのまま）
		const float c45 = 0.70710678f;
		Vector3 axes[3] = {
			{ c45, c45, 0.0f },
			{-c45, c45, 0.0f },
			{ 0.0f, 0.0f, 1.0f },
		};
		DebugDraw::OBB({ 0, 1, -kDebugObbPosZ }, axes, { 1.0f, kDebugObbHalfSizeY, 1.0f }, { kDebugObbColorR, kDebugObbColorG, 1.0f, 1.0f });

		// Catmull-Rom スプライン（PlayerRailを想定したサンプル制御点）
		static const std::vector<Vector3> kRailCps = {
			{-6, 0,  5},
			{-2, 2,  3},
			{ 2, 1, -2},
			{ 5, 3, -4},
			{ 7, 0, -8},
		};
		DebugDraw::CatmullRomSpline(kRailCps, { 1.0f, 1.0f, kDebugSplineColorB, 1.0f }, kDebugSplineSegments);
		// 制御点を小さなクロスで可視化
		for (const auto& p : kRailCps) {
			DebugDraw::Cross(p, kControlPointCrossSize, { 1.0f, kControlPointCrossColorGB, kControlPointCrossColorGB, 1.0f });
		}

		// Ray（カメラから前方）
		DebugDraw::Ray({ 0, kDebugRayOriginY, -kDebugRayOriginZ }, { 0, 0, 1 }, kDebugRayLength, { 1.0f, 1.0f, 0.0f, 1.0f });
	}

	// すべての動的スプラインを DebugDraw キューに積む（Visible(Debug) が ON のもののみ描画される）
	for (const auto& sp : GetDynamicSplineList()) {
		if (sp) sp->DrawDebug();
	}

	// アクション入力のログ出力（ImGuiのLogウィンドウで確認）
	if (auto* actionMap = GetInput()->GetActionMap()) {
		for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
			if (actionMap->IsTriggered(i)) {
				LogBuffer::GetInstance()->Add(
					std::string("Action TRIGGER: ") + std::string(GetActionName(static_cast<Action>(i))),
					LogBuffer::Level::Info);
			}
			if (actionMap->IsReleased(i)) {
				LogBuffer::GetInstance()->Add(
					std::string("Action RELEASE: ") + std::string(GetActionName(static_cast<Action>(i))),
					LogBuffer::Level::Info);
			}
		}
	}

	// カメラプレビューとQRコード読み取り（GameSceneが自動で更新・描画する）
	UseCameraCapture(true);
	UseQRCodeReader(true);

	// --- キーボード入力 ---
	if (GetInput()->GetKeyboard()->TriggerKey(DIK_SPACE)) {
		// スペースキーを押した瞬間
	}

	if (GetInput()->GetKeyboard()->PushKey(DIK_W)) {
		// Wキーを押している間
	}

	// マウス入力
	MouseInput* mouse = GetInput()->GetMouse();

	if (mouse->IsButtonTriggered(MouseInput::Button::Left)) {
		// 左クリックした瞬間
	}

	// --- コントローラー入力 ---
	ControllerInput* controller = GetInput()->GetController();

	if (controller->IsConnected()) {
		if (controller->IsButtonTriggered(XINPUT_GAMEPAD_A)) {
			// Aボタンを押した瞬間
		}

		const auto& leftStick = controller->GetLeftStick();
		float moveX = leftStick.x * leftStick.magnitude;
		float moveY = leftStick.y * leftStick.magnitude;

		if (controller->IsButtonTriggered(XINPUT_GAMEPAD_Y)) {
			controller->SetVibration(kVibrationStrength, kVibrationStrength);
		}
	}

	// キーボードの入力処理
	if (GetInput()->GetKeyboard()->TriggerKey(DIK_0)) {
		Debug::Log("trigger [0]");
	}

	if (GetInput()->GetKeyboard()->TriggerKey(DIK_1)) {
		SoundManager::GetInstance()->Play2DSound("fanfare");
		Debug::Log("trigger [1] - 2D Sound Play");
	}

	if (GetInput()->GetKeyboard()->TriggerKey(DIK_2)) {
		SoundManager::GetInstance()->Stop2DSound("fanfare");
		Debug::Log("trigger [2] - 2D Sound Stop");
	}

	// 3キー: AnimatedCubeの右側(5, 2, 0)で3Dサウンドを再生
    // → 右スピーカーからより大きく聴こえるはず
	if (GetInput()->GetKeyboard()->TriggerKey(DIK_3)) {
		if (sound3DHandle_ != 0) {
			SoundManager::GetInstance()->Stop3DSound(sound3DHandle_);
		}
		sound3DHandle_ = SoundManager::GetInstance()->Play3DSound(
			"fanfare", { -kSound3DPosX, kSound3DPosY, kSound3DPosZ });
		Debug::Log("trigger [3] - 3D Sound Play at (5, 2, 0)");
	}

	// 4キー: 3Dサウンドを停止
	if (GetInput()->GetKeyboard()->TriggerKey(DIK_4)) {
		SoundManager::GetInstance()->Stop3DSound(sound3DHandle_);
		sound3DHandle_ = 0;
		Debug::Log("trigger [4] - 3D Sound Stop");
	}

	// 5キー: 音源が円周上を回り続けるサウンドをON/OFF
	// → 左右の定位変化 + ドップラー効果による音程変化が聴こえるはず
	if (GetInput()->GetKeyboard()->TriggerKey(DIK_5)) {
		sound3DOrbitActive_ = !sound3DOrbitActive_;
		if (!sound3DOrbitActive_ && sound3DOrbitHandle_ != 0) {
			SoundManager::GetInstance()->Stop3DSound(sound3DOrbitHandle_);
			sound3DOrbitHandle_ = 0;
		}
		Debug::Log(sound3DOrbitActive_ ? "trigger [5] - Orbit ON" : "trigger [5] - Orbit OFF");
	}

	// 音源が半径8で円周上を回る（ドップラー・定位の確認用）
	if (sound3DOrbitActive_) {
		sound3DOrbitTime_ += GetScaledDeltaTime();
		const float orbitRadius = 8.0f;
		const float orbitSpeed = 1.5f; // ラジアン/秒
		float angle = sound3DOrbitTime_ * orbitSpeed;

		Vector3 orbitPos = {
			orbitRadius * std::cosf(angle),
			kOrbitSoundHeight,
			orbitRadius * std::sinf(angle)
		};
		// 接線方向の速度（ドップラー計算に使う）
		Vector3 orbitVel = {
			-orbitRadius * orbitSpeed * std::sinf(angle),
			0.0f,
			 orbitRadius * orbitSpeed * std::cosf(angle)
		};

		if (sound3DOrbitHandle_ == 0) {
			// まだ再生していなければ開始
			sound3DOrbitHandle_ = SoundManager::GetInstance()->Play3DSound(
				"fanfare", orbitPos, orbitVel, kOrbitSoundDistanceScale);
		} else {
			// 毎フレーム位置を更新
			SoundManager::GetInstance()->UpdateEmitter(sound3DOrbitHandle_, orbitPos, orbitVel);
		}
	}

	// HキーでHitEffect発生
	if (GetInput()->GetKeyboard()->TriggerKey(DIK_H)) {
		// --- Plane版 HitEffect（8枚の星型） ---
		const int planeCount = 8;
		const Vector3 hitPos = { 0.0f, 5.0f, 0.0f };

		for (int i = 0; i < planeCount; ++i) {
			HitEffectPlane p;

			// Plane生成
			p.mesh = std::make_unique<PrimitiveMesh>();
			p.mesh->Initialize(PrimitiveGenerator::CreatePlane(1.0f, 1.0f));
			p.mesh->SetTexture("Resources/Textures/circle.dds");
			p.mesh->SetBlendMode(PrimitivePipeline::kBlendModeAdd);

			// 縦長スケール（初期値）
			p.initialScale = { kHitPlaneInitialWidth, 1.0f, 1.0f };
			p.mesh->GetTransform().scale = p.initialScale;

			// 位置
			p.mesh->GetTransform().translate = hitPos;

			// Z軸回転（-π〜+π のランダム）
			std::uniform_real_distribution<float> distAngle(
				-std::numbers::pi_v<float>,
				std::numbers::pi_v<float>);
			float angle = distAngle(randomEngine_);
			p.mesh->GetTransform().rotate = { 0.0f, 0.0f, angle };

			// 寿命
			p.lifeTime = kHitPlaneLifeTime;
			p.currentTime = 0.0f;

			hitEffectPlanes_.push_back(std::move(p));
		}
	}

	// --- HitEffectPlanes の更新 ---
	const float deltaTime = GetScaledDeltaTime();

	for (auto it = hitEffectPlanes_.begin(); it != hitEffectPlanes_.end();) {
		it->currentTime += deltaTime;

		if (it->currentTime >= it->lifeTime) {
			// 寿命尽きた → 削除
			it = hitEffectPlanes_.erase(it);
			continue;
		}

		// 進行率 t (0.0 ~ 1.0)
		float t = it->currentTime / it->lifeTime;

		// スケール：初期サイズ → 3倍に拡大（EaseOutCubic）
		float scaleFactor = 1.0f + kHitPlaneScaleGrowth * Easing::Apply(Easing::Type::EaseOutCubic, t);
		it->mesh->GetTransform().scale = {
			it->initialScale.x * scaleFactor,
			it->initialScale.y * scaleFactor,
			it->initialScale.z
		};

		// アルファ：1.0 → 0.0（フェードアウト、EaseOutCubic）
		float alpha = 1.0f - Easing::Apply(Easing::Type::EaseOutCubic, t);
		it->mesh->SetColor({ 1.0f, 1.0f, 1.0f, alpha });

		// 毎フレーム更新
		it->mesh->Update(camera_.get());

		++it;
	}

	if (GetInput()->GetKeyboard()->TriggerKey(DIK_P)) {
		GetDxCore()->SetUseFixedFrameRate(false);
	}

	// Fキーで場のON/OFF切り替え
	if (GetInput()->GetKeyboard()->TriggerKey(DIK_F)) {
		bool current = ParticleManager::GetInstance()->IsAccelerationFieldEnabled();
		ParticleManager::GetInstance()->SetAccelerationFieldEnabled(!current);
	}

	// SoundManager の UpdateListener / Update は Framework が毎フレーム呼ぶ（ここでは呼ばない）

	// カメラの更新は必ずオブジェクトの更新前にやる
	camera_->Update();
	// useDebugCamera が ON ならここで camera_ の行列がデバッグカメラの視点に上書きされる
	UpdateDebugCameraIfActive();

	// 3Dオブジェクトの更新
	for (const auto& obj : GetDynamicObjects()) {
		obj->Update();
	}

	// SceneEditorWindow からドロップで追加された動的エンティティ
	UpdateDynamicPrimitives();
	for (auto& a : GetDynamicAnimated()) {
		a->Update(GetScaledDeltaTime());
	}
	for (auto& s : GetDynamicSprites()) {
		s->Update();
	}

	// アニメーション付きオブジェクトの更新
	if (sneakWalkInstance_) {
		sneakWalkInstance_->Update(GetScaledDeltaTime());
	}

	// Skybox更新を追加
	skybox_->Update();

	// パーティクル更新処理
	ParticleManager::GetInstance()->Update(GetScaledDeltaTime());

	// 全シーン共通の EffectManager + GPUParticle を更新
	UpdateGlobalEffects(camera_.get(), GetScaledDeltaTime());

	// 岩のボロノイ分割の確認
	if (fracturePreview_) {
		if (fracturePreview_->autoRotate) {
			fracturePreview_->yaw += FracturePreview::kAutoRotateSpeed * GetScaledDeltaTime();
		}
#ifdef USE_IMGUI
		fracturePreview_->OnImGui();
#endif
	}

	// debug camera の更新は GameScene::UpdateDebugCameraIfActive() で行う
}

void DemoScene::Draw() {

	// DSVを切り替えてSkybox描画
	// 必ず最初に背景から描画していくこと
	auto commandList = GetDxCore()->GetCommandList();
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();

	// Skybox描画は深度を書き込まないのでRead-Only DSVで問題なし
	auto readOnlyDsv = GetDxCore()->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);

	GetSkyboxManager()->DrawSetting();
	skybox_->Draw(GetDxCore());

	// Object3D以降は通常DSVに切り替え（深度書き込みを有効化）
	auto normalDsv = GetDxCore()->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);

	// 3Dオブジェクトの共通描画設定
	GetObject3DManager()->DrawSetting();
	LightManager::GetInstance()->BindLights(GetDxCore()->GetCommandList());

	// 3Dオブジェクト描画
	for (const auto& obj : GetDynamicObjects()) {
		obj->Draw(GetDxCore());
	}
	// 動的アニメーションモデル
	for (auto& a : GetDynamicAnimated()) {
		a->Draw(GetDxCore());
	}

	// 動的プリミティブ
	DrawDynamicPrimitives();

	// アニメーション付きオブジェクトの描画
	if (sneakWalkInstance_) {
		sneakWalkInstance_->Draw(GetDxCore());
	}

	// HitEffectPlanes の描画
	for (auto& p : hitEffectPlanes_) {
		p.mesh->Draw();
	}

	// 岩のボロノイ分割の確認（Object3D の描画が全部終わってから。ルートシグネチャを張り替えるため）
	if (fracturePreview_) {
		fracturePreview_->Draw(*camera_);
	}

	// パーティクル描画
	ParticleManager::GetInstance()->Draw();

	// 全シーン共通の GPUParticle + Effect Editor 描画
	DrawGlobalEffects();

#ifdef USE_IMGUI
	// Skeletonのデバッグ描画（全モデル描画完了後にまとめて行う）
	if (sneakWalkInstance_) {
		sneakWalkInstance_->DrawSkeletonDebug(GetDxCore());
	}

	// SceneEditorWindow からドロップで追加された動的アニメーションモデルも対象
	for (auto& a : GetDynamicAnimated()) {
		a->DrawSkeletonDebug(GetDxCore());
	}
#endif

	// デバッグカメラ有効時はシーンカメラ位置に視錐台ギズモを描画
	DrawSceneCameraGizmo();

	// 線描画（Skeletonデバッグ用 など）
	LineRenderer::GetInstance()->SetCamera(camera_.get());
	LineRenderer::GetInstance()->Draw();

	// スプライト描画
	GetSpriteManager()->DrawSetting();
	// 動的スプライト
	for (auto& s : GetDynamicSprites()) {
		s->Draw();
	}

	// Effect Editor のプレビューは Game::Draw がシーンに関係なく描く
}

// =============================================================
// 動的オブジェクトの追加・削除（SceneEditorWindow 経由）
// =============================================================
// 動的エンティティの Add/Remove/Instantiate は GameScene に集約済み。
// ここでは DemoScene 固有の名前付きメンバ（sneakWalkInstance_）も
// IsDynamicObject の対象に含めるための薄い override のみ残す。

#ifdef USE_IMGUI
bool DemoScene::IsDynamicObject(IImGuiEditable* editable) const {
	if (GameScene::IsDynamicObject(editable)) return true;
	if (sneakWalkInstance_ && static_cast<IImGuiEditable*>(sneakWalkInstance_.get()) == editable) return true;
	return false;
}
#endif

// =====================================================================
// シーン保存 / 読込
//   本体は GameScene に集約済み（JSON の形は SceneSerializer が持つ）。
//   DemoScene は固有の除外・後処理が無いので、シーン名だけ返せばよい。
// =====================================================================

std::string DemoScene::GetSceneJsonName() const {
	return "DemoScene";
}