#include "AnimationController.h"
#include <functional>

using namespace DirectX; // Untuk mempermudah penulisan XMVECTOR, dll

void AnimationController::Initialize(std::shared_ptr<Model> model)
{
    ownerModel = model;
    size_t nodeCount = ownerModel->GetNodes().size();

    // Alokasi memori untuk pose sekarang dan pose blending
    nodePoses.resize(nodeCount);
    prevNodePoses.resize(nodeCount);
}

void AnimationController::Play(const std::string& name, bool loop, float blendTime)
{
    if (!ownerModel) return;

    int newIndex = ownerModel->GetAnimationIndex(name.c_str());
    if (newIndex == -1) return;

    if (newIndex != currentAnimIndex)
    {
        // --- LOGIKA BLENDING DI MULAI ---

        // Jika kita sebelumnya sedang memainkan animasi (bukan dari T-Pose awal),
        // Kita snapshot (foto) pose terakhir ke dalam 'prevNodePoses'
        if (currentAnimIndex != -1)
        {
            // Salin state terakhir nodePoses ke prevNodePoses
            prevNodePoses = nodePoses;

            // Setup blending
            isBlending = true;
            blendDuration = blendTime;
            blendTimer = 0.0f;
        }
        else
        {
            // Kalau ini animasi pertama kali dijalankan, tidak perlu blending
            isBlending = false;
        }

        // --- RESET STATE BARU ---
        currentAnimIndex = newIndex;
        timer = 0.0f;
        isLooping = loop;
        m_playbackSpeed = 1.0f;
    }
}

void AnimationController::Update(float dt)
{
    if (!ownerModel || currentAnimIndex == -1) return;

    // 1. Update Timer Animasi Utama
    const auto& anims = ownerModel->GetAnimations();
    float duration = anims.at(currentAnimIndex).secondsLength;

    // ---> THE FIX: Guard against Division by Zero <---
    if (duration > 0.0f)
    {
        timer += dt * m_playbackSpeed;

        if (isLooping)
        {
            // ---> THE FIX: AAA Safe Time Wrapping (No fmod crashes!) <---
            while (timer < 0.0f) timer += duration;
            while (timer >= duration) timer -= duration;
        }
        else
        {
            // Clamp strictly
            if (timer < 0.0f) timer = 0.0f;
            if (timer > duration) timer = duration;
        }
    }
    else
    {
        timer = 0.0f; // Safe fallback for 0-second animations
    }

    // 2. Hitung Pose Animasi Target (Animasi Baru)
    // Hasil sementara disimpan di 'nodePoses' dulu
    ownerModel->ComputeAnimation(currentAnimIndex, timer, nodePoses);

    // 3. Proses Blending (Jika aktif)
    if (isBlending)
    {
        blendTimer += dt;

        // Hitung persentase blending (0.0 sampai 1.0)
        float t = blendTimer / blendDuration;

        if (t >= 1.0f)
        {
            t = 1.0f;
            isBlending = false; // Blending selesai
        }

        // Lakukan interpolasi per-bone (Node)
        size_t count = nodePoses.size();
        for (size_t i = 0; i < count; ++i)
        {
            // [FIX] Menggunakan .position (bukan .translation) sesuai Model.h
            // Ambil data dari Pose Lama (Snapshot)
            XMVECTOR s0 = XMLoadFloat3(&prevNodePoses[i].scale);
            XMVECTOR r0 = XMLoadFloat4(&prevNodePoses[i].rotation);
            XMVECTOR t0 = XMLoadFloat3(&prevNodePoses[i].position);

            // Ambil data dari Pose Baru (Target)
            XMVECTOR s1 = XMLoadFloat3(&nodePoses[i].scale);
            XMVECTOR r1 = XMLoadFloat4(&nodePoses[i].rotation);
            XMVECTOR t1 = XMLoadFloat3(&nodePoses[i].position);

            // Interpolasi
            // Lerp untuk Scale dan Position
            XMVECTOR sFinal = XMVectorLerp(s0, s1, t);
            XMVECTOR tFinal = XMVectorLerp(t0, t1, t);
            // Slerp (Spherical Linear Interpolation) untuk Rotasi agar putaran mulus
            XMVECTOR rFinal = XMQuaternionSlerp(r0, r1, t);

            // Simpan kembali ke nodePoses (ini yang akan dipakai render)
            XMStoreFloat3(&nodePoses[i].scale, sFinal);
            XMStoreFloat4(&nodePoses[i].rotation, rFinal);
            XMStoreFloat3(&nodePoses[i].position, tFinal);
        }
    }

    // 4. Upload ke Model (Visual)
    if (upperAnimIndex != -1 && !upperBodyMask.empty())
    {
        upperTimer += dt;
        float upperDuration = anims.at(upperAnimIndex).secondsLength;

        // Cek apakah animasi sudah selesai
        if (upperTimer >= upperDuration)
        {
            if (upperIsLooping) {
                upperTimer = fmod(upperTimer, upperDuration); // Ulangi
            }
            else {
                upperAnimIndex = -1; // Matikan animasi upper body
            }
        }

        // Jika animasi masih berjalan (belum dimatikan)
        if (upperAnimIndex != -1)
        {
            if (upperNodePoses.size() != ownerModel->GetNodes().size()) {
                upperNodePoses.resize(ownerModel->GetNodes().size());
            }
            ownerModel->ComputeAnimation(upperAnimIndex, upperTimer, upperNodePoses);

            // --- BONE MASKING BLEND ---
            for (size_t i = 0; i < nodePoses.size(); ++i)
            {
                if (upperBodyMask[i])
                {
                    // Timpa data dari Base Layer dengan Upper Layer
                    nodePoses[i] = upperNodePoses[i];
                }
            }
        }
    }
    // 4. Upload ke Model (Visual)
    ownerModel->SetNodePoses(nodePoses);
}

bool AnimationController::IsPlaying(const std::string& name) const
{
    if (!ownerModel) return false;
    return currentAnimIndex == ownerModel->GetAnimationIndex(name.c_str());
}

void AnimationController::SetUpperBodyMaskRoot(const std::string& rootNodeName)
{
    if (!ownerModel) return;

    upperBodyMask.assign(ownerModel->GetNodes().size(), false);
    int splitIndex = ownerModel->GetNodeIndex(rootNodeName.c_str());
    if (splitIndex == -1) return;

    // Gunakan lambda rekursif untuk menandai node 'body' dan SEMUA anaknya
    std::function<void(Model::Node*)> markNode = [&](Model::Node* node) {
        int idx = ownerModel->GetNodeIndex(node->name.c_str());
        if (idx != -1) upperBodyMask[idx] = true;

        for (Model::Node* child : node->children) {
            markNode(child);
        }
        };

    markNode(&ownerModel->GetNodes()[splitIndex]);
}

// 1. Ubah implementasi PlayUpper:
void AnimationController::PlayUpper(const std::string& name, bool loop)
{
    if (!ownerModel) return;
    upperAnimIndex = ownerModel->GetAnimationIndex(name.c_str());
    upperTimer = 0.0f;
    upperIsLooping = loop; // Simpan status looping
}