#include "MonochromeEffect.h"
#include "DirectXCore.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <cassert>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <iterator>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 設定の JSON を書き出す時の 1 段の字下げ [スペース数]
	constexpr int kJsonIndentWidth = 2;
	// 白黒の量がこれ以下なら通常の色とみなす（再生の終わりに自動で無効へ戻す判定）
	constexpr float kColorEpsilon = 1e-3f;
	// ImGui で並びの名前を編集する入力欄の大きさ [byte]
	constexpr size_t kNameBufferSize = 64;

	// ----- 段階の見た目の初期値 -----
	// 並び：monoAmount, contrast, midpoint, threshold, softness, noiseAmount, noiseScale, noiseSpeed,
	//       edgeStrength, edgeThreshold, edgeWidth, invert
	// 1/6. 通常の色
	constexpr MonochromeParams kColorPreset{
		0.0f, 1.0f, 0.5f, 0.5f, 1.0f, 0.0f, 6.0f, 0.5f, 0.0f, 0.02f, 1.0f, 0.0f };
	// 2. 白黒の 2 値（墨絵）：明暗を強めてくっきり分け、境目をノイズでにじませる
	constexpr MonochromeParams kInkPreset{
		1.0f, 1.6f, 0.45f, 0.45f, 0.0f, 0.12f, 4.0f, 0.3f, 0.6f, 0.02f, 1.5f, 0.0f };
	// 3. 明暗を強調したグレースケール
	constexpr MonochromeParams kStrongGrayPreset{
		1.0f, 2.2f, 0.5f, 0.5f, 1.0f, 0.0f, 6.0f, 0.5f, 0.3f, 0.02f, 1.0f, 0.0f };
	// 4. 白が多い 2 値：しきい値を下げてほぼ白にし、輪郭の黒い線だけ残す
	constexpr MonochromeParams kWhiteInkPreset{
		1.0f, 1.2f, 0.5f, 0.15f, 0.0f, 0.08f, 5.0f, 0.4f, 1.0f, 0.015f, 1.5f, 0.0f };
	// 5. 4 の反転：ほぼ黒に、白い線や光だけ残す
	constexpr MonochromeParams kInvertInkPreset{
		1.0f, 1.2f, 0.5f, 0.15f, 0.0f, 0.08f, 5.0f, 0.4f, 1.0f, 0.015f, 1.5f, 1.0f };

	// ImGui の段階のボタン・コンボに出す名前（MonochromeStage の順）
	constexpr const char* kStageNames[] = { "Color", "Ink", "StrongGray", "WhiteInk", "InvertInk" };
	static_assert(std::size(kStageNames) == static_cast<size_t>(MonochromeStage::Count));

	// ----- 見本の並び（JSON が無いとき）：溜め → 2 値 → グレー → 白 → 反転 → 色に戻る -----
	constexpr const char* kSampleSequenceName = "Sample";
	constexpr float kSampleChargeHold = 0.8f;     // [s] 通常の色のまま溜める
	constexpr float kSampleStageHold = 0.4f;      // [s] 白黒の各段階にとどまる時間
	constexpr float kSampleGrayBlend = 0.15f;     // [s] 2 値 → グレーへ移る時間
	constexpr float kSampleReturnBlend = 0.3f;    // [s] 色に戻る時間
	constexpr float kSampleReturnHold = 0.0f;     // [s] 色に戻った後（ここで終わる）

	// 新しいコマの既定のとどまる時間 [s]
	constexpr float kNewKeyHold = 0.5f;

	// 見た目の値と JSON のキーの組（JSON の読み書きと補間で使う）
	struct FieldEntry {
		const char* key;
		float MonochromeParams::* member;
	};
	constexpr FieldEntry kFields[] = {
		{ "monoAmount", &MonochromeParams::monoAmount },
		{ "contrast", &MonochromeParams::contrast },
		{ "midpoint", &MonochromeParams::midpoint },
		{ "threshold", &MonochromeParams::threshold },
		{ "softness", &MonochromeParams::softness },
		{ "noiseAmount", &MonochromeParams::noiseAmount },
		{ "noiseScale", &MonochromeParams::noiseScale },
		{ "noiseSpeed", &MonochromeParams::noiseSpeed },
		{ "edgeStrength", &MonochromeParams::edgeStrength },
		{ "edgeThreshold", &MonochromeParams::edgeThreshold },
		{ "edgeWidth", &MonochromeParams::edgeWidth },
		{ "invert", &MonochromeParams::invert },
	};

	// JSON のキー（並び）
	constexpr const char* kSequencesKey = "sequences";
	constexpr const char* kNameKey = "name";
	constexpr const char* kKeysKey = "keys";
	constexpr const char* kBlendTimeKey = "blendTime";
	constexpr const char* kHoldTimeKey = "holdTime";

	/// <summary>見た目を値ごとに線形補間する</summary>
	/// <param name="a">t = 0 の見た目</param>
	/// <param name="b">t = 1 の見た目</param>
	/// <param name="t">補間の割合（0〜1）</param>
	/// <returns>補間した見た目</returns>
	MonochromeParams LerpParams(const MonochromeParams& a, const MonochromeParams& b, float t) {
		MonochromeParams result = a;
		for (const FieldEntry& f : kFields) {
			result.*f.member = a.*f.member + (b.*f.member - a.*f.member) * t;
		}
		return result;
	}

	/// <summary>0〜1 を、始めと終わりがゆっくりな曲線にする</summary>
	/// <param name="t">割合（0〜1）</param>
	/// <returns>なめらかにした割合</returns>
	float SmoothStep01(float t) {
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	/// <summary>コマ 1 つを JSON にする</summary>
	/// <param name="key">コマ</param>
	/// <returns>JSON のオブジェクト</returns>
	JsonValue KeyToJson(const MonochromeKey& key) {
		JsonValue obj = JsonValue::MakeObject();
		obj[kBlendTimeKey] = static_cast<double>(key.blendTime);
		obj[kHoldTimeKey] = static_cast<double>(key.holdTime);
		for (const FieldEntry& f : kFields) {
			obj[f.key] = static_cast<double>(key.params.*f.member);
		}
		return obj;
	}

	/// <summary>JSON からコマ 1 つを読む（無い値は通常の色の初期値）</summary>
	/// <param name="obj">JSON のオブジェクト</param>
	/// <returns>コマ</returns>
	MonochromeKey KeyFromJson(const JsonValue& obj) {
		MonochromeKey key{};
		key.params = kColorPreset;
		key.blendTime = static_cast<float>(obj[kBlendTimeKey].AsDouble(0.0));
		key.holdTime = static_cast<float>(obj[kHoldTimeKey].AsDouble(0.0));
		for (const FieldEntry& f : kFields) {
			key.params.*f.member = static_cast<float>(obj[f.key].AsDouble(key.params.*f.member));
		}
		return key;
	}
}

void MonochromeEffect::InitializeDepth(
	DirectXCore* dxCore,
	ID3D12RootSignature* outlineRootSignature,
	const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc)
{
	IDxcBlob* psBlob = dxCore->LoadShaderBlob(
		L"Resources/Shaders/PostEffect/Filters/Monochrome.PS.hlsl",
		L"ps_6_0"
	);
	assert(psBlob);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
	psoDesc.pRootSignature = outlineRootSignature;
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));

	CreateConstantBuffer(dxCore, sizeof(ParamsCB));
	ResetParams();
	UpdateConstantBuffer();
}

void MonochromeEffect::UpdateConstantBuffer()
{
	if (!constantBufferMappedPtr_) return;

	ParamsCB cb{};
	cb.params = params_;
	cb.time = time_;
	cb.depthA = depthA_;
	cb.depthB = depthB_;
	// 射影行列をまだ受け取っていないと深度を距離へ戻せないので、輪郭は引かない
	if (depthB_ == 0.0f) {
		cb.params.edgeStrength = 0.0f;
	}
	memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void MonochromeEffect::ResetParams()
{
	params_ = kColorPreset;
	playing_ = false;
	autoEnabled_ = false;
	playTime_ = 0.0f;
	time_ = 0.0f;
}

void MonochromeEffect::SetProjectionMatrix(const Matrix4x4& projection)
{
	depthA_ = projection.m[2][2];
	depthB_ = projection.m[3][2];
}

void MonochromeEffect::Update(float deltaTime)
{
	deltaTime = (std::max)(deltaTime, 0.0f);
	if (!enabled_ && !playing_) return;

	time_ += deltaTime;
	if (!playing_) return;

	playTime_ += deltaTime;
	EvaluateSequence();
	if (playTime_ < GetDuration(playKeys_)) return;

	// 最後の見た目で止める。通常の色に戻って終わり、再生で有効にしていたなら無効へ戻す
	playing_ = false;
	params_ = playKeys_.back().params;
	if (autoEnabled_ && params_.monoAmount <= kColorEpsilon) {
		enabled_ = false;
		autoEnabled_ = false;
	}
}

void MonochromeEffect::EvaluateSequence()
{
	MonochromeParams from = playStart_;
	float t = playTime_;
	for (const MonochromeKey& key : playKeys_) {
		if (t < key.blendTime) {
			params_ = LerpParams(from, key.params, SmoothStep01(t / key.blendTime));
			return;
		}
		t -= key.blendTime;
		if (t < key.holdTime) {
			params_ = key.params;
			return;
		}
		t -= key.holdTime;
		from = key.params;
	}
	if (!playKeys_.empty()) {
		params_ = playKeys_.back().params;
	}
}

void MonochromeEffect::Play(const std::vector<MonochromeKey>& keys)
{
	if (keys.empty()) return;
	playKeys_ = keys;
	playStart_ = params_;
	playTime_ = 0.0f;
	playing_ = true;
	if (!enabled_) {
		enabled_ = true;
		autoEnabled_ = true;
	}
	EvaluateSequence();
}

bool MonochromeEffect::Play(const std::string& sequenceName)
{
	const MonochromeSequence* sequence = FindSequence(sequenceName);
	if (!sequence || sequence->keys.empty()) return false;
	Play(sequence->keys);
	return true;
}

void MonochromeEffect::Stop()
{
	ResetParams();
	enabled_ = false;
}

float MonochromeEffect::GetDuration(const std::vector<MonochromeKey>& keys)
{
	float duration = 0.0f;
	for (const MonochromeKey& key : keys) {
		duration += (std::max)(key.blendTime, 0.0f) + (std::max)(key.holdTime, 0.0f);
	}
	return duration;
}

MonochromeParams MonochromeEffect::GetStagePreset(MonochromeStage stage)
{
	switch (stage) {
	case MonochromeStage::Ink:        return kInkPreset;
	case MonochromeStage::StrongGray: return kStrongGrayPreset;
	case MonochromeStage::WhiteInk:   return kWhiteInkPreset;
	case MonochromeStage::InvertInk:  return kInvertInkPreset;
	case MonochromeStage::Color:
	default:                          return kColorPreset;
	}
}

MonochromeKey MonochromeEffect::MakeKey(MonochromeStage stage, float blendTime, float holdTime)
{
	MonochromeKey key{};
	key.params = GetStagePreset(stage);
	key.blendTime = (std::max)(blendTime, 0.0f);
	key.holdTime = (std::max)(holdTime, 0.0f);
	return key;
}

// ===================================================================
// 名前の付いた並び（JSON）
// ===================================================================

void MonochromeEffect::AddSampleSequence()
{
	MonochromeSequence sample;
	sample.name = kSampleSequenceName;
	sample.keys = {
		MakeKey(MonochromeStage::Color, 0.0f, kSampleChargeHold),
		MakeKey(MonochromeStage::Ink, 0.0f, kSampleStageHold),
		MakeKey(MonochromeStage::StrongGray, kSampleGrayBlend, kSampleStageHold),
		MakeKey(MonochromeStage::WhiteInk, 0.0f, kSampleStageHold),
		MakeKey(MonochromeStage::InvertInk, 0.0f, kSampleStageHold),
		MakeKey(MonochromeStage::Color, kSampleReturnBlend, kSampleReturnHold),
	};
	sequences_.push_back(std::move(sample));
}

void MonochromeEffect::LoadSequences(const std::string& path)
{
	sequencesPath_ = path;
	sequences_.clear();
	if (std::filesystem::exists(path)) {
		const auto result = JsonParser::ParseFile(path);
		// const のまま引く（const でない operator[] は無いキーを足してしまうため）
		const JsonValue& root = result.value;
		const JsonValue& list = root[kSequencesKey];
		if (result.success && list.IsArray()) {
			for (size_t i = 0; i < list.Size(); ++i) {
				const JsonValue& item = list[i];
				if (!item.IsObject()) continue;
				MonochromeSequence sequence;
				sequence.name = item[kNameKey].AsString(std::string());
				const JsonValue& keys = item[kKeysKey];
				if (keys.IsArray()) {
					for (size_t k = 0; k < keys.Size(); ++k) {
						if (keys[k].IsObject()) sequence.keys.push_back(KeyFromJson(keys[k]));
					}
				}
				sequences_.push_back(std::move(sequence));
			}
		}
	}
	if (sequences_.empty()) {
		AddSampleSequence();
	}
	editSequence_ = std::clamp(editSequence_, 0, static_cast<int>(sequences_.size()) - 1);
}

void MonochromeEffect::SaveSequences() const
{
	if (sequencesPath_.empty()) return;
	JsonValue list = JsonValue::MakeArray();
	for (const MonochromeSequence& sequence : sequences_) {
		JsonValue item = JsonValue::MakeObject();
		item[kNameKey] = sequence.name;
		JsonValue keys = JsonValue::MakeArray();
		for (const MonochromeKey& key : sequence.keys) {
			keys.AsArray().push_back(KeyToJson(key));
		}
		item[kKeysKey] = std::move(keys);
		list.AsArray().push_back(std::move(item));
	}
	JsonValue root = JsonValue::MakeObject();
	root[kSequencesKey] = std::move(list);
	JsonWriter::WriteFile(sequencesPath_, root, { true, kJsonIndentWidth });
}

const MonochromeSequence* MonochromeEffect::FindSequence(const std::string& sequenceName) const
{
	for (const MonochromeSequence& sequence : sequences_) {
		if (sequence.name == sequenceName) return &sequence;
	}
	return nullptr;
}

// ===================================================================
// ImGui
// ===================================================================

void MonochromeEffect::ShowImGui()
{
#ifdef _DEBUG
	if (playing_) {
		ImGui::Text("再生中 %.2f / %.2f s", playTime_, GetDuration(playKeys_));
	}

	// 段階のボタン：押すと再生を止めてその見た目にする
	ImGui::TextUnformatted("段階");
	for (int s = 0; s < static_cast<int>(MonochromeStage::Count); ++s) {
		if (s > 0) ImGui::SameLine();
		if (ImGui::Button(kStageNames[s])) {
			playing_ = false;
			params_ = GetStagePreset(static_cast<MonochromeStage>(s));
		}
	}
	EditParams(params_);

	ImGui::SeparatorText("並び（時間に沿って段階を切り替える）");
	ShowSequenceEditor();
#endif
}

#ifdef _DEBUG
void MonochromeEffect::EditParams(MonochromeParams& p)
{
	ImGui::SliderFloat("白黒の量", &p.monoAmount, 0.0f, 1.0f);
	ImGui::SliderFloat("コントラスト", &p.contrast, 0.0f, 5.0f);
	ImGui::SliderFloat("中心の明るさ", &p.midpoint, 0.0f, 1.0f);
	ImGui::SliderFloat("しきい値", &p.threshold, 0.0f, 1.0f);
	ImGui::SliderFloat("柔らかさ(0=2値)", &p.softness, 0.0f, 1.0f);
	ImGui::SliderFloat("ノイズの量", &p.noiseAmount, 0.0f, 0.5f);
	ImGui::SliderFloat("ノイズの細かさ", &p.noiseScale, 0.5f, 30.0f);
	ImGui::SliderFloat("ノイズの速さ", &p.noiseSpeed, 0.0f, 3.0f);
	ImGui::SliderFloat("輪郭の濃さ", &p.edgeStrength, 0.0f, 1.0f);
	ImGui::SliderFloat("輪郭のしきい値", &p.edgeThreshold, 0.001f, 0.2f, "%.4f");
	ImGui::SliderFloat("輪郭の太さ[px]", &p.edgeWidth, 0.5f, 4.0f);
	ImGui::SliderFloat("反転", &p.invert, 0.0f, 1.0f);
}

void MonochromeEffect::ShowSequenceEditor()
{
	if (sequences_.empty()) {
		AddSampleSequence();
	}
	editSequence_ = std::clamp(editSequence_, 0, static_cast<int>(sequences_.size()) - 1);

	// 並びの選択・追加・削除
	if (ImGui::BeginCombo("並び", sequences_[editSequence_].name.c_str())) {
		for (int i = 0; i < static_cast<int>(sequences_.size()); ++i) {
			ImGui::PushID(i);
			if (ImGui::Selectable(sequences_[i].name.c_str(), i == editSequence_)) editSequence_ = i;
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("新規")) {
		MonochromeSequence sequence;
		sequence.name = "New" + std::to_string(sequences_.size());
		sequence.keys.push_back(MakeKey(MonochromeStage::Color, 0.0f, kNewKeyHold));
		sequences_.push_back(std::move(sequence));
		editSequence_ = static_cast<int>(sequences_.size()) - 1;
	}
	ImGui::SameLine();
	if (ImGui::Button("削除") && sequences_.size() > 1) {
		sequences_.erase(sequences_.begin() + editSequence_);
		editSequence_ = (std::min)(editSequence_, static_cast<int>(sequences_.size()) - 1);
	}

	MonochromeSequence& sequence = sequences_[editSequence_];
	char nameBuffer[kNameBufferSize]{};
	strncpy_s(nameBuffer, sequence.name.c_str(), _TRUNCATE);
	if (ImGui::InputText("名前", nameBuffer, sizeof(nameBuffer))) {
		sequence.name = nameBuffer;
	}

	// 再生
	if (ImGui::Button("再生")) Play(sequence.keys);
	ImGui::SameLine();
	if (ImGui::Button("停止(色に戻す)")) ResetParams();
	ImGui::SameLine();
	ImGui::Text("長さ %.2f s", GetDuration(sequence.keys));

	// コマの編集
	int removeIndex = -1;
	int swapA = -1;
	int swapB = -1;
	for (int i = 0; i < static_cast<int>(sequence.keys.size()); ++i) {
		MonochromeKey& key = sequence.keys[i];
		ImGui::PushID(i);
		ImGui::Separator();
		ImGui::Text("コマ %d", i);
		ImGui::SameLine();
		if (ImGui::SmallButton("上") && i > 0) { swapA = i - 1; swapB = i; }
		ImGui::SameLine();
		if (ImGui::SmallButton("下") && i + 1 < static_cast<int>(sequence.keys.size())) { swapA = i; swapB = i + 1; }
		ImGui::SameLine();
		if (ImGui::SmallButton("消す")) removeIndex = i;

		ImGui::DragFloat("移る時間[s]", &key.blendTime, 0.01f, 0.0f, 10.0f);
		ImGui::DragFloat("とどまる時間[s]", &key.holdTime, 0.01f, 0.0f, 10.0f);

		// 段階の初期値を入れる・今の見た目を入れる・このコマを見る
		for (int s = 0; s < static_cast<int>(MonochromeStage::Count); ++s) {
			if (s > 0) ImGui::SameLine();
			if (ImGui::SmallButton(kStageNames[s])) key.params = GetStagePreset(static_cast<MonochromeStage>(s));
		}
		if (ImGui::SmallButton("今の見た目を入れる")) key.params = params_;
		ImGui::SameLine();
		if (ImGui::SmallButton("このコマを見る")) {
			playing_ = false;
			params_ = key.params;
		}
		if (ImGui::TreeNode("値")) {
			EditParams(key.params);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (swapA >= 0) std::swap(sequence.keys[swapA], sequence.keys[swapB]);
	if (removeIndex >= 0) sequence.keys.erase(sequence.keys.begin() + removeIndex);
	if (ImGui::Button("コマを足す")) {
		sequence.keys.push_back(MakeKey(MonochromeStage::Color, 0.0f, kNewKeyHold));
	}

	// 保存
	ImGui::Separator();
	if (ImGui::Button("保存")) SaveSequences();
	ImGui::SameLine();
	if (ImGui::Button("読み直す")) LoadSequences(sequencesPath_);
	ImGui::SameLine();
	ImGui::TextDisabled("%s", sequencesPath_.c_str());
}
#endif
