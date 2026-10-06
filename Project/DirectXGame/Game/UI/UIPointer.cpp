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

	const float width = static_cast<float>(WindowsApplication::kClientWidth);
	const float height = static_cast<float>(WindowsApplication::kClientHeight);

#ifdef _DEBUG
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
	pointer.position = { static_cast<float>(mouse->GetClientX()), static_cast<float>(mouse->GetClientY()) };
	if (pointer.position.x < 0.0f || pointer.position.y < 0.0f ||
		pointer.position.x >= width || pointer.position.y >= height) {
		return pointer;
	}
	pointer.moved = (mouse->GetDeltaX() != 0) || (mouse->GetDeltaY() != 0);
#endif

	pointer.valid = true;
	pointer.clicked = mouse->IsButtonTriggered(MouseInput::Button::Left);
	return pointer;
}
