#pragma once

#include <string>
#include <map>
#include <memory>
#include "System/Sprite.h" // Menggunakan Sprite milikmu
#include <DirectXMath.h>

// Struktur data untuk menyimpan info huruf dari file .fnt
struct CharData {
    int id;
    int x, y;
    int width, height;
    int xoffset, yoffset;
    int xadvance;
};

class BitmapFont
{
public:
    // Constructor: Load gambar & parse data .fnt
    BitmapFont(const std::string& texturePath, const std::string& fontDataPath);
    ~BitmapFont() = default;

    // Fungsi untuk menggambar teks
    void Draw(const std::string& text, float x, float y, float scale, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    DirectX::XMFLOAT2 MeasureText(const std::string& text, float scale);

    void Draw3D(const std::string& text,
        const Camera* camera,
        DirectX::XMFLOAT3 worldPos,
        float scale,
        DirectX::XMFLOAT3 rotation = { 0,0,0 }, // Pitch, Yaw, Roll
        DirectX::XMFLOAT4 color = { 1,1,1,1 });

private:
    std::unique_ptr<Sprite> sprite; // Pointer ke Sprite sistem kamu
    std::map<int, CharData> chars;  // Database koordinat huruf

    // Helper parsing sederhana
    int ParseValue(const std::string& line, const std::string& key);
};