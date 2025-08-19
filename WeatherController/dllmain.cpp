#include "pch.h"
#include <Windows.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

// 内存地址
uintptr_t timeBaseAddr = 0x009B392C; // 时间控制地址（单精度浮点数）
uintptr_t rainAddr = 0x009B0A30;     // 降雨控制地址（整型）

// 时间控制配置
std::atomic<bool> timeControlEnabled = true; // 时间控制总开关（通过配置文件）
std::atomic<bool> timeControlActive = false; // 当前启用状态
double timeCycleDuration = 1200.0;           // 每个时间循环周期，单位秒（默认 20 分钟）
std::atomic<double> timeSpeed = 1.0;
bool isPaused = false;
bool wasPauseKeyPressed = false;
bool wasToggleKeyPressed = false;

int keyToggleTimeControl = 96; // 启用/关闭时间控制：小键盘 0
int keyReverse = 97;           // 按住倒退时间：小键盘 1
int keyPause = 98;             // 按住暂停时间：小键盘 2
int keyForward = 99;           // 按住快进时间：小键盘 3

double speedReverse = -50.0;
double speedForward = 50.0;
double speedNormal = 1.0;

// 天气控制配置
bool weatherControlEnabled = true;
bool isControlTransferred = false;
int keyToggleWeather = 103;       // 切换降雨状态：小键盘 7
int keyGiveWeatherControl = 105;  // 停止降雨并将天气控制权转交游戏：小键盘 9

// 配置路径
char configPath[MAX_PATH];

// 读取配置
void LoadConfig() {
    char buffer[64];

    GetPrivateProfileStringA("KEYS", "TOGGLE_TIME_CONTROL", "96", buffer, sizeof(buffer), configPath);
    keyToggleTimeControl = atoi(buffer);

    GetPrivateProfileStringA("KEYS", "REVERSE", "97", buffer, sizeof(buffer), configPath);
    keyReverse = atoi(buffer);
    GetPrivateProfileStringA("KEYS", "PAUSE", "98", buffer, sizeof(buffer), configPath);
    keyPause = atoi(buffer);
    GetPrivateProfileStringA("KEYS", "FORWARD", "99", buffer, sizeof(buffer), configPath);
    keyForward = atoi(buffer);

    GetPrivateProfileStringA("KEYS", "TOGGLE_WEATHER", "103", buffer, sizeof(buffer), configPath);
    keyToggleWeather = atoi(buffer);
    GetPrivateProfileStringA("KEYS", "GIVE_WEATHER_CONTROL", "105", buffer, sizeof(buffer), configPath);
    keyGiveWeatherControl = atoi(buffer);

    GetPrivateProfileStringA("SPEED", "REVERSE", "-50.0", buffer, sizeof(buffer), configPath);
    speedReverse = atof(buffer);
    GetPrivateProfileStringA("SPEED", "FORWARD", "50.0", buffer, sizeof(buffer), configPath);
    speedForward = atof(buffer);
    GetPrivateProfileStringA("SPEED", "NORMAL", "1.0", buffer, sizeof(buffer), configPath);
    speedNormal = atof(buffer);

    GetPrivateProfileStringA("CONFIG", "TIME_CONTROL_ENABLED", "1", buffer, sizeof(buffer), configPath);
    timeControlEnabled = atoi(buffer) != 0;

    GetPrivateProfileStringA("CONFIG", "TIME_CYCLE_DURATION", "1200.0", buffer, sizeof(buffer), configPath);
    timeCycleDuration = atof(buffer);
}

// 主线程功能
void ControllerThread() {
    LoadConfig();

    float* timeAddr = nullptr;
    double customTime = timeCycleDuration * 0.5;
    bool forwardCycle = true;
    auto lastUpdate = std::chrono::steady_clock::now();
    BYTE* weatherFlag = reinterpret_cast<BYTE*>(rainAddr);

    while (true) {
        bool toggleHeld = (GetAsyncKeyState(keyToggleTimeControl) & 0x8000) != 0;

        if (toggleHeld && !wasToggleKeyPressed && timeControlEnabled) {
            timeControlActive = !timeControlActive;
        }
        wasToggleKeyPressed = toggleHeld;

        if (!timeControlEnabled) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        bool reverseHeld = (GetAsyncKeyState(keyReverse) & 0x8000) != 0;
        bool forwardHeld = (GetAsyncKeyState(keyForward) & 0x8000) != 0;
        bool pauseHeld = (GetAsyncKeyState(keyPause) & 0x8000) != 0;

        if (pauseHeld && !wasPauseKeyPressed) {
            isPaused = !isPaused;
        }
        wasPauseKeyPressed = pauseHeld;

        if (timeControlActive) {
            if (isPaused) {
                timeSpeed = 0.0;
            }
            else if (reverseHeld) {
                timeSpeed = speedReverse;
            }
            else if (forwardHeld) {
                timeSpeed = speedForward;
            }
            else {
                timeSpeed = speedNormal;
            }

            // 定位指针地址
            timeAddr = *(float**)timeBaseAddr;
            if (timeAddr == nullptr) continue;
            timeAddr = timeAddr + 2;

            auto now = std::chrono::steady_clock::now();
            double delta = std::chrono::duration<double>(now - lastUpdate).count();
            lastUpdate = now;

            customTime += delta * timeSpeed;

            if (!reverseHeld && !forwardHeld) {
                if (customTime >= timeCycleDuration) {
                    forwardCycle = false;
                    customTime = timeCycleDuration;
                }
                else if (customTime <= 0.0) {
                    forwardCycle = true;
                    customTime = 0.0;
                }

                double direction = forwardCycle ? 1.0 : -1.0;
                customTime += delta * direction * timeSpeed;

                if (customTime > timeCycleDuration) customTime = timeCycleDuration;
                if (customTime < 0.0) customTime = 0.0;
            }
            else {
                if (customTime < 0) customTime += timeCycleDuration;
                if (customTime >= timeCycleDuration) customTime -= timeCycleDuration;
            }

            double normalizedTime = customTime / timeCycleDuration;
            DWORD oldProtect;
            VirtualProtect((LPVOID)timeAddr, sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect);
            *(float*)timeAddr = (float)normalizedTime;
            VirtualProtect((LPVOID)timeAddr, sizeof(float), oldProtect, &oldProtect);
        }

        // 天气控制
        bool toggleWeatherHeld = (GetAsyncKeyState(keyToggleWeather) & 0x8000) != 0;
        bool giveWeatherHeld = (GetAsyncKeyState(keyGiveWeatherControl) & 0x8000) != 0;

        if (toggleWeatherHeld) {
            if (weatherControlEnabled || isControlTransferred) {
                *weatherFlag = (*weatherFlag == 0) ? 1 : 0;
                Sleep(300);
            }
        }

        if (giveWeatherHeld) {
            if (*weatherFlag == 1) *weatherFlag = 0; // 停止下雨
            weatherControlEnabled = false;
            isControlTransferred = true;
            Sleep(300);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// DLL 主入口
DWORD WINAPI MainThread(LPVOID hModule) {
    std::thread(ControllerThread).detach();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        char modulePath[MAX_PATH];
        GetModuleFileNameA(hModule, modulePath, MAX_PATH);
        std::string folderPath(modulePath);
        size_t pos = folderPath.find_last_of("\\/");
        if (pos != std::string::npos) {
            folderPath = folderPath.substr(0, pos + 1);
        }

        std::string configFile = folderPath + "WeatherController.ini";
        strcpy_s(configPath, configFile.c_str());

        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}