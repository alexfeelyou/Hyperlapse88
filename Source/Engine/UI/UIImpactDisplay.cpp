#include "UIImpactDisplay.h"
#include <algorithm>
#include <cmath>

UIImpactDisplay::UIImpactDisplay() {}
UIImpactDisplay::~UIImpactDisplay() {}

void UIImpactDisplay::Initialize(ID3D11Device* device)
{
    // Init Primitive untuk Flash & Background
    m_primitive = std::make_unique<Primitive>(device);

    auto LoadSprite = [&](ImpactType type, const char* path) {
        auto sprite = std::make_unique<Sprite>(device, path);
        // Sesuaikan ukuran sprite (Contoh 1920x1080 atau sesuai gambar aslimu)
        // Kalau gambarmu kecil, sesuaikan w dan h ini agar tidak gepeng
        float w = 1920.0f;
        float h = 1080.0f;
        m_sprites[type] = std::move(sprite);
        m_spriteDims[type] = { w, h };
        };

    // Load Sprite Text
    LoadSprite(ImpactType::Super, "Data/Sprite/Scene Breaker/Sprite_Text_ESCAPE.png");
    LoadSprite(ImpactType::Nigero, "Data/Sprite/Scene Breaker/Sprite_Text_NIGERO.png");
    LoadSprite(ImpactType::SpaceKeyJP, "Data/Sprite/Scene Breaker/Sprite_Text_SPACEKEY_jp.png");
    LoadSprite(ImpactType::RendaSeyo, "Data/Sprite/Scene Breaker/Sprite_Text_RENDASEYO.png");
    LoadSprite(ImpactType::Bougyo, "Data/Sprite/Scene Breaker/Sprite_Text_BOUGYO.png");
    LoadSprite(ImpactType::Kougeki, "Data/Sprite/Scene Breaker/Sprite_Text_KOUGEKI.png");
    // Pastikan status awal bersih
    ResetAnim();
}

void UIImpactDisplay::Show(ImpactType type, float duration)
{
    if (m_sprites.find(type) == m_sprites.end()) return;

    m_anim.isActive = true;
    m_anim.timer = 0.0f;
    m_anim.totalDuration = duration;
    m_anim.currentType = type;

    // Setup Animation
    m_anim.currentScale = 5.0f; // Mulai dari sangat besar
    m_anim.currentAlpha = 1.0f;
    m_anim.currentRotation = 0.0f;
    m_anim.flashAlpha = 0.5f;   // Kilatan cahaya di awal
}

void UIImpactDisplay::ShowSequence(const std::vector<ImpactEvent>& sequence)
{
    // Kosongkan antrian lama
    m_eventQueue.clear();
    // Isi antrian baru
    for (const auto& evt : sequence) {
        m_eventQueue.push_back(evt);
    }

    // Jika sedang diam, langsung mainkan yang pertama
    if (!m_anim.isActive && !m_eventQueue.empty()) {
        ImpactEvent next = m_eventQueue.front();
        m_eventQueue.pop_front();
        Show(next.type, next.duration);
    }
}

void UIImpactDisplay::Update(float elapsedTime)
{
    // [LOGIKA ANTRIAN]
    // Jika animasi sekarang mati/selesai, cek apakah ada antrian berikutnya?
    if (!m_anim.isActive)
    {
        if (!m_eventQueue.empty())
        {
            // Ambil antrian paling depan
            ImpactEvent next = m_eventQueue.front();
            m_eventQueue.pop_front();

            // Mainkan!
            Show(next.type, next.duration);
        }
        return;
    }

    m_anim.timer += elapsedTime;

    // Logic Flash (Kilatan Putih)
    if (m_anim.flashAlpha > 0.0f)
    {
        m_anim.flashAlpha -= elapsedTime * 8.0f;
        if (m_anim.flashAlpha < 0.0f) m_anim.flashAlpha = 0.0f;
    }

    float impactTime = 0.15f; // Waktu "Slam"

    if (m_anim.timer < impactTime)
    {
        // FASE 1: SLAM (Membesar ke Normal dengan cepat)
        float t = m_anim.timer / impactTime;
        float k = 15.0f;
        float scaleFactor = exp(-k * t);
        m_anim.currentScale = 1.0f + (4.0f * scaleFactor);
    }
    else if (m_anim.timer < m_anim.totalDuration)
    {
        // FASE 2: SLOW ZOOM OUT (Pelan banget menjauh)
        float timeAfterImpact = m_anim.timer - impactTime;
        float zoomSpeed = 0.05f;
        m_anim.currentScale = 1.0f - (timeAfterImpact * zoomSpeed);

        // Fade out di akhir durasi
        float timeLeft = m_anim.totalDuration - m_anim.timer;
        if (timeLeft < 0.2f)
        {
            m_anim.currentAlpha = timeLeft / 0.2f;
        }
    }
    else
    {
        // Animasi Selesai -> Reset
        ResetAnim();
    }
}

// === FUNGSI YANG HILANG (PENYEBAB ERROR) ADA DI BAWAH INI ===

void UIImpactDisplay::Render(ID3D11DeviceContext* dc)
{
    if (!m_anim.isActive) return;

    // 1. LAYER BACKGROUND (Hitam Transparan)
    float bgAlpha = 0.3f;
    if (m_anim.currentAlpha < 1.0f) {
        bgAlpha *= m_anim.currentAlpha;
    }

    // Gambar Kotak Hitam Fullscreen
    m_primitive->Rect(0.0f, 0.0f, m_screenW, m_screenH, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, bgAlpha);
    m_primitive->Render(dc);

    // 2. LAYER FLASH (Putih Transparan)
    if (m_anim.flashAlpha > 0.0f)
    {
        m_primitive->Rect(0.0f, 0.0f, m_screenW, m_screenH, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, m_anim.flashAlpha);
        m_primitive->Render(dc);
    }

    // 3. LAYER TEXT (Sprite)
    auto it = m_sprites.find(m_anim.currentType);
    if (it != m_sprites.end())
    {
        Sprite* sprite = it->second.get();
        SpriteDim dim = m_spriteDims[m_anim.currentType];

        float finalW = dim.w * m_anim.currentScale;
        float finalH = dim.h * m_anim.currentScale;

        // Hitung Center Screen
        float centerX = m_screenW * 0.5f;
        float centerY = m_screenH * 0.5f;

        float dx = centerX - (finalW * 0.5f);
        float dy = centerY - (finalH * 0.5f);

        sprite->Render(dc,
            dx, dy, 0.0f,
            finalW, finalH,
            m_anim.currentRotation,
            1.0f, 1.0f, 1.0f, m_anim.currentAlpha
        );
    }
}

void UIImpactDisplay::OnResize(float screenW, float screenH)
{
    m_screenW = screenW;
    m_screenH = screenH;
}

void UIImpactDisplay::ResetAnim()
{
    m_anim.isActive = false;
    m_anim.flashAlpha = 0.0f;
    m_anim.currentScale = 1.0f;
    m_anim.currentAlpha = 1.0f;
}

bool UIImpactDisplay::IsActive() const
{
    return m_anim.isActive;
}

float UIImpactDisplay::GetDistortionKick() const
{
    if (!m_anim.isActive) return 0.0f;
    if (m_anim.currentType == ImpactType::Bougyo || m_anim.currentType == ImpactType::Kougeki)
    {
        return 0.0f;
    }
   
    float kickDuration = 0.2f; 

    if (m_anim.timer < kickDuration)
    {
        return exp(-20.0f * m_anim.timer);
    }

    return 0.0f;
}