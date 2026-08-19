#include "System/Input.h"

void Input::Initialize(HWND hWnd)
{
	gamePad = std::make_unique<GamePad>();
	mouse = std::make_unique<Mouse>(hWnd);
	keyboard = std::make_unique<Keyboard>();

    ::GetCursorPos(&m_lastMousePos);
}

void Input::Update()
{
	gamePad->Update();
	mouse->Update();
	keyboard->Update();
    
    // ---------------------------------------------------------
    // GAMEPAD DETECTION
    // ---------------------------------------------------------
    constexpr float analogDeadzone{ 0.15f };

    const bool isGamepadActive{
        gamePad->GetButton() != 0 ||
        std::abs(gamePad->GetAxisLX()) > analogDeadzone ||
        std::abs(gamePad->GetAxisLY()) > analogDeadzone ||
        std::abs(gamePad->GetAxisRX()) > analogDeadzone ||
        std::abs(gamePad->GetAxisRY()) > analogDeadzone ||
        gamePad->GetTriggerL() > analogDeadzone ||
        gamePad->GetTriggerR() > analogDeadzone
    };

    // ---------------------------------------------------------
    // KEYBOARD & MOUSE DETECTION (With Jitter Guard)
    // ---------------------------------------------------------
    bool isKbmActive{ false };

    // Mouse Jitter Guard (Requires a move of > 1 pixel to trigger)
    POINT currentMousePos{};
    if (::GetCursorPos(&currentMousePos))
    {
        const int deltaX{ std::abs(currentMousePos.x - m_lastMousePos.x) };
        const int deltaY{ std::abs(currentMousePos.y - m_lastMousePos.y) };

        if (deltaX > 1 || deltaY > 1)
        {
            isKbmActive = true;
        }
        m_lastMousePos = currentMousePos; // Always update history
    }

    // B. Core Keyboard Polling (Only runs if mouse was idle, thanks to short-circuit !)
    if (!isKbmActive)
    {
        // Poll the absolute most common action keys. 
        if ((GetAsyncKeyState('W') & 0x8000) || (GetAsyncKeyState('A') & 0x8000) ||
            (GetAsyncKeyState('S') & 0x8000) || (GetAsyncKeyState('D') & 0x8000) ||
            (GetAsyncKeyState(VK_SHIFT) & 0x8000) || (GetAsyncKeyState(VK_SPACE) & 0x8000) ||
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ||
            (GetAsyncKeyState(VK_RETURN) & 0x8000) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
        {
            isKbmActive = true;
        }
    }

    // ---------------------------------------------------------
    // RESOLVE CONFLICT & UPDATE OS
    // ---------------------------------------------------------
    // If someone mashes both, Gamepad wins priority to prevent rapid flickering
    if (isGamepadActive) {
        m_lastUsedDevice = InputDevice::Gamepad;
    }
    else if (isKbmActive) {
        m_lastUsedDevice = InputDevice::Keyboard;
    }

    UpdateCursorVisibility();
}

void Input::UpdateCursorVisibility() noexcept
{
    // The strict rule: Keyboard = Visible, Gamepad = Hidden.
    const bool shouldBeVisible{ m_lastUsedDevice == InputDevice::Keyboard };

    // Early Exit
    if (m_isCursorVisible == shouldBeVisible) return;

    m_isCursorVisible = shouldBeVisible;

    // The Win32 OS Bug Fix: Safely force the OS internal counter to cross the 0 threshold.
    if (shouldBeVisible)
    {
        while (::ShowCursor(TRUE) < 0);
    }
    else
    {
        while (::ShowCursor(FALSE) >= 0);
    }
}