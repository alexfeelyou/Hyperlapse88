#include "BitmapFont.h"
#include "System/Graphics.h" // Untuk mengambil Device & Context
#include "Camera.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace
{
    size_t NextUtf8Offset(const std::string& text, size_t offset)
    {
        if (offset >= text.size()) return text.size();

        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        size_t length = 1;

        if ((lead & 0x80) == 0x00) length = 1;
        else if ((lead & 0xE0) == 0xC0) length = 2;
        else if ((lead & 0xF0) == 0xE0) length = 3;
        else if ((lead & 0xF8) == 0xF0) length = 4;

        return (std::min)(offset + length, text.size());
    }

    int DecodeUtf8Codepoint(const std::string& text, size_t offset, size_t nextOffset)
    {
        const unsigned char c0 = static_cast<unsigned char>(text[offset]);
        const size_t length = nextOffset - offset;

        if (length == 1) return c0;
        if (length == 2)
        {
            return ((c0 & 0x1F) << 6) |
                (static_cast<unsigned char>(text[offset + 1]) & 0x3F);
        }
        if (length == 3)
        {
            return ((c0 & 0x0F) << 12) |
                ((static_cast<unsigned char>(text[offset + 1]) & 0x3F) << 6) |
                (static_cast<unsigned char>(text[offset + 2]) & 0x3F);
        }
        if (length == 4)
        {
            return ((c0 & 0x07) << 18) |
                ((static_cast<unsigned char>(text[offset + 1]) & 0x3F) << 12) |
                ((static_cast<unsigned char>(text[offset + 2]) & 0x3F) << 6) |
                (static_cast<unsigned char>(text[offset + 3]) & 0x3F);
        }

        return c0;
    }
}

BitmapFont::BitmapFont(const std::string& texturePath, const std::string& fontDataPath)
{
    // 1. Ambil Device dan buat Sprite
    auto device = Graphics::Instance().GetDevice();
    sprite = std::make_unique<Sprite>(device, texturePath.c_str());

    // 2. Parse file .fnt
    std::ifstream file(fontDataPath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line))
    {
        // Kita hanya ambil baris yang mendefinisikan karakter ("char id=...")
        if (line.find("char id=") != std::string::npos)
        {
            CharData c;
            c.id = ParseValue(line, "id");
            c.x = ParseValue(line, "x");
            c.y = ParseValue(line, "y");
            c.width = ParseValue(line, "width");
            c.height = ParseValue(line, "height");
            c.xoffset = ParseValue(line, "xoffset");
            c.yoffset = ParseValue(line, "yoffset");
            c.xadvance = ParseValue(line, "xadvance");

            chars[c.id] = c;
        }
    }
}

// Fungsi helper untuk mengambil angka dari string seperti "x=10"
int BitmapFont::ParseValue(const std::string& line, const std::string& key)
{
    std::string search = key + "=";
    size_t pos = line.find(search);
    if (pos == std::string::npos) return 0;

    size_t start = pos + search.length();
    size_t end = line.find(" ", start);
    if (end == std::string::npos) end = line.length();

    return std::stoi(line.substr(start, end - start));
}

void BitmapFont::Draw(const std::string& text, float startX, float startY, float scale, float r, float g, float b, float a)
{
    if (!sprite) return;

    // Ambil Context untuk render
    auto dc = Graphics::Instance().GetDeviceContext();

    float cursorX = startX;
    float cursorY = startY;

    // Jarak antar baris (bisa disesuaikan manual atau ambil dari common lineHeight di .fnt)
    float lineHeight = 38.0f * scale;

    for (size_t offset = 0; offset < text.size(); )
    {
        const size_t nextOffset = NextUtf8Offset(text, offset);
        const int id = DecodeUtf8Codepoint(text, offset, nextOffset);
        offset = nextOffset;

        if (id == '\n') // Handle Enter/Baris Baru
        {
            cursorX = startX;
            cursorY += lineHeight;
            continue;
        }

        // Jika huruf tidak ada di data, lewati
        if (chars.find(id) == chars.end()) continue;

        CharData& data = chars[id];

        // Hitung posisi visual di layar
        float drawX = cursorX + (data.xoffset * scale);
        float drawY = cursorY + (data.yoffset * scale);

        // --- INI BAGIAN PENTINGNYA ---
        // Kita panggil fungsi Render kamu yang support texture slicing (sx, sy, sw, sh)
        sprite->Render(
            dc,
            drawX, drawY, 0.0f,                 // dx, dy, dz (Posisi Layar)
            (float)data.width * scale,          // dw (Lebar di Layar)
            (float)data.height * scale,         // dh (Tinggi di Layar)
            (float)data.x, (float)data.y,       // sx, sy (Posisi Potong di Texture)
            (float)data.width, (float)data.height, // sw, sh (Ukuran Potong di Texture)
            0.0f,                               // Angle
            r, g, b, a                          // Warna
        );

        // Geser kursor X untuk huruf berikutnya
        cursorX += data.xadvance * scale;
    }
}

void BitmapFont::Draw3D(const std::string& text, const Camera* camera,
    DirectX::XMFLOAT3 worldPos, float scale, DirectX::XMFLOAT3 rotation, DirectX::XMFLOAT4 color)
{
    if (!sprite) return;
    auto dc = Graphics::Instance().GetDeviceContext();

    // -------------------------------------------------------------
    // [FIX BLENDING] AKTIFKAN TRANSPARANSI
    // -------------------------------------------------------------
    auto rs = Graphics::Instance().GetRenderState();

    // 1. Aktifkan Alpha Blending (Agar background hitam hilang)
    dc->OMSetBlendState(rs->GetBlendState(BlendState::Transparency), nullptr, 0xFFFFFFFF);

    // 2. (Opsional tapi PENTING untuk 3D Text) Matikan Z-Write 
    // Agar kotak transparan teks tidak "membolongi" objek di belakangnya
    // Gunakan DepthRead (Test ON, Write OFF) kalau ada, atau biarkan default dulu.
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestOnly), 0);

    // -------------------------------------------------------------

    using namespace DirectX;

    // Matrix Rotasi untuk seluruh teks
    XMMATRIX matRot = XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);

    // Kursor awal (lokal)
    float cursorX = 0.0f;
    float cursorY = 0.0f;
    float lineHeight = 38.0f * scale;

    // Untuk centering text (Opsional, kalau mau teks rata tengah di worldPos)
    // DirectX::XMFLOAT2 size = MeasureText(text, scale);
    // cursorX = -size.x / 2.0f; 

    for (size_t offset = 0; offset < text.size(); )
    {
        const size_t nextOffset = NextUtf8Offset(text, offset);
        const int id = DecodeUtf8Codepoint(text, offset, nextOffset);
        offset = nextOffset;

        if (id == '\n')
        {
            cursorX = 0.0f; // Reset X
            cursorY -= lineHeight; // Di 3D, Y ke bawah itu negatif
            continue;
        }

        if (chars.find(id) == chars.end()) continue;

        CharData& data = chars[id];

        // 1. Hitung Ukuran Huruf di Dunia
        float w = data.width * scale;
        float h = data.height * scale;

        // 2. Hitung Offset Lokal (Jarak dari titik tumpu teks ke huruf ini)
        // Di 2D: Y positif ke bawah. Di 3D: Y positif ke atas. Kita balik Y-nya.
        float localX = cursorX + (data.xoffset * scale) + (w / 2.0f); // +w/2 karena Sprite Render pivotnya di tengah
        float localY = cursorY - (data.yoffset * scale) - (h / 2.0f);

        // 3. Transformasi Posisi Lokal -> Posisi World (Sesuai Rotasi Teks)
        XMFLOAT3 localPos = { localX, localY, 0.0f };
        XMVECTOR vLocal = XMLoadFloat3(&localPos);
        vLocal = XMVector3TransformCoord(vLocal, matRot); // Putar offsetnya

        // Tambahkan ke posisi asal teks
        XMFLOAT3 finalPos;
        XMStoreFloat3(&finalPos, vLocal + XMLoadFloat3(&worldPos));

        // 4. Render Huruf
        sprite->Render3D(dc, camera,
            finalPos.x, finalPos.y, finalPos.z,
            w, h,
            (float)data.x, (float)data.y,
            (float)data.width, (float)data.height,
            rotation.x, rotation.y, rotation.z, // Huruf ikut rotasi teks
            color.x, color.y, color.z, color.w
        );

        // 5. Geser Kursor
        cursorX += data.xadvance * scale;
    }
}

DirectX::XMFLOAT2 BitmapFont::MeasureText(const std::string& text, float scale)
{
    float width = 0.0f;
    float maxWidth = 0.0f;
    int lineCount = 1;
    float lineHeight = 38.0f * scale; // Hardcoded sesuai Draw() kamu. Idealnya ini dibaca dari file .fnt ("common lineHeight")

    // Kalau kosong, return 0
    if (text.empty()) return { 0.0f, 0.0f };

    // Hitung Lebar
    for (size_t offset = 0; offset < text.size(); )
    {
        const size_t nextOffset = NextUtf8Offset(text, offset);
        const int id = DecodeUtf8Codepoint(text, offset, nextOffset);
        offset = nextOffset;

        if (id == '\n')
        {
            maxWidth = (std::max)(maxWidth, width);
            width = 0.0f;
            ++lineCount;
            continue;
        }

        if (chars.find(id) != chars.end())
        {
            // xadvance adalah jarak kursor berpindah ke huruf selanjutnya
            width += chars[id].xadvance * scale;
        }
    }
    maxWidth = (std::max)(maxWidth, width);

    // Untuk tinggi, tombol biasanya cuma 1 baris, jadi kita return tinggi font default
    // Kalau mau support multiline, logicnya harus hitung berapa kali '\n' muncul.
    return { maxWidth, lineHeight * static_cast<float>(lineCount) };
}
