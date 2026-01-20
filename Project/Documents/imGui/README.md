# ImGuiシステム 使い方ガイド

## ファイル構成

```
project/
├── debug/
│   ├── Debug.h          ... Debug::Log() など
│   ├── Debug.cpp
│   ├── LogBuffer.h      ... ログ保持クラス
│   └── LogBuffer.cpp
│
├── imgui/
│   ├── IImGuiWindow.h   ... ウィンドウ基底クラス
│   ├── IImGuiEditable.h ... 編集可能オブジェクト基底
│   ├── IImGuiEditable.cpp
│   ├── ImGuiManager.h   ... 管理クラス
│   ├── ImGuiManager.cpp
│   ├── FPSWindow.h      ... FPS表示
│   ├── LogWindow.h      ... ログ表示
│   ├── HierarchyWindow.h    ... オブジェクト一覧
│   ├── HierarchyWindow.cpp
│   ├── InspectorWindow.h    ... 選択オブジェクト編集
│   └── InspectorWindow.cpp
│
└── modified/
    ├── Object3DInstance.h   ... 修正版（IImGuiEditable継承）
    ├── Object3DInstance.cpp
    ├── SpriteInstance.h     ... 修正版（IImGuiEditable継承）
    └── SpriteInstance.cpp
```

---

## セットアップ手順

### 1. ImGuiライブラリの準備

以下のImGuiファイルをプロジェクトに追加:
- imgui.h / imgui.cpp
- imgui_draw.cpp
- imgui_tables.cpp
- imgui_widgets.cpp
- imgui_impl_win32.h / imgui_impl_win32.cpp
- imgui_impl_dx12.h / imgui_impl_dx12.cpp

### 2. WndProcにImGuiハンドラを追加

```cpp
// WindowsApplication.cppなど

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // ImGuiにメッセージを渡す
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }
    
    // 既存の処理...
}
```

### 3. main.cppの修正

```cpp
// インクルード追加
#include "imgui/ImGuiManager.h"
#include "debug/Debug.h"

int WINAPI WinMain(...) {
    // ... 既存の初期化 ...

    // SRVManagerの初期化の後にImGuiを初期化
    SRVManager* srvManager = new SRVManager();
    srvManager->Initialize(dxCore);

    // ImGui初期化
    ImGuiManager::Instance().Initialize(
        winApp->GetHwnd(), 
        dxCore, 
        srvManager
    );

    // オブジェクト生成（自動でImGuiに登録される）
    Object3DInstance* obj = new Object3DInstance();
    obj->Initialize(object3DManager, dxCore, "Resources", "axis.obj", "MyObject");
    //                                                               ^^^^^^^^^ 
    //                                                               名前を指定

    // ゲームループ
    while (/* ... */) {
        // ゲーム処理の前にImGuiフレーム開始
        ImGuiManager::Instance().BeginFrame();

        // ログ出力例
        Debug::Log("フレーム更新");
        Debug::LogWarning("警告メッセージ");
        Debug::LogError("エラーメッセージ");

        // ... ゲームの更新処理 ...

        // 描画
        dxCore->BeginDraw();
        srvManager->PreDraw();

        // ... 3D/2D描画 ...

        // ImGui描画（最後に呼ぶ）
        ImGuiManager::Instance().EndFrame();

        dxCore->EndDraw();
    }

    // 終了処理
    ImGuiManager::Instance().Shutdown();
    // ... 他の終了処理 ...
}
```

---

## 使い方

### ログ出力

```cpp
#include "debug/Debug.h"

// 情報ログ
Debug::Log("プレイヤーがスポーンしました");

// 警告ログ（黄色で表示）
Debug::LogWarning("テクスチャが見つかりません");

// エラーログ（赤色で表示）
Debug::LogError("初期化に失敗しました");
```

### オブジェクトの名前指定

```cpp
// 名前を指定して初期化
Object3DInstance* player = new Object3DInstance();
player->Initialize(manager, dxCore, "Resources", "player.obj", "Player");

// または後から設定
player->SetName("Player");

// Spriteも同様
SpriteInstance* uiSprite = new SpriteInstance();
uiSprite->Initialize(spriteManager, "Resources/ui.png", "MainUI");
```

### 自動登録の仕組み

Object3DInstanceやSpriteInstanceを`new`で生成すると、
コンストラクタ内で自動的にImGuiManagerに登録されます。

`delete`すると自動的に登録解除されます。

```cpp
// 生成 → 自動でHierarchyに表示される
Object3DInstance* obj = new Object3DInstance();
obj->Initialize(...);

// 削除 → 自動でHierarchyから消える
delete obj;
```

---

## ウィンドウの表示/非表示

メニューバーの「Windows」から各ウィンドウの表示/非表示を切り替えられます。

---

## 新しいウィンドウを追加する場合

```cpp
// MyWindow.h
#pragma once
#include "IImGuiWindow.h"

class MyWindow : public IImGuiWindow {
public:
    MyWindow() : IImGuiWindow("My Window") {}

protected:
    void OnDraw() override {
        ImGui::Text("カスタムウィンドウ");
        // 好きな内容を描画
    }
};
```

ImGuiManager::Initialize()内で追加:
```cpp
windows_.push_back(std::make_unique<MyWindow>());
```

---

## 新しい編集可能オブジェクトを追加する場合

```cpp
// MyObject.h
#pragma once
#include "imgui/IImGuiEditable.h"

class MyObject : public IImGuiEditable {
public:
    MyObject(const std::string& name) : name_(name) {}

    std::string GetName() const override { return name_; }
    std::string GetTypeName() const override { return "MyObject"; }
    
    void OnImGuiInspector() override {
        ImGui::DragFloat("Value", &value_, 0.1f);
    }

private:
    std::string name_;
    float value_ = 0.0f;
};
```

---

## 注意事項

1. **ImGuiManager::Initialize()はSRVManager初期化の後に呼ぶ**
   - SRVインデックスを確保するため

2. **ImGuiManager::EndFrame()は描画の最後に呼ぶ**
   - ImGuiは最前面に表示する必要があるため

3. **オブジェクトの破棄順序に注意**
   - ImGuiManager::Shutdown()の前にオブジェクトを破棄しないこと
   - または、Shutdown()でeditables_をクリアしているので問題なし
