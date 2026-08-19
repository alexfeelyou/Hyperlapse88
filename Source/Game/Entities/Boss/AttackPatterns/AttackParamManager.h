#pragma once
#include <string>
#include <json.hpp>

// --- Phase 1 Attacks ---
#include "AttackRadial.h"
#include "AttackFan.h"
#include "AttackPhalanx.h"
#include "AttackRain.h"
#include "AttackUltimate.h"
#include "AttackWave.h"
#include "AttackMeteor.h"
#include "AttackDirect.h"

// --- Phase 2 Attacks ---
#include "AttackBouncing.h"
#include "AttackBoomerangs.h"
#include "AttackBlasters.h"
#include "AttackSpears.h"

using json = nlohmann::json;

class AttackParamManager {
public:
    static AttackParamManager& Instance() {
        static AttackParamManager instance;
        return instance;
    }

    // Memuat ulang data dari file JSON
    bool Load(const std::string& filepath);

    // ========================================================
    // GETTER: PHASE 01
    // ========================================================
    RadialParams& GetRadialNormalParams() { return m_radialNormal; }
    RadialParams& GetRadialContinuousParams() { return m_radialContinuous; }
    FanParams& GetFanNormalParams() { return m_fanNormal; }
    FanParams& GetFanContinuousParams() { return m_fanContinuous; }
    PhalanxParams& GetPhalanxParams() { return m_phalanx; }
    RainParams& GetRainParams() { return m_rain; }
    RainParams& GetRainTargetedParams() { return m_rainTargeted; } // <--- TAMBAH INI
    UltimateParams& GetUltimateParams() { return m_ultimate; }
	WaveParams& GetWaveParams() { return m_wave; }
	MeteorParams& GetMeteorParams() { return m_meteor; }
    DirectParams& GetDirectParams() { return m_direct; }

    // ========================================================
    // GETTER: PHASE 02 (WINDOWKILL)
    // ========================================================
    BouncingBulletParams& GetBouncingParams() { return m_bouncing; }
    BoomerangParams& GetBoomerangParams() { return m_boomerang; }
    BlasterParams& GetBlasterParams() { return m_blaster; }
    UndyneSpearParams& GetUndyneParams() { return m_spear; }

private:
    AttackParamManager() = default;

    // Fungsi Internal Parser
    void ParseRadialParams(const json& j, RadialParams& outParams);
    void ParseFanParams(const json& j, FanParams& outParams);
    void ParsePhalanxParams(const json& j, PhalanxParams& outParams);
    void ParseRainParams(const json& j, RainParams& outParams);
    void ParseUltimateParams(const json& j, UltimateParams& outParams);
	void ParseWaveParams(const json& j, WaveParams& outParams);
	void ParseMeteorParams(const json& j, MeteorParams& outParams);
    void ParseDirectParams(const json& j, DirectParams& outParams);

    void ParseBouncingParams(const json& j, BouncingBulletParams& outParams);
    void ParseBoomerangParams(const json& j, BoomerangParams& outParams);
    void ParseBlasterParams(const json& j, BlasterParams& outParams);
    void ParseSpearParams(const json& j, UndyneSpearParams& outParams);

    // Data Storage (Phase 01)
    RadialParams   m_radialNormal;
    RadialParams   m_radialContinuous;
    FanParams      m_fanNormal;
    FanParams      m_fanContinuous;
    PhalanxParams  m_phalanx;
    RainParams     m_rain;
    RainParams     m_rainTargeted;
    UltimateParams m_ultimate;
	WaveParams	   m_wave;
	MeteorParams   m_meteor;
    DirectParams   m_direct;

    // Data Storage (Phase 02)
    BouncingBulletParams m_bouncing;
    BoomerangParams      m_boomerang;
    BlasterParams        m_blaster;
    UndyneSpearParams    m_spear;
};