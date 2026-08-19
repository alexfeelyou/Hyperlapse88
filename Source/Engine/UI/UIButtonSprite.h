#pragma once
#include "UIButton.h"
#include "System/Sprite.h"
#include "System/Graphics.h"

struct ButtonTints { float r, g, b; };

class UIButtonSprite : public UIButton
{
public:
    UIButtonSprite(const char* texturePath, float x, float y, float w, float h)
        : UIButton(x, y, w, h)
    {
        sprite = std::make_unique<Sprite>(Graphics::Instance().GetDevice(), texturePath);
    }

    // Implementasi khusus Sprite
    void Render(ID3D11DeviceContext* dc, Camera* camera) override
    {
        if (!sprite) return;

        // Pilih warna berdasarkan state dari Parent
        float r = 1, g = 1, b = 1;
        switch (currentState)
        {
        case ButtonState::STANDBY: r = 1.0f; g = 1.0f; b = 1.0f; break;
        case ButtonState::HOVER:   r = 1.2f; g = 1.2f; b = 1.2f; break; // Lebih terang
        case ButtonState::PRESSED: r = 0.7f; g = 0.7f; b = 0.7f; break; // Lebih gelap
        }

        sprite->Render(dc, camera,
            (float)rect.left, (float)rect.top, 0,
            width, height,
            0, 0, 0,
            r, g, b, 1.0f);
    }

private:
    std::unique_ptr<Sprite> sprite;
};