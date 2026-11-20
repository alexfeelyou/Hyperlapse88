#include "HealthComponent.h"
#include <algorithm>
#include <imgui.h>
#include "CharacterMovementComponent.h"
#include "ComponentRegistry.h"
#include "GameObject.h"

HealthComponent::HealthComponent(const HealthConfig& config) noexcept
    : m_config{ config }
    , m_currentHp{ config.maxHp }
    , m_currentPoise{ config.maxPoise }
{}

void HealthComponent::OnAttach(GameObject* owner) noexcept
{
    IComponent::OnAttach(owner);
    m_currentHp = m_config.maxHp;
    m_currentPoise = m_config.maxPoise;
    m_isDead = false;
}

void HealthComponent::Update(const float dt)
{
    // Process combat hit-stop (frame suspension)
    if (m_hitStopTimer > 0.0f)
    {
        m_hitStopTimer -= dt;
        if (m_hitStopTimer < 0.0f)
        {
            m_hitStopTimer = 0.0f;
        }
    }

    if (m_isDead)
    {
        return;
    }

    // Process poise recovery mechanics
    if (m_poiseDelayTimer > 0.0f)
    {
        m_poiseDelayTimer -= dt;
    }
    else if (m_currentPoise < m_config.maxPoise)
    {
        m_currentPoise = (std::min)(m_config.maxPoise, m_currentPoise + (m_config.poiseRecoveryRate * dt));
    }
}

bool HealthComponent::ApplyDamage(
    const float damage,
    const float poiseDamage,
    const DirectX::XMFLOAT3& impulse,
    const float hitStopDuration) noexcept
{
    if (m_isDead || m_isInvincible)
    {
        return false;
    }

    // Sanitize non-negative incoming damage
    const float sanitizedDamage{ (std::max)(0.0f, damage) };
    const float sanitizedPoise{ (std::max)(0.0f, poiseDamage) };

    m_currentHp -= sanitizedDamage;
    m_hitStopTimer = hitStopDuration;

    // Apply physical knockback impulse directly to the kinematic motor
    if (m_owner)
    {
        if (auto* motor{ m_owner->GetComponent<CharacterMovementComponent>() })
        {
            motor->AddImpulse(impulse);
        }
    }

    // Death evaluation
    if (m_currentHp <= 0.0f)
    {
        m_currentHp = 0.0f;
        m_isDead = true;
        return true;
    }

    // Deplete poise gauge and test for stagger break
    m_currentPoise -= sanitizedPoise;
    m_poiseDelayTimer = m_config.poiseRecoveryDelay;

    if (m_currentPoise <= 0.0f)
    {
        m_currentPoise = 0.0f;
        return true; // Poise break triggered
    }

    return false;
}

void HealthComponent::Heal(const float amount) noexcept
{
    if (m_isDead)
    {
        return;
    }

    m_currentHp = (std::min)(m_config.maxHp, m_currentHp + (std::max)(0.0f, amount));
}

void HealthComponent::ResetPoise() noexcept
{
    m_currentPoise = m_config.maxPoise;
    m_poiseDelayTimer = 0.0f;
}

void HealthComponent::DrawInspector()
{
    ImGui::TextDisabled("Health & Poise Subsystem");
    ImGui::Separator();

    ImGui::DragFloat("Max HP", &m_config.maxHp, 1.0f, 1.0f, 10000.0f, "%.0f");
    ImGui::DragFloat("Max Poise", &m_config.maxPoise, 1.0f, 1.0f, 1000.0f, "%.0f");
    ImGui::DragFloat("Poise Recovery Rate", &m_config.poiseRecoveryRate, 0.5f, 0.0f, 100.0f, "%.1f/s");
    ImGui::DragFloat("Poise Recovery Delay", &m_config.poiseRecoveryDelay, 0.1f, 0.0f, 10.0f, "%.1fs");

    ImGui::Spacing();
    ImGui::TextDisabled("LIVE VITALS");

    const float hpFraction{ m_config.maxHp > 0.001f ? (m_currentHp / m_config.maxHp) : 0.0f };
    char hpBuf[64];
    snprintf(hpBuf, sizeof(hpBuf), "HP: %.0f / %.0f", m_currentHp, m_config.maxHp);
    ImGui::ProgressBar(hpFraction, ImVec2{ -1.0f, 0.0f }, hpBuf);

    const float poiseFraction{ m_config.maxPoise > 0.001f ? (m_currentPoise / m_config.maxPoise) : 0.0f };
    char poiseBuf[64];
    snprintf(poiseBuf, sizeof(poiseBuf), "Poise: %.0f / %.0f", m_currentPoise, m_config.maxPoise);
    ImGui::ProgressBar(poiseFraction, ImVec2{ -1.0f, 0.0f }, poiseBuf);

    ImGui::Checkbox("Invincible", &m_isInvincible);
}

void HealthComponent::Serialize(nlohmann::json& outJson) const
{
    outJson["MaxHp"] = m_config.maxHp;
    outJson["MaxPoise"] = m_config.maxPoise;
    outJson["PoiseRecoveryRate"] = m_config.poiseRecoveryRate;
    outJson["PoiseRecoveryDelay"] = m_config.poiseRecoveryDelay;
}

void HealthComponent::Deserialize(const nlohmann::json& inJson)
{
    m_config.maxHp = inJson.value("MaxHp", 100.0f);
    m_config.maxPoise = inJson.value("MaxPoise", 50.0f);
    m_config.poiseRecoveryRate = inJson.value("PoiseRecoveryRate", 15.0f);
    m_config.poiseRecoveryDelay = inJson.value("PoiseRecoveryDelay", 2.5f);

    m_currentHp = m_config.maxHp;
    m_currentPoise = m_config.maxPoise;
    m_isDead = false;
}

REGISTER_COMPONENT(HealthComponent)