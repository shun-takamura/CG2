#include "StageClearRecord.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

StageClearRecord* StageClearRecord::GetInstance() {
	static StageClearRecord instance;
	return &instance;
}

void StageClearRecord::ResetForNewRun() {
	clearTime_ = 0.0f;
	cleared_ = false;
	ClearCapture();
}

void StageClearRecord::SetCapture(const std::string& textureName, uint32_t srvIndex, uint32_t width, uint32_t height) {
	if (textureName.empty() || width == 0 || height == 0) {
		ClearCapture();
		return;
	}
	hasCapture_ = true;
	captureTextureName_ = textureName;
	captureSrvIndex_ = srvIndex;
	captureWidth_ = width;
	captureHeight_ = height;
}

void StageClearRecord::ClearCapture() {
	hasCapture_ = false;
	captureTextureName_.clear();
	captureSrvIndex_ = 0;
	captureWidth_ = 0;
	captureHeight_ = 0;
}

void StageClearRecord::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("Stage : %s", stageName_.c_str());
	ImGui::Text("Clear time : %.2f s%s", clearTime_, cleared_ ? " (cleared)" : "");
	ImGui::Text("Capture : %s", hasCapture_ ? captureTextureName_.c_str() : "(none)");
#endif
}
