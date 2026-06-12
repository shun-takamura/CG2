#include "ReplaySystem.h"

#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>

#include "SessionLogger.h"
#include "InputManager.h"
#include "InputAction.h"
#include "KeyboardInput.h"
#include "MouseInput.h"
#include "ControllerInput.h"

namespace {
    // "key=value" トークンから value 文字列を返す（先頭が key= でなければ空）
    std::string ValueOf(const std::string& token, const char* key) {
        const size_t klen = std::char_traits<char>::length(key);
        if (token.size() >= klen && token.compare(0, klen, key) == 0) {
            return token.substr(klen);
        }
        return std::string();
    }

    // カンマ区切りを int に分解
    std::vector<long> SplitCsv(const std::string& s) {
        std::vector<long> out;
        if (s.empty()) return out;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) out.push_back(std::strtol(item.c_str(), nullptr, 10));
        }
        return out;
    }
}

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

void ReplaySystem::InitializeReplay(const std::string& dir) {
    mode_ = Mode::Replay;
    records_.clear();
    replayIndex_ = 0;
    active_ = true;

    // session.log から記録時シードを取り出す（"[Random] seed=N ..."）
    {
        std::ifstream sess(dir + "/session.log");
        std::string line;
        while (std::getline(sess, line)) {
            const size_t p = line.find("seed=");
            if (p != std::string::npos) {
                loadedSeed_ = static_cast<uint32_t>(std::strtoul(line.c_str() + p + 5, nullptr, 10));
                hasLoadedSeed_ = true;
                break;
            }
        }
    }

    // input.log を1行ずつパース（SessionLogger の prefix は "frame=" 以降を探してスキップ）
    std::ifstream in(dir + "/input.log");
    std::string line;
    while (std::getline(in, line)) {
        const size_t fp = line.find("frame=");
        if (fp == std::string::npos) continue;

        FrameRecord rec;
        std::stringstream ss(line.substr(fp));
        std::string token;
        while (ss >> token) {
            std::string v;
            if (!(v = ValueOf(token, "dt=")).empty()) {
                rec.dt = std::strtof(v.c_str(), nullptr);
            } else if (token.compare(0, 3, "kb=") == 0) {
                for (long code : SplitCsv(token.substr(3))) {
                    if (code >= 0 && code < 256) rec.keys[code] = 0x80;
                }
            } else if (token.compare(0, 6, "mouse=") == 0) {
                auto m = SplitCsv(token.substr(6));
                if (m.size() >= 4) {
                    rec.mdx = static_cast<int32_t>(m[0]); rec.mdy = static_cast<int32_t>(m[1]);
                    rec.mwheel = static_cast<int32_t>(m[2]); rec.mbtn = static_cast<int>(m[3]);
                }
            } else if (token.compare(0, 4, "pad=") == 0) {
                auto p = SplitCsv(token.substr(4));
                if (p.size() >= 8) {
                    rec.padConnected = (p[0] != 0);
                    rec.lx = static_cast<int16_t>(p[1]); rec.ly = static_cast<int16_t>(p[2]);
                    rec.rx = static_cast<int16_t>(p[3]); rec.ry = static_cast<int16_t>(p[4]);
                    rec.lt = static_cast<uint8_t>(p[5]); rec.rt = static_cast<uint8_t>(p[6]);
                    rec.btns = static_cast<uint16_t>(p[7]);
                }
            }
        }
        records_.push_back(rec);
    }
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

bool ReplaySystem::AdvanceReplay(InputManager* input, float& outDt) {
    if (mode_ != Mode::Replay || !input) {
        return false;
    }
    if (replayIndex_ >= records_.size()) {
        return false;  // 記録を再生し終えた
    }

    const FrameRecord& r = records_[replayIndex_];

    // 通常 Update と同じ順序：アクション層の前処理 → 各デバイスへ注入
    if (InputActionMap* am = input->GetActionMap()) {
        am->BeginFrame();
    }
    if (KeyboardInput* kb = input->GetKeyboard()) {
        kb->ApplyReplay(r.keys);
    }
    if (MouseInput* ms = input->GetMouse()) {
        ms->ApplyReplay(r.mdx, r.mdy, r.mwheel, r.mbtn);
    }
    if (ControllerInput* pad = input->GetController()) {
        pad->ApplyReplay(r.padConnected,
            static_cast<SHORT>(r.lx), static_cast<SHORT>(r.ly),
            static_cast<SHORT>(r.rx), static_cast<SHORT>(r.ry),
            static_cast<BYTE>(r.lt), static_cast<BYTE>(r.rt),
            static_cast<WORD>(r.btns));
    }

    outDt = r.dt;
    ++replayIndex_;
    return true;
}
