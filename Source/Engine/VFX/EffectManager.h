#pragma once

#include <DirectXMath.h>
#include <Effekseer.h>
#include <EffekseerRendererDX11.h>
#include <string>
#include <unordered_map>
#include <memory>

class Camera; 

class EffectManager
{
public:
    // Thread-safe Singleton access
    static EffectManager& Instance()
    {
        static EffectManager instance;
        return instance;
    }

    // Explicitly delete copy/move semantics to prevent catastrophic engine duplication
    EffectManager(const EffectManager&) = delete;
    EffectManager& operator=(const EffectManager&) = delete;

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Finalize();

    void Update(float elapsedTime);
    void Render(Camera* camera);

    // ==========================================
    // RESOURCE MANAGEMENT (The RAM Saver)
    // ==========================================
    // Preloads an effect into memory without playing it
    void PreloadEffect(const std::string& filePath);

    // ==========================================
    // PLAYBACK & CONTROL
    // ==========================================
    // Plays an effect and returns a lightweight handle for future control
    Effekseer::Handle Play(const std::string& filePath, const DirectX::XMFLOAT3& pos, float scale = 1.0f);

    void Stop(Effekseer::Handle handle);
    void StopAll();

    bool IsPlaying(Effekseer::Handle handle) const;

    // Transform Updates (Only applies if the handle is still actively playing!)
    void SetPosition(Effekseer::Handle handle, const DirectX::XMFLOAT3& pos);
    void SetTargetPosition(Effekseer::Handle handle, const DirectX::XMFLOAT3& pos); // [NEW] Fix untuk partikel nyasar!
    void SetRotation(Effekseer::Handle handle, const DirectX::XMFLOAT3& rotationEuler);
    void SetScale(Effekseer::Handle handle, const DirectX::XMFLOAT3& scale);

private:
    EffectManager() = default;
    ~EffectManager() = default;

    // The Effekseer Core Pointers 
    Effekseer::ManagerRef m_manager{};
    EffekseerRenderer::RendererRef m_renderer{};

    // The Flyweight Cache: Maps file paths to loaded memory blocks
    std::unordered_map<std::string, Effekseer::EffectRef> m_effectCache{};

    // Helper to safely convert standard strings to Effekseer's required UTF-16 format
    std::u16string ConvertToUTF16(const std::string& str) const;
};