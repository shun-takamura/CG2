#include "DebugCamera.h"
#include "Transform.h"
#include <algorithm>
#include <cmath>

void DebugCamera::Initialize() {
    matRot_   = MakeIdentity4x4();
    pivot_    = { 0.0f, 0.0f, 0.0f };
    distance_ = 10.0f;
    Update();
}

void DebugCamera::SetPose(const Vector3& eye, const Matrix4x4& rot, float distance) {
    matRot_   = rot;
    distance_ = (distance < kMinDistance) ? kMinDistance : distance;
    // eye = pivot + matRot*(0,0,-distance) なので pivot = eye + forward*distance
    Vector3 forward = { matRot_.m[2][0], matRot_.m[2][1], matRot_.m[2][2] };
    pivot_ = { eye.x + forward.x * distance_,
               eye.y + forward.y * distance_,
               eye.z + forward.z * distance_ };
    Update();
}

void DebugCamera::Orbit(float deltaYaw, float deltaPitch) {
    // pitch はカメラの右軸（local X）周り、yaw はワールドY軸周り
    Matrix4x4 dPitch = MakeRotateXMatrix({ deltaPitch, 0.0f, 0.0f });
    Matrix4x4 dYaw   = MakeRotateYMatrix({ 0.0f, deltaYaw, 0.0f });
    // 既存実装と同じ「ローカル pitch を先に、ワールド yaw を後に」の合成
    matRot_ = Multiply(dPitch, matRot_);
    matRot_ = Multiply(matRot_, dYaw);
}

void DebugCamera::Pan(float deltaRight, float deltaUp) {
    // カメラの right / up はワールド回転行列の行ベクトル
    Vector3 right = { matRot_.m[0][0], matRot_.m[0][1], matRot_.m[0][2] };
    Vector3 up    = { matRot_.m[1][0], matRot_.m[1][1], matRot_.m[1][2] };
    pivot_.x += right.x * deltaRight + up.x * deltaUp;
    pivot_.y += right.y * deltaRight + up.y * deltaUp;
    pivot_.z += right.z * deltaRight + up.z * deltaUp;
}

void DebugCamera::Zoom(float delta) {
    distance_ += delta;
    if (distance_ < kMinDistance) {
        // 距離が下限を超えたぶんはピボットを視線方向（奥）へ押し出し、
        // 「奥へ突き進む」ように見せる（dolly）
        float excess = kMinDistance - distance_;
        // カメラのローカル +Z は視線奥方向。matRot_ の3行目がそれをワールドに変換した向き
        Vector3 forward = { matRot_.m[2][0], matRot_.m[2][1], matRot_.m[2][2] };
        pivot_.x += forward.x * excess;
        pivot_.y += forward.y * excess;
        pivot_.z += forward.z * excess;
        distance_ = kMinDistance;
    }
}

void DebugCamera::Update() {
    // ピボットから -Z 方向に distance_ 離れた位置をカメラとする
    Vector3 localOffset = { 0.0f, 0.0f, -distance_ };
    Vector3 rotated = TransformCoordinate(localOffset, matRot_);
    translate_ = { rotated.x + pivot_.x, rotated.y + pivot_.y, rotated.z + pivot_.z };

    // ワールド行列 = 回転 * translate
    Transform tf{};
    tf.translate = translate_;
    Matrix4x4 worldMatrix = Multiply(matRot_, MakeTranslateMatrix(tf));

    viewMatrix_       = Inverse(worldMatrix);
    // ピボットがクリップされないよう farClip は distance_ の 2 倍を下限とする
    float effectiveFar = std::max(farClip_, distance_ * 2.0f);
    projectionMatrix_ = MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, effectiveFar);
    viewProjectionMatrix_ = Multiply(viewMatrix_, projectionMatrix_);
}
