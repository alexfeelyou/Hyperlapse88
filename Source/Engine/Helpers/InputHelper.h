#pragma once
#include <DirectXMath.h>

namespace Beyond {
    class InputHelper {
    public:
        // Mengubah posisi mouse global menjadi koordinat dunia game
        // cameraPos: Posisi kamera saat ini (penting untuk SceneGame yang kameranya bergerak)
        static DirectX::XMFLOAT3 GetMouseWorldPos(const DirectX::XMFLOAT3& cameraPos);
    };
}