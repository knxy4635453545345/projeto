// language: C++, file: hooks/esp.cpp
// Offsets: version-5a21b87bbc2641cf (dump 26/10/2026)
// Cadeia: VisualEngine → FakeDataModel → RealDataModel → Workspace/Players → Camera
#include <link.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <android/log.h>
#include <atomic>
#include "hellborn.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HB_ESP", __VA_ARGS__)

// ---------- STATE GLOBAL ----------
std::array<EspEntity, 64> g_esp{};
std::atomic<int>          g_esp_count{0};
float                     g_view[16]{};
std::atomic<float>        g_screen_w{0};
std::atomic<float>        g_screen_h{0};
std::atomic<uintptr_t>    g_cam_addr{0};
std::atomic<uintptr_t>    g_visual_engine{0};
std::atomic<uintptr_t>    g_data_model{0};
std::atomic<uintptr_t>    g_players_svc{0};
std::atomic<uintptr_t>    g_local_player{0};
std::atomic<bool>         g_aimbot_held{false};
Settings                  g_settings;

// ---------- OFFSETS (version-5a21b87bbc2641cf) ----------
namespace off {
    constexpr const char* engine_lib = "libroblox.so";

    // estáticos (offset no módulo; valor lido é ponteiro)
    constexpr uintptr_t VisualEngine_Pointer      = 142189080;
    constexpr uintptr_t TaskScheduler_Pointer     = 147445400;

    // VisualEngine
    constexpr uintptr_t VE_ViewMatrix             = 432;
    constexpr uintptr_t VE_Dimensions             = 2832;
    constexpr uintptr_t VE_FakeDataModel          = 2800;

    // FakeDataModel
    constexpr uintptr_t FDM_RealDataModel         = 504;

    // DataModel
    constexpr uintptr_t DM_Workspace              = 328;

    // Workspace
    constexpr uintptr_t WS_CurrentCamera          = 1184;

    // Camera
    constexpr uintptr_t CAM_Position              = 228;
    constexpr uintptr_t CAM_Rotation              = 192;
    constexpr uintptr_t CAM_Subject               = 176;
    constexpr uintptr_t CAM_FOV                   = 296;
    constexpr uintptr_t CAM_ViewportSize          = 708;

    // Instance
    constexpr uintptr_t INST_ChildrenStart        = 112;   // inicio do std::vector
    constexpr uintptr_t INST_ChildrenEndDelta     = 8;     // end fica em children_start + 8
    constexpr uintptr_t INST_NameContainer        = 104;
    constexpr uintptr_t INST_ClassDescriptor      = 24;
    constexpr uintptr_t INST_Parent               = 96;

    // NameContainer (std::string-like)
    constexpr uintptr_t NAME_Ptr                  = 0;
    constexpr uintptr_t NAME_Length               = 16;

    // ClassDescriptor
    constexpr uintptr_t CD_ClassName              = 8;

    // Humanoid
    constexpr uintptr_t HUM_Health                = 388;
    constexpr uintptr_t HUM_MaxHealth             = 412;
    constexpr uintptr_t HUM_HumanoidRootPart      = 1120;
    constexpr uintptr_t HUM_DisplayName           = 160;

    // BasePart → Primitive
    constexpr uintptr_t BP_Primitive              = 376;

    // Primitive
    constexpr uintptr_t PRIM_Position             = 212;
    constexpr uintptr_t PRIM_Size                 = 452;
    constexpr uintptr_t PRIM_AssemblyLinearVel    = 224;

    // Player
    constexpr uintptr_t PLAYER_LocalPlayer        = 280;
    constexpr uintptr_t PLAYER_ModelInstance      = 672;
    constexpr uintptr_t PLAYER_Team               = 736;
}
// ---------------------------------------------------------

static uintptr_t g_base = 0;

static void resolve_base() {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, off::engine_lib) && strstr(line, "r-xp")) {
            sscanf(line, "%lx-", &g_base);
            LOGI("engine base = %p", (void*)g_base);
            return;
        }
    }
    fclose(f);
}

template <typename T>
static inline bool rd(uintptr_t a, T& out) {
    if (!a) return false;
    memcpy(&out, reinterpret_cast<void*>(a), sizeof(T));
    return true;
}

static bool rd_str(uintptr_t container_addr, char* out, size_t cap) {
    // container: ptr no offset 0, tamanho no offset 16
    uintptr_t strp = 0;
    size_t len = 0;
    if (!rd(container_addr + off::NAME_Ptr, strp)) return false;
    if (!rd(container_addr + off::NAME_Length, len)) return false;
    if (!strp || len == 0 || len >= cap) return false;
    for (size_t i = 0; i < len; i++) {
        char c = 0;
        if (!rd(strp + i, c)) return false;
        out[i] = c;
    }
    out[len] = 0;
    return true;
}

static bool inst_name(uintptr_t inst, char* out, size_t cap) {
    return rd_str(inst + off::INST_NameContainer, out, cap);
}

static bool inst_class(uintptr_t inst, char* out, size_t cap) {
    uintptr_t cd = 0;
    if (!rd(inst + off::INST_ClassDescriptor, cd) || !cd) return false;
    return rd_str(cd + off::CD_ClassName, out, cap);
}

// itera std::vector<Instance*> começando em inst + INST_ChildrenStart
template <typename Fn>
static void for_each_child(uintptr_t inst, Fn&& fn) {
    uintptr_t start = 0, end = 0;
    if (!rd(inst + off::INST_ChildrenStart, start) || !start) return;
    if (!rd(inst + off::INST_ChildrenStart + off::INST_ChildrenEndDelta, end) || !end) return;
    if (end <= start) return;
    for (uintptr_t p = start; p < end; p += sizeof(uintptr_t)) {
        uintptr_t child = 0;
        if (!rd(p, child) || !child) continue;
        if (!fn(child)) break;
    }
}

static bool world_to_screen(const Vec3& w, Vec2& out) {
    // view-projection matrix 4x4, column-major (row selecionada por índice)
    float cx = w.x*g_view[0] + w.y*g_view[4] + w.z*g_view[8]  + g_view[12];
    float cy = w.x*g_view[1] + w.y*g_view[5] + w.z*g_view[9]  + g_view[13];
    float cw = w.x*g_view[3] + w.y*g_view[7] + w.z*g_view[11] + g_view[15];
    if (cw < 0.001f) return false;
    float sx = g_screen_w.load() * 0.5f;
    float sy = g_screen_h.load() * 0.5f;
    out.x = (cx / cw + 1.0f) * sx;
    out.y = (1.0f - cy / cw) * sy;
    return true;
}

// -------- resolução de cadeia por frame --------
static void resolve_chain() {
    if (!g_base) resolve_base();
    if (!g_base) return;

    uintptr_t ve = 0;
    if (!rd(g_base + off::VisualEngine_Pointer, ve) || !ve) {
        g_esp_count.store(0);
        return;
    }
    g_visual_engine.store(ve);

    // view + dims
    memcpy(g_view, reinterpret_cast<void*>(ve + off::VE_ViewMatrix), sizeof(g_view));

    float dims[2]{};
    if (rd(ve + off::VE_Dimensions, dims)) {
        if (dims[0] > 0 && dims[1] > 0) {
            g_screen_w.store(dims[0]);
            g_screen_h.store(dims[1]);
        }
    }

    // fake → real datamodel
    uintptr_t fake = 0;
    if (!rd(ve + off::VE_FakeDataModel, fake) || !fake) {
        g_esp_count.store(0);
        return;
    }
    uintptr_t dm = 0;
    if (!rd(fake + off::FDM_RealDataModel, dm) || !dm) {
        g_esp_count.store(0);
        return;
    }
    g_data_model.store(dm);

    // workspace → camera
    uintptr_t ws = 0;
    if (!rd(dm + off::DM_Workspace, ws) || !ws) return;
    uintptr_t cam = 0;
    if (!rd(ws + off::WS_CurrentCamera, cam) || !cam) return;
    g_cam_addr.store(cam);

    // sobrescreve com ViewportSize real da câmera se disponível
    float vp[2]{};
    if (rd(cam + off::CAM_ViewportSize, vp) && vp[0] > 0 && vp[1] > 0) {
        g_screen_w.store(vp[0]);
        g_screen_h.store(vp[1]);
    }

    // Players service via children do DataModel (busca por ClassName)
    if (!g_players_svc.load()) {
        for_each_child(dm, [](uintptr_t child) {
            char cls[32]{};
            if (inst_class(child, cls, sizeof(cls)) && !strcmp(cls, "Players")) {
                g_players_svc.store(child);
                return false;    // stop
            }
            return true;
        });
    }

    // LocalPlayer
    uintptr_t ps = g_players_svc.load();
    if (ps) {
        uintptr_t lp = 0;
        if (rd(ps + off::PLAYER_LocalPlayer, lp) && lp)
            g_local_player.store(lp);
    }
}

// -------- extração de uma entidade de um Model --------
static bool build_entity(uintptr_t model, uintptr_t cam_pos_addr, EspEntity& out, bool is_team) {
    uintptr_t hrp = 0, hum = 0;
    for_each_child(model, [&](uintptr_t child) {
        char nm[64]{};
        if (!inst_name(child, nm, sizeof(nm))) return true;
        if (!strcmp(nm, "HumanoidRootPart")) hrp = child;
        else if (!strcmp(nm, "Humanoid"))     hum = child;
        return !(hrp && hum);
    });
    if (!hrp || !hum) return false;

    float hp = 0, maxhp = 0;
    if (!rd(hum + off::HUM_Health, hp))         return false;
    if (!rd(hum + off::HUM_MaxHealth, maxhp))   return false;
    if (maxhp <= 0 || hp <= 0) return false;

    uintptr_t prim = 0;
    if (!rd(hrp + off::BP_Primitive, prim) || !prim) return false;

    Vec3 pos{}, vel{};
    if (!rd(prim + off::PRIM_Position, pos)) return false;
    rd(prim + off::PRIM_AssemblyLinearVel, vel);

    Vec2 head_s{}, foot_s{};
    if (!world_to_screen({pos.x, pos.y + 1.5f, pos.z}, head_s)) return false;
    if (!world_to_screen({pos.x, pos.y - 1.5f, pos.z}, foot_s)) return false;

    float h = std::fabs(head_s.y - foot_s.y);
    if (h < 4.0f || h > 1200.0f) return false;
    float w = h * 0.55f;

    Vec3 cam_pos{};
    rd(cam_pos_addr, cam_pos);

    out.valid         = true;
    out.box_tl        = { head_s.x - w * 0.5f, head_s.y };
    out.box_size      = { w, h };
    out.head_screen   = head_s;
    out.head_world    = { pos.x, pos.y + 1.5f, pos.z };
    out.velocity_world = vel;
    out.health_pct    = hp / maxhp;
    out.is_team       = is_team;
    Vec3 d{ pos.x - cam_pos.x, pos.y - cam_pos.y, pos.z - cam_pos.z };
    out.distance      = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
    inst_name(model, out.name, sizeof(out.name));
    return true;
}

// -------- tick principal --------
static void tick() {
    resolve_chain();

    uintptr_t ps = g_players_svc.load();
    uintptr_t cam = g_cam_addr.load();
    if (!ps || !cam) { g_esp_count.store(0); return; }

    uintptr_t cam_pos_addr = cam + off::CAM_Position;

    // time do local player
    uintptr_t local_team = 0;
    uintptr_t lp = g_local_player.load();
    if (lp) rd(lp + off::PLAYER_Team, local_team);

    int count = 0;
    bool team_check = g_settings.esp_team_check.load();

    for_each_child(ps, [&](uintptr_t pobj) {
        if (count >= 64) return false;

        // pula o local player
        if (lp && pobj == lp) return true;

        uintptr_t model = 0;
        if (!rd(pobj + off::PLAYER_ModelInstance, model) || !model) return true;

        uintptr_t pteam = 0;
        rd(pobj + off::PLAYER_Team, pteam);
        bool is_team = (team_check && local_team && pteam && pteam == local_team);
        if (is_team) return true;

        EspEntity e{};
        if (build_entity(model, cam_pos_addr, e, is_team)) {
            g_esp[count++] = e;
        }
        return true;
    });

    g_esp_count.store(count);
}

void start_esp_thread() {
    resolve_base();
    while (true) {
        if (g_settings.esp_enabled.load() || g_settings.aim_enabled.load()) tick();
        else g_esp_count.store(0);
        usleep(8000);       // ~120 Hz
    }
}