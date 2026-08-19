#include "WindowPool.h"
#include <SDL3/SDL.h>

namespace Beyond
{
    bool WindowPool::Acquire(
        WindowTrackingSystem& trackingSystem,
        const TrackedWindowConfig& config,
        std::function<DirectX::XMFLOAT3()> getTargetPos,
        std::function<DirectX::XMFLOAT2()> getTargetSize
    )
    {
        if (m_pool.empty())
        {
            return false; // Pool kosong, beri tahu pemanggil untuk membuat baru
        }

        // Ambil satu dari gudang
        auto recycled = std::move(m_pool.back());
        m_pool.pop_back();

        // Reset data
        recycled->name = config.name;
        recycled->trackingOffset = config.trackingOffset;
        recycled->getTargetPositionFunc = getTargetPos;
        recycled->getTargetSizeFunc = getTargetSize;

        if (recycled->window)
        {
            recycled->window->SetTitle(config.title.c_str());
            if (config.fpsLimit > 0.0f)
            {
                recycled->window->SetTargetFPS(config.fpsLimit);
            }

            // Tampilkan kembali
            SDL_ShowWindow(recycled->window->GetSDLWindow());
        }

        // Masukkan kembali ke sistem tracking agar mulai dihitung posisinya
        trackingSystem.RestoreFromPool(std::move(recycled));

        return true;
    }

    void WindowPool::Release(WindowTrackingSystem& trackingSystem, const std::string& name)
    {
        // Minta tracking system untuk melepaskan kepemilikan window ini
        std::unique_ptr<TrackedWindow> extractedWindow = trackingSystem.ExtractForPool(name);

        if (extractedWindow)
        {
            if (extractedWindow->window)
            {
                // Sembunyikan window
                SDL_HideWindow(extractedWindow->window->GetSDLWindow());
            }

            // Masukkan ke gudang
            m_pool.push_back(std::move(extractedWindow));
        }
    }

    void WindowPool::Clear()
    {
        // Karena vector berisi unique_ptr, clear() otomatis akan memanggil destructor 
        // TrackedWindow, yang pada gilirannya harus menghancurkan Beyond::Window (pastikan ini terjadi).
        m_pool.clear();
    }
}