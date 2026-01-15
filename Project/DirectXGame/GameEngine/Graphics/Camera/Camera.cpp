#include "Camera.h"

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

void Camera::Update()
{
	worldMatrix_ = MakeAffineMatrix(transform_);
	viewMatrix_ = Inverse(worldMatrix_);

	projectionMatrix_ = MakePerspectiveFovMatrix(horizontalFovY_, aspectRatio_, nearClip_, farClip_);

	viewProjectionMatrix_ = Multiply(viewMatrix_, projectionMatrix_);
}
