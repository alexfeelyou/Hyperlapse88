#pragma once

#include "Camera.h"
#include "RenderState.h"
#include "Light.h"

struct RenderContext
{
    ID3D11DeviceContext* deviceContext;
    const RenderState* renderState;
    const Camera* camera;
    LightManager* lightManager = nullptr;

    ID3D11RenderTargetView* velocityRenderTargetView{ nullptr }; 

    bool psxEnabled = false;
    float psxResWidth = 320.0f;
    float psxResHeight = 240.0f;
};