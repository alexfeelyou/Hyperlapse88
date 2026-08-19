#pragma once
#include <vector>
#include <memory>
#include "UIButton.h"

class ButtonManager
{
public:
    // Polymorphism magic: Kita simpan pointer ke Base Class (UIButton)
    void AddButton(std::unique_ptr<UIButton> button)
    {
        buttons.push_back(std::move(button));
    }

    void Update()
    {
        for (auto& btn : buttons) btn->Update();
    }

    void Render(ID3D11DeviceContext* dc, Camera* camera)
    {
        for (auto& btn : buttons) btn->Render(dc, camera);
    }

    void Clear() { buttons.clear(); }

private:
    // Vector ini bisa menampung SpriteButton MAUPUN PrimitiveButton
    std::vector<std::unique_ptr<UIButton>> buttons;
};