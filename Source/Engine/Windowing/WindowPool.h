#pragma once

#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <DirectXMath.h>

// Forward declaration karena kita akan mendefinisikan ini di tempat lain, atau
// kita asumsikan kamu akan memindahkan definisi struktur ini ke header global / terpisah nanti.
// Untuk saat ini, kita include WindowTrackingSystem.h agar tipe datanya dikenali.
#include "WindowTrackingSystem.h" 

namespace Beyond
{
    class WindowPool
    {
    public:
        // Singleton pattern agar pool ini bisa diakses dari mana saja tanpa dilempar-lempar
        static WindowPool& Instance()
        {
            static WindowPool instance;
            return instance;
        }

        // --- CORE FUNCTIONS ---
        // Mencoba mengambil window dari pool. Mengembalikan true jika berhasil, false jika pool kosong.
        bool Acquire(
            WindowTrackingSystem& trackingSystem,
            const TrackedWindowConfig& config,
            std::function<DirectX::XMFLOAT3()> getTargetPos,
            std::function<DirectX::XMFLOAT2()> getTargetSize = nullptr
        );

        // Memasukkan window kembali ke pool (menyembunyikannya)
        void Release(WindowTrackingSystem& trackingSystem, const std::string& name);

        // Membersihkan seluruh gudang (dipanggil saat pindah scene/reset)
        void Clear();

    private:
        WindowPool() = default;
        ~WindowPool() = default;
        WindowPool(const WindowPool&) = delete;
        WindowPool& operator=(const WindowPool&) = delete;

        // "Gudang" penyimpanan
        std::vector<std::unique_ptr<TrackedWindow>> m_pool;
    };
}