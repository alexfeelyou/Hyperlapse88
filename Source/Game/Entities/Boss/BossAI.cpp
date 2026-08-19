#include "BossAI.h"
#include "BossPhase01.h"
#include "BossPhase02.h"
#include "Player.h"
#include "Boss.h"
#include "AttackParamManager.h"
#include <cmath>

#include "BossAI.h"
#include "BossPhase01.h"
#include "BossPhase02.h"
#include "Player.h"
#include "Boss.h"
#include "AttackParamManager.h"
#include <cmath>

// ========================================================
// IMPLEMENTASI AI PHASE 01
// ========================================================
BossAI_Phase01::BossAI_Phase01(BossPhase01* phase, Player* target)
    : m_phase(phase), m_target(target) {}

// [DIUBAH] Mesin Pengatur Giliran Selang-Seling (Tactician Mode)
BossAI_Phase01::AttackSequence BossAI_Phase01::GetNextTacticianAttack() {
    AttackSequence nextAttack;

    if (m_isNextMainAttack) {
        // Giliran Main Attack (Wave -> Phalanx -> Meteor -> Targeted Rain)
        switch (m_mainAttackIndex) {
        case 0: nextAttack = AttackSequence::Wave; break;
        case 1: nextAttack = AttackSequence::Phalanx; break;
        case 2: nextAttack = AttackSequence::Meteor; break;
        case 3: nextAttack = AttackSequence::Rain; break;
        }
        m_mainAttackIndex = (m_mainAttackIndex + 1) % 4; // Ulangi ke 0 jika mencapai 4
    }
    else {
        // [DIUBAH] Filler kini selalu Radial (Direct & Fan dipensiunkan dari rotasi filler)
        nextAttack = AttackSequence::Radial;
    }

    // Tukar status agar serangan berikutnya bergantian
    m_isNextMainAttack = !m_isNextMainAttack;

    return nextAttack;
}

void BossAI_Phase01::Update(float dt, Boss* boss) {
    if (!m_enabled || !m_target || !m_phase) return;

    // Tunggu sampai serangan utama dan hujan selesai sebelum melancarkan serangan berikutnya
    if (m_phase->HasActiveAttacks() || m_phase->HasRainActive()) return;

    m_cooldownTimer -= dt;

    if (m_cooldownTimer <= 0.0f) {
        float hpPercent = static_cast<float>(m_phase->GetHP()) / static_cast<float>(m_phase->GetMaxHP());
        bool isEnraged = (hpPercent <= 0.5f);

        // [BARU] Transisi mulus saat boss masuk Chaos Mode (HP < 50%)
        if (isEnraged && !m_isEnraged) {
            m_isEnraged = true;
            m_currentAttack = AttackSequence::Phalanx; // Memulai combo pertama dari Phalanx
        }

        DirectX::XMFLOAT3 pPos = m_target->GetPosition();
        DirectX::XMFLOAT3 bPos = boss->GetPosition();
        float lockedAngle = std::atan2f(pPos.x - bPos.x, pPos.z - bPos.z);

        switch (m_currentAttack) {

            // ========================================================
            // FILLER ATTACK (Tactician Mode)
            // [DIUBAH] Direct & Fan dipensiunkan dari rotasi filler aktif.
            // Case di bawah dipertahankan (bukan dihapus) untuk kemungkinan
            // dipakai sebagai layer tambahan di kombinasi Fase 2 nanti.
            // Tidak lagi reachable dari GetNextTacticianAttack() atau default state.
            // ========================================================
        case AttackSequence::Direct:
            m_phase->AddPooledAttack(std::make_unique<AttackDirect>(AttackParamManager::Instance().GetDirectParams(), m_target));
            m_cooldownTimer = 1.0f;
            m_currentAttack = GetNextTacticianAttack();
            break;

        case AttackSequence::Fan:
            m_phase->AddPooledAttack(std::make_unique<AttackFan>(AttackParamManager::Instance().GetFanNormalParams(), lockedAngle, m_target));
            m_cooldownTimer = 1.0f;
            m_currentAttack = GetNextTacticianAttack();
            break;

        case AttackSequence::Radial:
            m_phase->AddPooledAttack(std::make_unique<AttackRadial>(AttackParamManager::Instance().GetRadialNormalParams()));
            m_cooldownTimer = 1.0f;
            m_currentAttack = GetNextTacticianAttack();
            break;

            // ========================================================
            // SHARED MAIN ATTACKS (Tactician = Single, Enraged = Combo + Rain)
            // ========================================================
        case AttackSequence::Phalanx:
            m_phase->AddPooledAttack(std::make_unique<AttackPhalanx>(AttackParamManager::Instance().GetPhalanxParams(), m_target));
            if (isEnraged) {
                // Eksekusi Combo Rain secara bersamaan
                m_phase->TriggerRain(AttackParamManager::Instance().GetRainParams(), RainMode::VerticalSweep, m_target->GetPosition().x > 0);
                m_currentAttack = AttackSequence::Wave; // Siklus Chaos berikutnya
            }
            else {
                m_currentAttack = GetNextTacticianAttack();
            }
            m_cooldownTimer = 1.0f;
            break;

        case AttackSequence::Wave:
            m_phase->AddPooledAttack(std::make_unique<AttackWave>(AttackParamManager::Instance().GetWaveParams()));
            if (isEnraged) {
                m_phase->TriggerRain(AttackParamManager::Instance().GetRainParams(), RainMode::VerticalSweep, m_target->GetPosition().x > 0);
                m_currentAttack = AttackSequence::Meteor;
            }
            else {
                m_currentAttack = GetNextTacticianAttack();
            }
            m_cooldownTimer = 1.0f;
            break;

        case AttackSequence::Meteor:
            m_phase->AddPooledAttack(std::make_unique<AttackMeteor>(AttackParamManager::Instance().GetMeteorParams()));
            if (isEnraged) {
                m_phase->TriggerRain(AttackParamManager::Instance().GetRainParams(), RainMode::VerticalSweep, m_target->GetPosition().x > 0);
                m_currentAttack = AttackSequence::Ultimate;
            }
            else {
                m_currentAttack = GetNextTacticianAttack();
            }
            m_cooldownTimer = 1.0f;
            break;

        case AttackSequence::Ultimate:
            m_phase->AddPooledAttack(std::make_unique<AttackUltimate>(AttackParamManager::Instance().GetUltimateParams(), m_target));
            if (isEnraged) {
                m_phase->TriggerRain(AttackParamManager::Instance().GetRainParams(), RainMode::VerticalSweep, m_target->GetPosition().x > 0);
                m_currentAttack = AttackSequence::Phalanx; // Kembali ke awal siklus Chaos
            }
            else {
                m_currentAttack = GetNextTacticianAttack();
            }
            m_cooldownTimer = 2.5f;
            break;

            // ========================================================
            // THE RAIN MANAGER (Tactician = Targeted)
            // ========================================================
        case AttackSequence::Rain:
            // Karena Rain Vertical Sweep sudah jadi Combo, state ini hanya dipanggil oleh Tactician Mode
            m_phase->TriggerRain(AttackParamManager::Instance().GetRainTargetedParams(), RainMode::Targeted, true);
            m_cooldownTimer = 1.0f;
            m_currentAttack = GetNextTacticianAttack();
            break;

        case AttackSequence::RadialContinuos:
        case AttackSequence::FanContinuos:
            // Kosongkan atau biarkan jika sewaktu-waktu ingin dipakai lagi
            break;
        }
    }
}

// ========================================================
// IMPLEMENTASI AI PHASE 02
// ========================================================
BossAI_Phase02::BossAI_Phase02(BossPhase02* phase, Player* target)
    : m_phase(phase), m_target(target) {}

void BossAI_Phase02::Update(float dt, Boss* boss) {
    // Jangan serang player jika masih terjebak di kandang
    if (!m_enabled || !m_target || !m_phase || m_phase->IsPlayerCaged()) return;

    // Tunggu sampai serangan saat ini selesai
    if (m_phase->HasActiveAttacks()) return;

    m_cooldownTimer -= dt;

    if (m_cooldownTimer <= 0.0f) {
        // Eksekusi serangan Windowkill (MENGGUNAKAN PARAM MANAGER)
        switch (m_currentAttack) {
        case AttackSequence::Bouncing:
            m_phase->AddAttack(std::make_unique<AttackBouncing>(AttackParamManager::Instance().GetBouncingParams()));
            m_cooldownTimer = 2.0f;
            m_currentAttack = AttackSequence::Boomerang;
            break;

        case AttackSequence::Boomerang:
            m_phase->AddAttack(std::make_unique<AttackBoomerangs>(AttackParamManager::Instance().GetBoomerangParams()));
            m_cooldownTimer = 2.0f;
            m_currentAttack = AttackSequence::Blaster;
            break;

        case AttackSequence::Blaster:
            // Menembak Laser Orbital ke arah Player
            m_phase->AddAttack(std::make_unique<AttackBlasters>(AttackParamManager::Instance().GetBlasterParams(), true, m_target->GetPosition().x));
            m_cooldownTimer = 2.0f;
            m_currentAttack = AttackSequence::Spear;
            break;

        case AttackSequence::Spear:
            m_phase->AddAttack(std::make_unique<AttackSpears>(AttackParamManager::Instance().GetUndyneParams(), m_target));
            m_cooldownTimer = 2.5f;
            m_currentAttack = AttackSequence::Bouncing;
            break;
        }
    }
}