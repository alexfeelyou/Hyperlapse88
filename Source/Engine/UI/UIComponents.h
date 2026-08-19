#pragma once
#include <vector>
#include <string>
#include <deque>
#include <functional>
#include <memory>
#include "BitmapFont.h" // Untuk BitmapFont

// --- BASE CLASS ---
class UIElement {
public:
    float x, y;
    UIElement(float _x, float _y) : x(_x), y(_y) {}
    virtual ~UIElement() = default;

    virtual void Update(float dt) {} // Default kosong
    virtual void Draw(BitmapFont* font) = 0; // Pure virtual
};

// --- STATUS PANEL ---
class StatusPanel : public UIElement {
private:
    std::string text;
public:
    StatusPanel(float x, float y, const std::string& startText);
    void SetStatus(const std::string& newText);
    void Draw(BitmapFont* font) override;
};

// --- LOG PANEL ---
class LogPanel : public UIElement {
private:
    std::deque<std::string> logs;
    int maxLines;
    float lineSpacing;
public:
    LogPanel(float x, float y, int max);
    void AddLog(const std::string& msg);
    void Draw(BitmapFont* font) override;
};

// --- DIRECTORY PANEL ---
struct FileEntry {
    std::string name;
    int size;
    bool isDir;
    std::function<void()> onExecute;
};

class DirectoryPanel : public UIElement {
private:
    std::vector<FileEntry> files;
    int selectedIndex;
    float lineSpacing;
public:
    DirectoryPanel(float x, float y);
    void AddFile(std::string name, int size, bool isDir, std::function<void()> action);
    void Update(float dt) override; // Override Update karena butuh Input
    void Draw(BitmapFont* font) override;
};