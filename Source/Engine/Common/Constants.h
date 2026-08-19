#pragma once

namespace Beyond {
    namespace Config {
        // Skala dunia: 40 pixel di monitor = 1 unit di dunia 3D
        constexpr float PIXEL_TO_UNIT_RATIO = 40.0f;

        // Konstanta kamera standar
        constexpr float CAM_FOV = 60.0f;
        constexpr float CAM_NEAR = 0.1f;
        constexpr float CAM_FAR = 1000.0f;
    }
}