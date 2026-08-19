#pragma once
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <sstream>
#include "Primitive.h"
#include "UIButtonPrimitive.h"
#include "BitmapFont.h"
#include "ResourceManager.h"

// Struct Style biar profesional & gampang ganti tema
struct PanelStyle {
    DirectX::XMFLOAT4 colorBg = { 0.0f, 0.0f, 0.0f, 1.0f };       // Hitam
    DirectX::XMFLOAT4 colorBorder = { 0.96f, 0.80f, 0.23f, 1.0f };    // Kuning
    DirectX::XMFLOAT4 colorText = { 0.96f, 0.80f, 0.23f, 1.0f };    // Kuning
    DirectX::XMFLOAT4 colorDimmer = { 0.0f, 0.0f, 0.0f, 0.2f };       // Redup belakang
    float borderThickness = 2.5f;
    float textScale = 0.625f;
};

class UIPanel
{
public:
    UIPanel(Primitive* prim, float x, float y, float w, float h, const std::string& title);

    void SetStyle(const PanelStyle& newStyle) { style = newStyle; }
    void Show();
    void Hide();
    bool IsVisible() const { return isVisible; }

    void SetMessage(const std::string& msg); // Support Auto-Wrap
    void AddButton(const std::string& label, float relX, float relY, float w, float h, std::function<void()> onClick);
    void SetOnClose(std::function<void()> callback) { onClose = callback; }

    void Update();
    void Render(ID3D11DeviceContext* dc);

private:
    void RecalculateTextLayout(); // Helper potong kata otomatis

    Primitive* primitive;
    float x, y, width, height;
    std::string title;

    // Text handling
    std::string rawMessage;
    std::vector<std::string> formattedLines;

    bool isVisible = false;
    PanelStyle style;
    std::vector<std::unique_ptr<UIButtonPrimitive>> buttons;
    std::function<void()> onClose = nullptr;
};