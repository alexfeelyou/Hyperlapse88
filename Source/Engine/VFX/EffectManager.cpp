#include "EffectManager.h"
#include "Camera.h" 
#include <mutex>

using namespace DirectX;

void EffectManager::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    // Create Renderer 
    m_renderer = EffekseerRendererDX11::Renderer::Create(device, context, 2048);

    // Create Manager
    m_manager = Effekseer::Manager::Create(2048);

    // Bind Default Loaders
    m_manager->SetSpriteRenderer(m_renderer->CreateSpriteRenderer());
    m_manager->SetRibbonRenderer(m_renderer->CreateRibbonRenderer());
    m_manager->SetRingRenderer(m_renderer->CreateRingRenderer());
    m_manager->SetTrackRenderer(m_renderer->CreateTrackRenderer());
    m_manager->SetModelRenderer(m_renderer->CreateModelRenderer());

    m_manager->SetTextureLoader(m_renderer->CreateTextureLoader());
    m_manager->SetModelLoader(m_renderer->CreateModelLoader());
    m_manager->SetMaterialLoader(m_renderer->CreateMaterialLoader());

    // Engine Sync: DirectX is Left-Handed, so we must tell Effekseer to use LH math
    m_manager->SetCoordinateSystem(Effekseer::CoordinateSystem::LH);
}

void EffectManager::Finalize()
{
    StopAll();
    m_effectCache.clear();
}

void EffectManager::Update(float elapsedTime)
{
    if (m_manager == nullptr) return;

    m_manager->Update(elapsedTime * 60.0f);
}

void EffectManager::Render(Camera* camera)
{
    if (m_renderer == nullptr || m_manager == nullptr || camera == nullptr) return;

    // Convert DirectX matrices to Effekseer matrices safely
    XMFLOAT4X4 view = camera->GetView();
    XMFLOAT4X4 proj = camera->GetProjection();

    m_renderer->SetCameraMatrix(*reinterpret_cast<const Effekseer::Matrix44*>(&view));
    m_renderer->SetProjectionMatrix(*reinterpret_cast<const Effekseer::Matrix44*>(&proj));

    m_renderer->BeginRendering();
    m_manager->Draw();
    m_renderer->EndRendering();
}

std::u16string EffectManager::ConvertToUTF16(const std::string& str) const
{
    std::u16string utf16Str;
    utf16Str.resize(str.length() + 1); // +1 for safety padding
    Effekseer::ConvertUtf8ToUtf16(utf16Str.data(), static_cast<int>(utf16Str.size()), str.c_str());
    utf16Str.push_back(u'\0'); // Ensure null termination
    return utf16Str;
}

void EffectManager::PreloadEffect(const std::string& filePath)
{
    // EARLY EXIT: Don't load it if it's already in the RAM cache
    if (m_effectCache.find(filePath) != m_effectCache.end()) return;

    // Thread Safety Lock (Uncomment if using async loading)
    // std::lock_guard<std::mutex> lock(Graphics::Instance().GetMutex());

    std::u16string utf16Path{ ConvertToUTF16(filePath) };
    Effekseer::EffectRef effect{ Effekseer::Effect::Create(m_manager, utf16Path.c_str()) };

    if (effect != nullptr)
    {
        m_effectCache[filePath] = effect; // Cache it permanently
    }
    else
    {
        std::string errMsg = "[EFFEKSEER ERROR] Gagal meload file (Cek path, tekstur, atau versi Effekseer!): " + filePath + "\n";
        OutputDebugStringA(errMsg.c_str());
    }
}

Effekseer::Handle EffectManager::Play(const std::string& filePath, const XMFLOAT3& pos, float scale)
{
    PreloadEffect(filePath);

    auto it{ m_effectCache.find(filePath) };
    if (it == m_effectCache.end()) {
        std::string errMsg = "[EFFEKSEER ERROR] File tidak ada di cache (Preload gagal sebelumnya): " + filePath + "\n";
        OutputDebugStringA(errMsg.c_str());
        return -1;
    }

    // Play the cached resource
    Effekseer::Handle handle{ m_manager->Play(it->second, pos.x, pos.y, pos.z) };

    if (handle == -1) {
        OutputDebugStringA("[EFFEKSEER ERROR] Manager gagal memutar efek (Handle = -1)!\n");
    }
    else {
        // Apply initial scale only if successfully played
        m_manager->SetScale(handle, scale, scale, scale);
    }

    return handle;
}

void EffectManager::SetTargetPosition(Effekseer::Handle handle, const XMFLOAT3& pos)
{
    if (IsPlaying(handle)) m_manager->SetTargetLocation(handle, pos.x, pos.y, pos.z);
}

bool EffectManager::IsPlaying(Effekseer::Handle handle) const
{
    if (handle < 0) return false;
    return m_manager->Exists(handle);
}

void EffectManager::Stop(Effekseer::Handle handle)
{
    if (IsPlaying(handle)) m_manager->StopEffect(handle);
}

void EffectManager::StopAll()
{
    if (m_manager != nullptr) m_manager->StopAllEffects();
}

void EffectManager::SetPosition(Effekseer::Handle handle, const XMFLOAT3& pos)
{
    if (IsPlaying(handle)) m_manager->SetLocation(handle, pos.x, pos.y, pos.z);
}

void EffectManager::SetRotation(Effekseer::Handle handle, const XMFLOAT3& rotEuler)
{
    if (IsPlaying(handle)) m_manager->SetRotation(handle, rotEuler.x, rotEuler.y, rotEuler.z);
}

void EffectManager::SetScale(Effekseer::Handle handle, const XMFLOAT3& scale)
{
    if (IsPlaying(handle)) m_manager->SetScale(handle, scale.x, scale.y, scale.z);
}