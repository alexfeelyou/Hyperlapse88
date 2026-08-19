#pragma once

#include <memory>
#include <imgui.h>
#include <wrl/client.h>
#include "Scene.h"
#include "Camera.h"
#include "System/AudioManager.h"
#include "System/Sprite.h" 
#include "System/Input.h"
#include "System/Graphics.h"
#include "BitmapFont.h"
#include "Framework.h"
#include "ResourceManager.h"
#include "UberShader.h"

class SceneIntro : public Scene
{
public:
    SceneIntro();
    ~SceneIntro() override = default;

    void Update(float elapsedTime) override;
    void Render(float dt, Camera* camera = nullptr) override;
    void DrawGUI() override;
    void OnResize(int width, int height) override;

    Camera* GetCamera() const { return camera.get(); }

private:
    void CreateRenderTarget();

    std::unique_ptr<Camera> camera;

    // Helper for the GUI
    void GUIPostProcessTab();

    // GUI State
    struct PostProcessState
    {
        bool MasterEnabled = true; 
        bool EnableVignette = true;
        bool EnableLens = true;
        bool EnableCRT = true;
    };
    PostProcessState m_fxState;

    // Post-Processing Resources
    std::unique_ptr<UberShader> uberShader;
    UberShader::UberData uberParams;
    float m_globalTime = 0.0f;

    // DirectX Render Targets
    Microsoft::WRL::ComPtr<ID3D11Texture2D> renderTargetTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResourceView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencilTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilView;
};