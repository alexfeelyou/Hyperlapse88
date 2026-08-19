#include "PerformanceLogger.h"
#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>

void PerformanceLogger::Initialize()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_logFile.is_open()) return;

    std::string logDirectory = "Logs"; // Nama foldernya "Logs"
    if (!std::filesystem::exists(logDirectory))
    {
        std::filesystem::create_directory(logDirectory);
    }

    // Generate nama file berdasarkan waktu
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    tm localTime;
    localtime_s(&localTime, &now);
    std::stringstream filename;
    filename << logDirectory << "/ChaosLog_" << std::put_time(&localTime, "%Y%m%d_%H%M%S") << ".txt";

    m_logFile.open(filename.str());

    m_logFile << "=====================================================================\n";
    m_logFile << "                    PROJECT CHAOS - PERFORMANCE LOG\n";
    m_logFile << "=====================================================================\n";
    m_logFile << "Session Start : " << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << "\n";
    m_logFile << "---------------------------------------------------------------------\n";
    m_logFile << "[SYSTEM SPECS]\n";
    m_logFile << "OS            : " << SDL_GetPlatform() << "\n";
    m_logFile << "CPU           : " << SDL_GetNumLogicalCPUCores() << " Cores\n";
    m_logFile << "RAM           : " << SDL_GetSystemRAM() << " MB\n";
    m_logFile << "---------------------------------------------------------------------\n\n";
}

void PerformanceLogger::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_logFile.is_open()) return;

    m_logFile << "\n=====================================================================\n";
    m_logFile << "                       === SESSION CLOSED CLEANLY ===\n";
    m_logFile << "=====================================================================\n";
    m_logFile << "[SESSION SUMMARY]\n";
    m_logFile << "Max Concurrent Windows: " << m_maxWindows << "\n";
    m_logFile << "Optimal Window Capacity : " << m_maxStableWindows << " (Max windows before dropping < 50 FPS)\n";
    m_logFile << "Lowest FPS Recorded   : " << (m_lowestFps == 999.0f ? 60.0f : m_lowestFps) << " FPS\n";
    m_logFile << "Total Boundary Clamps : " << m_totalClamps << "\n";
    m_logFile << "=====================================================================\n";

    m_logFile.close();
}

void PerformanceLogger::LogInfo(const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_logFile.is_open()) return;

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    tm localTime; localtime_s(&localTime, &now);
    m_logFile << "[" << std::put_time(&localTime, "%H:%M:%S") << "] " << message << "\n";
}

void PerformanceLogger::LogWindowAction(const std::string& action, const std::string& windowName, float latencyMs)
{
    std::stringstream ss;
    ss << "[WINDOW] " << action << ": " << windowName << " (Latency: " << std::fixed << std::setprecision(2) << latencyMs << " ms)";
    LogInfo(ss.str());
}

void PerformanceLogger::LogBoundaryClamp(const std::string& windowName, int oldX, int oldY, int newX, int newY)
{
    m_totalClamps++;
    std::stringstream ss;
    ss << "[PHYSICS] CLAMP: '" << windowName << "' hit bounds. Pushed back to (" << newX << ", " << newY << ").";
    LogInfo(ss.str());
}

void PerformanceLogger::StartTimer(PerfBucket bucket)
{
    m_bucketStarts[(int)bucket] = std::chrono::high_resolution_clock::now();
}

void PerformanceLogger::StopTimer(PerfBucket bucket)
{
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float, std::milli> duration = end - m_bucketStarts[(int)bucket];
    m_bucketAccumulators[(int)bucket] += duration.count();
}

void PerformanceLogger::EndFrameCheck(float currentFps, int activeWindows)
{
    if (activeWindows > m_maxWindows) m_maxWindows = activeWindows;
    if (currentFps > 0 && currentFps < m_lowestFps) m_lowestFps = currentFps;

    if (currentFps >= 50.0f && activeWindows > m_maxStableWindows) {
        m_maxStableWindows = activeWindows;
    }

    // Update cooldown
    if (m_fpsCooldown > 0.0f) {
        m_fpsCooldown -= (1.0f / 60.0f); // Asumsi 60hz frame time
    }

    // Jika FPS drop di bawah 50 dan cooldown sudah selesai (misal nge-log max 1 kali tiap 2 detik)
    if (currentFps < 50.0f && m_fpsCooldown <= 0.0f)
    {
        std::stringstream ss;
        ss << "\n---------------------------------------------------------------------\n";

        if (currentFps < 40.0f) ss << "> [PERFORMANCE CRITICAL] - FPS SEVERE DROP (" << currentFps << " FPS)\n";
        else ss << "> [PERFORMANCE WARNING] - FPS DROP DETECTED (" << currentFps << " FPS)\n";

        ss << "> Active Windows   : " << activeWindows << "\n";
        ss << "> --- PROFILING BREAKDOWN ---\n";
        ss << "> Logic & Physics  : " << std::fixed << std::setprecision(2) << m_bucketAccumulators[(int)PerfBucket::Logic] << " ms\n";
        ss << "> Render 3D (D3D)  : " << m_bucketAccumulators[(int)PerfBucket::Render3D] << " ms\n";
        ss << "> Window OS (GDI)  : " << m_bucketAccumulators[(int)PerfBucket::WindowOS] << " ms\n";

        // Auto-Diagnosis Sederhana
        if (m_bucketAccumulators[(int)PerfBucket::WindowOS] > 15.0f) {
            ss << "> DIAGNOSIS        : DWM / GDI Readback overload. OS Windows nge-lag.\n";
        }
        else if (m_bucketAccumulators[(int)PerfBucket::Render3D] > 15.0f) {
            ss << "> DIAGNOSIS        : GPU kewalahan merender scene 3D.\n";
        }

        ss << "---------------------------------------------------------------------\n";
        LogInfo(ss.str());

        m_fpsCooldown = 2.0f; // Tunggu 2 detik sebelum nge-log warning FPS lagi
    }

    // Reset accumulator tiap akhir frame
    m_bucketAccumulators[0] = 0.0f;
    m_bucketAccumulators[1] = 0.0f;
    m_bucketAccumulators[2] = 0.0f;
}