#pragma once

#include <functional>
#include <imgui.h>
#include <iostream> 
#include <memory>
#include <SDL3/SDL.h>
#include <sstream>
#include <windows.h>
#include "System/AudioManager.h"
#include "System/Graphics.h"
#include "System/HighResolutionTimer.h"
#include "System/ImGuiRenderer.h"
#include "System/Input.h"
#include "EditorManager.h"
#include "ProfilerManager.h"
#include "RenderWindow.h"
#include "Scene.h"
#include "SceneGame.h"
#include "SceneTitle.h"
#include "WindowManager.h"

class Framework
{
public:
    Framework();
    ~Framework();
    static Framework* Instance();

    void Update(float elapsedTime);
    void Render(float elapsedTime);
    void ForceUpdateRender();
    void ChangeScene(std::function<std::unique_ptr<Scene>()> sceneFactory);
    void OnResize(int width, int height);
    void Quit();

    platform::Window* GetMainWindow() const;

    LRESULT CALLBACK HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void CalculateFrameStats(float dt);

    static Framework* pInstance;
    HighResolutionTimer timer;

    std::unique_ptr<Scene> scene;
    std::function<std::unique_ptr<Scene>()> m_nextSceneFactory{};
};