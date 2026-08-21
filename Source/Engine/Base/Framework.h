#pragma once
#include <windows.h>
#include <memory>
#include "System/HighResolutionTimer.h"
#include "BeyondWindow.h"
#include "Scene.h"
#include "System/Graphics.h"
#include "System/ImGuiRenderer.h"
#include "System/Input.h"
#include "System/AudioManager.h"
#include "WindowManager.h"
#include "SceneTitle.h"
#include "SceneIntro.h"
#include "SceneGame.h"
#include <memory>
#include <sstream>
#include <iostream> 
#include <imgui.h>
#include <SDL3/SDL.h>

class Framework
{
public:
    Framework();
    ~Framework();
    static Framework* Instance();

    void Update(float elapsedTime);
    void Render(float elapsedTime);
    void ForceUpdateRender();
    void ChangeScene(std::unique_ptr<Scene> newScene);
    void Quit();

    Beyond::Window* GetMainWindow() const;

    LRESULT CALLBACK HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnSubWindowClosed(Uint32 sdlWindowID);

private:
    void CalculateFrameStats(float dt);

    static Framework* pInstance;
    HighResolutionTimer timer;

    std::unique_ptr<Scene> scene;
    std::unique_ptr<Scene> nextScene;
};