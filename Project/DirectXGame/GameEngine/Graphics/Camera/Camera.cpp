#include "Camera.h"
#include "imgui.h"

Camera::Camera()
	: transform_({ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f} })
	, horizontalFovY_(0.45f)
	, aspectRatio_(float(WindowsApplication::kClientWidth) / float(WindowsApplication::kClientHeight))
	, nearClip_(0.1f)
	, farClip_(100.0f)
	, worldMatrix_(MakeAffineMatrix(transform_))
	, viewMatrix_(Inverse(worldMatrix_))
	, projectionMatrix_(MakePerspectiveFovMatrix(horizontalFovY_, aspectRatio_, nearClip_, farClip_))
	, viewProjectionMatrix_(Multiply(viewMatrix_, projectionMatrix_))
{
}

void Camera::OnImGui()
{
#ifdef _DEBUG
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &transform_.translate.x, 0.1f);

        Vector3 rotateDegree = {
            transform_.rotate.x * (180.0f / 3.14159265f),
            transform_.rotate.y * (180.0f / 3.14159265f),
            transform_.rotate.z * (180.0f / 3.14159265f)
        };
        if (ImGui::DragFloat3("Rotation", &rotateDegree.x, 1.0f)) {
            transform_.rotate.x = rotateDegree.x * (3.14159265f / 180.0f);
            transform_.rotate.y = rotateDegree.y * (3.14159265f / 180.0f);
            transform_.rotate.z = rotateDegree.z * (3.14159265f / 180.0f);
        }

        ImGui::DragFloat3("Scale", &transform_.scale.x, 0.01f);
        ImGui::DragFloat("FovY", &horizontalFovY_, 0.01f, 0.01f, 3.14f);
        ImGui::DragFloat("NearClip", &nearClip_, 0.01f, 0.001f, 10.0f);
        ImGui::DragFloat("FarClip", &farClip_, 1.0f, 1.0f, 1000.0f);
    }
#endif
}

void Camera::Update()
{
	worldMatrix_ = MakeAffineMatrix(transform_);
	viewMatrix_ = Inverse(worldMatrix_);

	projectionMatrix_ = MakePerspectiveFovMatrix(horizontalFovY_, aspectRatio_, nearClip_, farClip_);

	viewProjectionMatrix_ = Multiply(viewMatrix_, projectionMatrix_);
}
