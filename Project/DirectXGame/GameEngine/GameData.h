#pragma once
#include <string>

class GameData {
public:
    static GameData* GetInstance() {
        static GameData instance;
        return &instance;
    }

    void SetSelectedModel(const std::string& model) { selectedModel_ = model; }
    void SetBulletModel(const std::string& model) { bulletModel_ = model; }
    void SetBulletMode(const std::string& mode) { bulletMode_ = mode; }

    const std::string& GetSelectedModel() const { return selectedModel_; }
    const std::string& GetBulletMode() const { return bulletMode_; }
    const std::string& GetBulletModel() const { return bulletModel_; }

private:
    GameData() = default;
    std::string selectedModel_ = "player1.obj";
    std::string bulletModel_ = "playerBullet.obj";
    std::string bulletMode_ = "normal";
};