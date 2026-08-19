#include "UIComponents.h"
#include "System/Input.h" // Input cuma dibutuhkan di sini, header tidak perlu tahu

// ================= STATUS PANEL =================
StatusPanel::StatusPanel(float x, float y, const std::string& startText)
    : UIElement(x, y), text(startText)
{}

void StatusPanel::SetStatus(const std::string& newText) {
    text = newText;
}

void StatusPanel::Draw(BitmapFont* font) {
    if (!font) return;
    font->Draw(text.c_str(), x, y, 0.625f, 1, 1, 1, 1);
}

// ================= LOG PANEL =================
LogPanel::LogPanel(float x, float y, int max)
    : UIElement(x, y), maxLines(max), lineSpacing(35.0f)
{}

void LogPanel::AddLog(const std::string& msg) {
    logs.push_back(msg);
    if (logs.size() > maxLines) {
        logs.pop_front();
    }
}

void LogPanel::Draw(BitmapFont* font) {
    if (!font) return;
    float currentY = y;
    for (const auto& line : logs) {
        font->Draw(line.c_str(), x, currentY, 0.625f, 1, 1, 1, 1);
        currentY += lineSpacing;
    }
}

// ================= DIRECTORY PANEL =================
DirectoryPanel::DirectoryPanel(float x, float y)
    : UIElement(x, y), selectedIndex(0), lineSpacing(40.0f)
{}

void DirectoryPanel::AddFile(std::string name, int size, bool isDir, std::function<void()> action) {
    files.push_back({ name, size, isDir, action });
}

void DirectoryPanel::Update(float dt) {
    Input& input = Input::Instance();

    // Logika navigasi keyboard
    if (input.GetKeyboard().IsTriggered(VK_UP)) {
        selectedIndex--;
        if (selectedIndex < 0) selectedIndex = (int)files.size() - 1;
    }
    if (input.GetKeyboard().IsTriggered(VK_DOWN)) {
        selectedIndex++;
        if (selectedIndex >= (int)files.size()) selectedIndex = 0;
    }
    if (input.GetKeyboard().IsTriggered(VK_RETURN)) {
        if (selectedIndex >= 0 && selectedIndex < files.size()) {
            if (files[selectedIndex].onExecute) {
                files[selectedIndex].onExecute();
            }
        }
    }
}

void DirectoryPanel::Draw(BitmapFont* font) {
    if (!font) return;
    float currentY = y;
    for (int i = 0; i < files.size(); i++) {
        // Highlight logic
        if (i == selectedIndex) {
            // Kuning kalau dipilih
            font->Draw(files[i].name.c_str(), x, currentY, 0.625f, 1.0f, 1.0f, 0.0f, 1.0f);
        }
        else {
            // Putih biasa
            font->Draw(files[i].name.c_str(), x, currentY, 0.625f, 1.0f, 1.0f, 1.0f, 1.0f);
        }
        currentY += lineSpacing;
    }
}