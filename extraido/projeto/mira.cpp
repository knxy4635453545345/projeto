// language: C++, file: hooks/aimbot.cpp
// Camera.Rotation = 192 (matriz 3x3, 9 floats)
// Camera.Position = 228 (3 floats)
// Escrever a rotação aponta a câmera para o alvo.
#include <cmath>
#include <cstring>
#include <unistd.h>
#include <android/log.h>
#include <atomic>
#include "hellborn.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HB_AIM", __VA_ARGS__)

namespace off {
    constexpr uintptr_t CAM_Position = 228;
    constexpr uintptr_t CAM_Rotation = 192;
}

static inline Vec3 lerp3(const Vec3& a, const Vec3& b, float t) {
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

// constrói matriz de rotação 3x3 que olha de cam_pos para target
// base Roblox: X = direita, Y = cima, Z = frente (a câmera olha para -Z no olho)
static void build_look_matrix(const Vec3& eye, const Vec3& target, float out[9]) {
    Vec3 dir { target.x - eye.x, target.y - eye.y, target.z - eye.z };
    float dl = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (dl < 1e-4f) { memset(out, 0, 9 * sizeof(float)); return; }
    dir.x /= dl; dir.y /= dl; dir.z /= dl;

    // right = normalize(dir × worldUp)
    Vec3 up { 0.f, 1.f, 0.f };
    Vec3 right {
        dir.y*up.z - dir.z*up.y,
        dir.z*up.x - dir.x*up.z,
        dir.x*up.y - dir.y*up.x
    };
    float rl = std::sqrt(right.x*right.x + right.y*right.y + right.z*right.z);
    if (rl < 1e-4f) { memset(out, 0, 9 * sizeof(float)); return; }
    right.x /= rl; right.y /= rl; right.z /= rl;

    // up = right × dir
    Vec3 u {
        right.y*dir.z - right.z*dir.y,
        right.z*dir.x - right.x*dir.z,
        right.x*dir.y - right.y*dir.x
    };

    // Layout Roblox CFrame: R colunas X=right, Y=up, Z=-dir (facing)
    out[0] = right.x; out[1] = right.y; out[2] = right.z;
    out[3] = u.x;     out[4] = u.y;     out[5] = u.z;
    out[6] = -dir.x;  out[7] = -dir.y;  out[8] = -dir.z;
}

// seleciona alvo mais próximo do centro da tela dentro do FOV
static int pick_target(float fov) {
    int n = g_esp_count.load();
    float cx = g_screen_w.load() * 0.5f;
    float cy = g_screen_h.load() * 0.5f;
    float best = fov;
    int best_i = -1;
    bool team_check = g_settings.aim_team_check.load();

    for (int i = 0; i < n; i++) {
        const EspEntity& e = g_esp[i];
        if (!e.valid) continue;
        if (team_check && e.is_team) continue;
        float dx = e.head_screen.x - cx;
        float dy = e.head_screen.y - cy;
        float d = std::sqrt(dx*dx + dy*dy);
        if (d < best) { best = d; best_i = i; }
    }
    return best_i;
}

static void write_camera_rotation(uintptr_t cam, float smoothed[9]) {
    // escrita atômica por linha — reduz tearing contra o input controller
    memcpy(reinterpret_cast<void*>(cam + off::CAM_Rotation), smoothed, 9 * sizeof(float));
}

void start_aimbot_thread() {
    float current[9] = {};
    bool primed = false;

    while (true) {
        if (!g_settings.aim_enabled.load() || !g_aimbot_held.load()) {
            primed = false;
            usleep(4000);
            continue;
        }

        uintptr_t cam = g_cam_addr.load();
        if (!cam) { usleep(4000); continue; }

        int idx = pick_target(g_settings.aim_fov.load());
        if (idx < 0) { usleep(4000); continue; }

        const EspEntity& e = g_esp[idx];

        Vec3 eye{};
        memcpy(&eye, reinterpret_cast<void*>(cam + off::CAM_Position), sizeof(eye));

        // predição
        float p = g_settings.aim_prediction.load();
        Vec3 predicted {
            e.head_world.x + e.velocity_world.x * p,
            e.head_world.y + e.velocity_world.y * p,
            e.head_world.z + e.velocity_world.z * p
        };

        float goal[9]{};
        build_look_matrix(eye, predicted, goal);

        // smooth: interpola com a matriz atual escrita por último
        if (!primed) {
            // primeiro frame: lê a matriz atual pra suavizar a partir dela
            memcpy(current, reinterpret_cast<void*>(cam + off::CAM_Rotation), sizeof(current));
            primed = true;
        }
        float s = g_settings.aim_smoothing.load();
        float blended[9];
        for (int i = 0; i < 9; i++)
            blended[i] = current[i] + (goal[i] - current[i]) * (1.0f - s);
        memcpy(current, blended, sizeof(blended));

        write_camera_rotation(cam, blended);
        usleep(4000);
    }
}