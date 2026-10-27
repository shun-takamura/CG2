#include "UIPointer.h"

#include "InputManager.h"
#include "MouseInput.h"
#include "WindowsApplication.h"

#ifdef _DEBUG
#include "imgui.h"
#include "ImGuiManager.h"
#include "ViewportWindow.h"
#endif

UIPointer UIPointer::FromInput(InputManager* input) {
	UIPointer pointer;
	MouseInput* mouse = input ? input->GetMouse() : nullptr;
	if (!mouse) return pointer;

#ifdef _DEBUG
	const float width = static_cast<float>(WindowsApplication::kUIWidth);
	const float height = static_cast<float>(WindowsApplication::kUIHeight);
	auto* vp = ImGuiManager::Instance().GetViewportWindow();
	if (!vp || !vp->IsHovered()) return pointer;
	const ImVec2 imagePos = vp->GetImageScreenPos();
	const ImVec2 imageSize = vp->GetImageScreenSize();
	if (imageSize.x <= 0.0f || imageSize.y <= 0.0f) return pointer;
	const ImVec2 mousePos = ImGui::GetMousePos();
	pointer.position = { (mousePos.x - imagePos.x) * (width / imageSize.x),
		(mousePos.y - imagePos.y) * (height / imageSize.y) };
	const ImVec2 delta = ImGui::GetIO().MouseDelta;
	pointer.moved = (delta.x != 0.0f) || (delta.y != 0.0f);
#else
	// クライアント座標 → 黒帯を引く → 内部解像度へ換算（最大化・フルスクリーンでもずれない）
	if (!mouse->GetUIPosition(pointer.position.x, pointer.position.y)) {
		return pointer;
	}
	pointer.moved = (mouse->GetDeltaX() != 0) || (mouse->GetDeltaY() != 0);
#endif

	pointer.valid = true;
	pointer.clicked = mouse->IsButtonTriggered(MouseInput::Button::Left);
	pointer.pressed = mouse->IsButtonPressed(MouseInput::Button::Left);
	return pointer;
}
