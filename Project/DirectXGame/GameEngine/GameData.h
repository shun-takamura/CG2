#pragma once
#include <string>

class GameData {
public:
    static GameData* GetInstance() {
        static GameData instance;
        return &instance;
    }

    void SetSelectedModel(const std::string& model) { selectedModel_ = model; }
    const std::string& GetSelectedModel() const { return selectedModel_; }

private:
    GameData() = default;
    std::string selectedModel_ = "player1.obj";
};