#include "ReplaySystem.h"

#include <string>

#include "SessionLogger.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "MouseInput.h"
#include "ControllerInput.h"

ReplaySystem& ReplaySystem::Instance() {
    static ReplaySystem instance;
    return instance;
}

void ReplaySystem::InitializeRecord(uint32_t seed) {
    mode_ = Mode::Record;
    seed_ = seed;
    frame_ = 0;
    active_ = true;
}

void ReplaySystem::RecordFrame(float dt, InputManager* input) {
    if (!active_ || mode_ != Mode::Record || !input) {
        return;
    }

    std::string line = "frame=" + std::to_string(frame_) + " dt=" + std::to_string(dt);

    // キーボード：押下中の DIK コードを列挙（押されているものだけ＝コンパクト）
    line += " kb=";
    if (KeyboardInput* kb = input->GetKeyboard()) {
        bool first = true;
        for (int i = 0; i < 256; ++i) {
            if (kb->keys_[i] & 0x80) {
                if (!first) line += ",";
                line += std::to_string(i);
                first = false;
            }
        }
    }

    // マウス：移動量(dx,dy,wheel) とボタンマスク(bit0=L,1=R,2=M,3=B4)
    line += " mouse=";
    if (MouseInput* ms = input->GetMouse()) {
        int btn = 0;
        if (ms->IsButtonPressed(MouseInput::Button::Left))    btn |= 1 << 0;
        if (ms->IsButtonPressed(MouseInput::Button::Right))   btn |= 1 << 1;
        if (ms->IsButtonPressed(MouseInput::Button::Middle))  btn |= 1 << 2;
        if (ms->IsButtonPressed(MouseInput::Button::Button4)) btn |= 1 << 3;
        line += std::to_string(ms->GetDeltaX()) + "," + std::to_string(ms->GetDeltaY())
              + "," + std::to_string(ms->GetDeltaWheel()) + "," + std::to_string(btn);
    } else {
        line += "0,0,0,0";
    }

    // コントローラ：接続/スティック生値/トリガー生値/ボタンビット
    line += " pad=";
    if (ControllerInput* pad = input->GetController(); pad && pad->IsConnected()) {
        line += "1,"
              + std::to_string(pad->GetLeftStickRawX())  + "," + std::to_string(pad->GetLeftStickRawY())  + ","
              + std::to_string(pad->GetRightStickRawX()) + "," + std::to_string(pad->GetRightStickRawY()) + ","
              + std::to_string(static_cast<int>(pad->GetLeftTriggerRaw()))  + ","
              + std::to_string(static_cast<int>(pad->GetRightTriggerRaw())) + ","
              + std::to_string(static_cast<int>(pad->GetButtonsRaw()));
    } else {
        line += "0,0,0,0,0,0,0,0";
    }

    SessionLogger::Instance().Write(
        SessionLogger::Category::Input, SessionLogger::Level::Trace, line);

    ++frame_;
}
