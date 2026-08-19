#pragma once

#include <functional>
#include <DirectXMath.h>
#include <d3d11.h>
#include "System/Input.h"

// Callback type
using OnClickCallback = std::function<void()>;

enum class ButtonState { STANDBY, HOVER, PRESSED };

class UIButton
{
public:
    UIButton(float x, float y, float width, float height)
        : rect{ (long)x, (long)y, (long)(x + width), (long)(y + height) }
        , width(width), height(height) {}

    virtual ~UIButton() = default;

    // --- PURE VIRTUAL (Anak Wajib Implementasi) ---
    // Setiap anak harus menggambar dirinya sendiri dengan caranya masing-masing
    virtual void Render(ID3D11DeviceContext* dc, class Camera* camera) = 0;

    // --- SHARED LOGIC (Semua anak pakai logika yang sama) ---
    void Update()
    {
        // 1. Jika tombol ini Terpilih (Selected), PAKSA state jadi PRESSED
        // Ini memastikan visualnya tetap "Mencet" walaupun mouse pergi.
        if (isSelected)
        {
            currentState = ButtonState::PRESSED;

            // Kita tetap butuh cek klik mouse jika ingin logic "Re-click"
            // Tapi untuk visual, kita return di sini agar tidak ditimpa logic hover di bawah.
            return;
        }

        // 2. Standard Mouse Logic (Hanya jalan jika TIDAK Selected)
        Mouse& mouse = Input::Instance().GetMouse();
        POINT mousePos = { mouse.GetPositionX(), mouse.GetPositionY() };

        bool isHover = PtInRect(&rect, mousePos);
        bool isMouseDown = mouse.GetButtonDown() & Mouse::BTN_LEFT;

        if (isHover)
        {
            if (isMouseDown) currentState = ButtonState::PRESSED;
            else
            {
                // Logic klik terjadi saat mouse dilepas (Mouse Up) di dalam area
                if (currentState == ButtonState::PRESSED && callback) callback();
                currentState = ButtonState::HOVER;
            }
        }
        else
        {
            currentState = ButtonState::STANDBY;
        }
    }

    // --- NEW HELPERS ---
    void SetSelected(bool value) { isSelected = value; }
    bool IsSelected() const { return isSelected; }
    void SetOnClick(OnClickCallback cb) { callback = cb; }
    ButtonState GetState() const { return currentState; }

    // Di dalam class UIButton, bagian Public:
    float GetX() const { return (float)rect.left; }
    float GetY() const { return (float)rect.top; }
    float GetWidth() const { return width; }
    float GetHeight() const { return height; }
    
    void SetPosition(float x, float y)
    {
        rect.left = (long)x;
        rect.top = (long)y;
        rect.right = (long)(x + width);
        rect.bottom = (long)(y + height);
    }

    void SetSize(float w, float h)
    {
        width = w;
        height = h;
        // Kita harus hitung ulang batas kanan dan bawah rect berdasarkan ukuran baru
        rect.right = rect.left + (long)width;
        rect.bottom = rect.top + (long)height;
    }


protected:
    RECT rect;
    float width, height;
    ButtonState currentState = ButtonState::STANDBY;
    OnClickCallback callback;
    bool isSelected = false;
};