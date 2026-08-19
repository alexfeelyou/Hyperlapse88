#include "AttackParamManager.h"
#include <fstream>
#include <iostream>

bool AttackParamManager::Load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[AttackParamManager] Failed to open: " << filepath << "\n";
        return false;
    }

    json j;
    try {
        j = json::parse(file, nullptr, true, true);
    }
    catch (json::parse_error& e) {
        std::cerr << "[AttackParamManager] JSON Parse Error: " << e.what() << "\n";
        return false;
    }

    // --- PARSE PHASE 01 ---
    if (j.contains("Phase01")) {
        const auto& p1 = j["Phase01"];
        if (p1.contains("RadialNormal"))     ParseRadialParams(p1["RadialNormal"], m_radialNormal);
        if (p1.contains("RadialContinuous")) ParseRadialParams(p1["RadialContinuous"], m_radialContinuous);
        if (p1.contains("FanNormal"))        ParseFanParams(p1["FanNormal"], m_fanNormal);
        if (p1.contains("FanContinuous"))    ParseFanParams(p1["FanContinuous"], m_fanContinuous);
        if (p1.contains("Phalanx"))          ParsePhalanxParams(p1["Phalanx"], m_phalanx);
        if (p1.contains("Rain"))             ParseRainParams(p1["Rain"], m_rain);
        if (p1.contains("RainTargeted"))     ParseRainParams(p1["RainTargeted"], m_rainTargeted); // <-- TAMBAH INI
        if (p1.contains("Ultimate"))         ParseUltimateParams(p1["Ultimate"], m_ultimate);
        if (p1.contains("Wave"))             ParseWaveParams(p1["Wave"], m_wave);
		if (p1.contains("Meteor"))           ParseMeteorParams(p1["Meteor"], m_meteor);
        if (p1.contains("Direct"))           ParseDirectParams(p1["Direct"], m_direct);
    }

    // --- PARSE PHASE 02 ---
    if (j.contains("Phase02")) {
        const auto& p2 = j["Phase02"];
        if (p2.contains("Bouncing"))         ParseBouncingParams(p2["Bouncing"], m_bouncing);
        if (p2.contains("Boomerang"))        ParseBoomerangParams(p2["Boomerang"], m_boomerang);
        if (p2.contains("Blaster"))          ParseBlasterParams(p2["Blaster"], m_blaster);
        if (p2.contains("Spear"))            ParseSpearParams(p2["Spear"], m_spear);
    }

    std::cout << "[AttackParamManager] Parameters successfully loaded from " << filepath << "\n";
    return true;
}

// ========================================================
// PARSER IMPLEMENTATIONS (PHASE 01)
// ========================================================
void AttackParamManager::ParseRadialParams(const json& j, RadialParams& out) {
    if (j.contains("count")) out.count = j["count"];
    if (j.contains("speed")) out.speed = j["speed"];
    if (j.contains("burstDelay")) out.burstDelay = j["burstDelay"];
    if (j.contains("burstCount")) out.burstCount = j["burstCount"];
    if (j.contains("damage")) out.damage = j["damage"];
    if (j.contains("sfxVolume")) out.sfxVolume = j["sfxVolume"];
    if (j.contains("activeDuration")) out.activeDuration = j["activeDuration"];
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4) {
        out.color = { j["color"][0], j["color"][1], j["color"][2], j["color"][3] };
    }
}

void AttackParamManager::ParseFanParams(const json& j, FanParams& out) {
    if (j.contains("rows")) out.rows = j["rows"];
    if (j.contains("waves")) out.waves = j["waves"];
    if (j.contains("waveDelay")) out.waveDelay = j["waveDelay"];
    if (j.contains("spreadAngle")) out.spreadAngle = j["spreadAngle"];
    if (j.contains("speed")) out.speed = j["speed"];
    if (j.contains("damage")) out.damage = j["damage"];
    if (j.contains("sfxVolume")) out.sfxVolume = j["sfxVolume"];
    if (j.contains("triggerCount")) out.triggerCount = j["triggerCount"];
    if (j.contains("triggerDelay")) out.triggerDelay = j["triggerDelay"];
}

void AttackParamManager::ParsePhalanxParams(const json& j, PhalanxParams& out) {
    if (j.contains("count")) out.count = j["count"];
    if (j.contains("chargeDelay")) out.chargeDelay = j["chargeDelay"];
    if (j.contains("holdDuration")) out.holdDuration = j["holdDuration"];
    if (j.contains("fireDelay")) out.fireDelay = j["fireDelay"];
    if (j.contains("speed")) out.speed = j["speed"];
    if (j.contains("hoverRadius")) out.hoverRadius = j["hoverRadius"];
    if (j.contains("turnSpeed")) out.turnSpeed = j["turnSpeed"];
    if (j.contains("smoothSpeed")) out.smoothSpeed = j["smoothSpeed"];
    if (j.contains("postFireDelay")) out.postFireDelay = j["postFireDelay"];
    if (j.contains("attackMoveSpeed")) out.attackMoveSpeed = j["attackMoveSpeed"];
    if (j.contains("returnMoveSpeed")) out.returnMoveSpeed = j["returnMoveSpeed"];
    if (j.contains("damage")) out.damage = j["damage"];
    if (j.contains("sfxVolume")) out.sfxVolume = j["sfxVolume"];
}

void AttackParamManager::ParseRainParams(const json& j, RainParams& out) {
    if (j.contains("minSpeed")) out.minSpeed = j["minSpeed"];
    if (j.contains("maxSpeed")) out.maxSpeed = j["maxSpeed"];
    if (j.contains("warningDuration")) out.warningDuration = j["warningDuration"];
    if (j.contains("activeDuration")) out.activeDuration = j["activeDuration"];
    if (j.contains("width")) out.width = j["width"];
    if (j.contains("depth")) out.depth = j["depth"];
    if (j.contains("damage")) out.damage = j["damage"];
    if (j.contains("sfxVolume")) out.sfxVolume = j["sfxVolume"];

    if (j.contains("triggerCount")) out.triggerCount = j["triggerCount"];
    if (j.contains("triggerDelay")) out.triggerDelay = j["triggerDelay"];
}

void AttackParamManager::ParseUltimateParams(const json& j, UltimateParams& out) {
    if (j.contains("laserDuration")) out.laserDuration = j["laserDuration"];
    if (j.contains("shootSpeed")) out.shootSpeed = j["shootSpeed"];
    // (Bisa kamu tambah field lain jika ada)
    if (j.contains("ballColor") && j["ballColor"].is_array() && j["ballColor"].size() == 4) {
        out.ballColor = { j["ballColor"][0], j["ballColor"][1], j["ballColor"][2], j["ballColor"][3] };
    }
}

void AttackParamManager::ParseWaveParams(const json& j, WaveParams& out) {
    if (j.contains("trackCount")) out.trackCount = j["trackCount"];
    if (j.contains("bulletsPerWave")) out.bulletsPerWave = j["bulletsPerWave"];
    if (j.contains("waves")) out.waves = j["waves"];
    if (j.contains("waveDelay")) out.waveDelay = j["waveDelay"];
    if (j.contains("speed")) out.speed = j["speed"];
    if (j.contains("trackSpacing")) out.trackSpacing = j["trackSpacing"];
    if (j.contains("startZ")) out.startZ = j["startZ"];
    if (j.contains("spawnX")) out.spawnX = j["spawnX"];
    if (j.contains("damage")) out.damage = j["damage"];
    if (j.contains("sfxVolume")) out.sfxVolume = j["sfxVolume"];
}

void AttackParamManager::ParseMeteorParams(const json& j, MeteorParams& out) {
    if (j.contains("count")) out.count = j["count"];
    if (j.contains("spawnDelay")) out.spawnDelay = j["spawnDelay"];
    if (j.contains("speed")) out.speed = j["speed"];
    if (j.contains("speedVariance")) out.speedVariance = j["speedVariance"];
    if (j.contains("radius")) out.radius = j["radius"];
    if (j.contains("visualScale")) out.visualScale = j["visualScale"];

    // Konfigurasi Anchor
    if (j.contains("startX")) out.startX = j["startX"];
    if (j.contains("startZ")) out.startZ = j["startZ"];
    if (j.contains("targetX")) out.targetX = j["targetX"];
    if (j.contains("targetZ")) out.targetZ = j["targetZ"];

    if (j.contains("spreadOffset")) out.spreadOffset = j["spreadOffset"];
    if (j.contains("damage")) out.damage = j["damage"];
    if (j.contains("sfxVolume")) out.sfxVolume = j["sfxVolume"];
}

void AttackParamManager::ParseDirectParams(const json& j, DirectParams& out) {
    if (j.contains("count")) out.count = j["count"];
    if (j.contains("spawnDelay")) out.spawnDelay = j["spawnDelay"];
    if (j.contains("speed")) out.speed = j["speed"];
    if (j.contains("radius")) out.radius = j["radius"];
    if (j.contains("visualScale")) out.visualScale = j["visualScale"];
    if (j.contains("damage")) out.damage = j["damage"];
    if (j.contains("sfxVolume")) out.sfxVolume = j["sfxVolume"];
    if (j.contains("triggerCount")) out.triggerCount = j["triggerCount"];
    if (j.contains("triggerDelay")) out.triggerDelay = j["triggerDelay"];
}

// ========================================================
// PARSER IMPLEMENTATIONS (PHASE 02 - WINDOWKILL)
// ========================================================
void AttackParamManager::ParseBouncingParams(const json& j, BouncingBulletParams& out) {
    if (j.contains("spawnCount")) out.spawnCount = j["spawnCount"];
    if (j.contains("spawnDelay")) out.spawnDelay = j["spawnDelay"];
    if (j.contains("speed")) out.speed = j["speed"];
    if (j.contains("maxBounces")) out.maxBounces = j["maxBounces"];
    if (j.contains("damage")) out.damage = j["damage"];
    if (j.contains("windowWidth")) out.windowWidth = j["windowWidth"];
    if (j.contains("windowHeight")) out.windowHeight = j["windowHeight"];
    if (j.contains("visualScale")) out.visualScale = j["visualScale"];
    if (j.contains("hitboxRadius")) out.hitboxRadius = j["hitboxRadius"];
}

void AttackParamManager::ParseBoomerangParams(const json& j, BoomerangParams& out) {
    if (j.contains("spawnCount")) out.spawnCount = j["spawnCount"];
    if (j.contains("spawnDelay")) out.spawnDelay = j["spawnDelay"];
    if (j.contains("spawnBottomHalfOnly")) out.spawnBottomHalfOnly = j["spawnBottomHalfOnly"];
    if (j.contains("speed")) out.speed = j["speed"];
    if (j.contains("maxTravelDistance")) out.maxTravelDistance = j["maxTravelDistance"];
    if (j.contains("turnSpeed")) out.turnSpeed = j["turnSpeed"];
    if (j.contains("damage")) out.damage = j["damage"];
    if (j.contains("windowSize")) out.windowSize = j["windowSize"];
    if (j.contains("visualScale")) out.visualScale = j["visualScale"];
    if (j.contains("hitboxRadius")) out.hitboxRadius = j["hitboxRadius"];
}

void AttackParamManager::ParseBlasterParams(const json& j, BlasterParams& out) {
    if (j.contains("cannonWindowSize")) out.cannonWindowSize = j["cannonWindowSize"];
    if (j.contains("cannonVisualScale")) out.cannonVisualScale = j["cannonVisualScale"];
    if (j.contains("beamVisualWidth")) out.beamVisualWidth = j["beamVisualWidth"];
    if (j.contains("beamMaxLength")) out.beamMaxLength = j["beamMaxLength"];
    if (j.contains("beamGrowSpeed")) out.beamGrowSpeed = j["beamGrowSpeed"];
    if (j.contains("beamSlideSpeed")) out.beamSlideSpeed = j["beamSlideSpeed"];
    if (j.contains("beamDamage")) out.beamDamage = j["beamDamage"];

    if (j.contains("spawnCount")) out.spawnCount = j["spawnCount"];
    if (j.contains("spawnDelay")) out.spawnDelay = j["spawnDelay"];
    if (j.contains("spawnSpreadX")) out.spawnSpreadX = j["spawnSpreadX"];
    if (j.contains("chargeDelay")) out.chargeDelay = j["chargeDelay"];
    if (j.contains("fireDuration")) out.fireDuration = j["fireDuration"];
    if (j.contains("dropInDuration")) out.dropInDuration = j["dropInDuration"];
}

void AttackParamManager::ParseSpearParams(const json& j, UndyneSpearParams& out) {
    if (j.contains("count")) out.count = j["count"];
    if (j.contains("spawnDelay")) out.spawnDelay = j["spawnDelay"];
    if (j.contains("hoverDuration")) out.hoverDuration = j["hoverDuration"];
    if (j.contains("maxSpeed")) out.maxSpeed = j["maxSpeed"];
    if (j.contains("arcRadius")) out.arcRadius = j["arcRadius"];
    if (j.contains("arcCenterX")) out.arcCenterX = j["arcCenterX"];
    if (j.contains("arcCenterZ")) out.arcCenterZ = j["arcCenterZ"];
    if (j.contains("arcMinAngle")) out.arcMinAngle = j["arcMinAngle"];
    if (j.contains("arcMaxAngle")) out.arcMaxAngle = j["arcMaxAngle"];
    if (j.contains("damage")) out.damage = j["damage"];
    if (j.contains("startSpeed")) out.startSpeed = j["startSpeed"];
    if (j.contains("acceleration")) out.acceleration = j["acceleration"];
}