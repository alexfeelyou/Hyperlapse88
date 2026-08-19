#pragma once
#include <vector>
#include <string>
#include <memory>
#include <DirectXMath.h> // Pastikan include ini ada untuk perhitungan matematika
#include "System/Model.h"

class AnimationController
{
public:
    void Initialize(std::shared_ptr<Model> model);
    void Update(float dt);

    // blendTime default 0.2 detik (standar action game)
    void Play(const std::string& name, bool loop = true, float blendTime = 0.2f);

    void SetPlaybackSpeed(float speed) { m_playbackSpeed = speed; }
    [[nodiscard]] float GetPlaybackSpeed() const { return m_playbackSpeed; }

    bool IsPlaying(const std::string& name) const;
    float GetCurrentTime() const { return timer; }

    // Fungsi baru untuk inisialisasi mask
    void SetUpperBodyMaskRoot(const std::string& rootNodeName);

    // Fungsi untuk memutar animasi atas
    void PlayUpper(const std::string& name, bool loop = false);

    // Tambahkan fungsi pengecekan ini di bawah GetCurrentTime():
    bool IsUpperPlaying() const { return upperAnimIndex != -1; }

    // Tambahkan variabel ini di bagian private (di bawah upperTimer):
    bool upperIsLooping = false;

private:
    std::shared_ptr<Model> ownerModel;

    // State Animasi Saat Ini
    int currentAnimIndex = -1;
    float timer = 0.0f;
    bool isLooping = true;
    float m_playbackSpeed{ 1.0f };
    int upperAnimIndex = -1;
    float upperTimer = 0.0f;

    // State Blending
    bool isBlending = false;
    float blendTimer = 0.0f;
    float blendDuration = 0.0f;

    // Buffer Pose
    // 'nodePoses' menyimpan hasil akhir yang akan dikirim ke Model
    std::vector<Model::NodePose> nodePoses;

    // 'prevNodePoses' menyimpan snapshot pose terakhir dari animasi SEBELUMNYA
    std::vector<Model::NodePose> prevNodePoses;

    std::vector<Model::NodePose> upperNodePoses;
    std::vector<bool> upperBodyMask;
};