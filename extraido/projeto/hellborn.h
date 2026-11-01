// language: C++, file: include/hellborn.h
#pragma once
#include <atomic>
#include <array>
#include <cstdint>

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

struct EspEntity {
    bool   valid       = false;
    Vec2   box_tl      {};
    Vec2   box_size    {};
    Vec2   head_screen {};
    Vec3   head_world  {};          // publicado pro aimbot
    Vec3   velocity_world {};
    float  health_pct  = 1.0f;
    float  distance    = 0.0f;
    bool   is_team     = false;
    char   name[64]    = {};
};

extern std::array<EspEntity, 64> g_esp;
extern std::atomic<int>          g_esp_count;

// view / dims preenchidos pelo esp
extern float g_view[16];
extern std::atomic<float> g_screen_w;
extern std::atomic<float> g_screen_h;

// câmera (endereço vivo) — o aimbot escreve rotação aqui
extern std::atomic<uintptr_t> g_cam_addr;

// endereços resolvidos uma vez
extern std::atomic<uintptr_t> g_visual_engine;
extern std::atomic<uintptr_t> g_data_model;
extern std::atomic<uintptr_t> g_players_svc;
extern std::atomic<uintptr_t> g_local_player;

extern std::atomic<bool> g_aimbot_held;

struct Settings {
    std::atomic<bool>  esp_enabled    {false};
    std::atomic<bool>  esp_box        {true};
    std::atomic<bool>  esp_health     {true};
    std::atomic<bool>  esp_name       {true};
    std::atomic<bool>  esp_distance   {true};
    std::atomic<bool>  esp_tracer     {false};
    std::atomic<bool>  esp_team_check {false};
    float esp_color[3] = {0.78f, 0.12f, 0.12f};

    std::atomic<bool>  aim_enabled      {false};
    std::atomic<float> aim_fov          {120.0f};
    std::atomic<float> aim_smoothing    {0.18f};
    std::atomic<float> aim_prediction   {0.12f};
    std::atomic<bool>  aim_team_check   {false};
    std::atomic<bool>  aim_visible_check{true};
};
extern Settings g_settings;

void start_overlay();
void start_esp_thread();
void start_aimbot_thread();
void stealth_hide_module();