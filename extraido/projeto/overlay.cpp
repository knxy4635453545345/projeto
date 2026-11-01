// language: C++, file: menu/overlay.cpp
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <unistd.h>
#include <android/log.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "dobby.h"
#include "hellborn.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "HB_UI", __VA_ARGS__)

static bool         g_init   = false;
static bool         g_menu_open = true;

typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

static void style_hellborn() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding   = 10.0f;
    s.FrameRounding    = 6.0f;
    s.GrabRounding     = 4.0f;
    s.ItemSpacing      = ImVec2(10, 8);
    s.WindowPadding    = ImVec2(14, 12);
    s.Colors[ImGuiCol_WindowBg]        = ImVec4(0.05f, 0.05f, 0.07f, 0.96f);
    s.Colors[ImGuiCol_TitleBg]         = ImVec4(0.45f, 0.02f, 0.02f, 1.0f);
    s.Colors[ImGuiCol_TitleBgActive]   = ImVec4(0.72f, 0.04f, 0.04f, 1.0f);
    s.Colors[ImGuiCol_Header]          = ImVec4(0.55f, 0.05f, 0.05f, 1.0f);
    s.Colors[ImGuiCol_HeaderHovered]   = ImVec4(0.75f, 0.08f, 0.08f, 1.0f);
    s.Colors[ImGuiCol_CheckMark]       = ImVec4(1.00f, 0.15f, 0.15f, 1.0f);
    s.Colors[ImGuiCol_SliderGrab]      = ImVec4(0.90f, 0.12f, 0.12f, 1.0f);
    s.Colors[ImGuiCol_Button]          = ImVec4(0.45f, 0.05f, 0.05f, 1.0f);
    s.Colors[ImGuiCol_ButtonHovered]   = ImVec4(0.72f, 0.08f, 0.08f, 1.0f);
    s.Colors[ImGuiCol_FrameBg]         = ImVec4(0.10f, 0.10f, 0.13f, 1.0f);
}

static void init_imgui(EGLDisplay dpy, EGLSurface surf) {
    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, surf, EGL_WIDTH,  &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    g_screen_w.store((float)w);
    g_screen_h.store((float)h);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename   = nullptr;
    io.DisplaySize   = ImVec2((float)w, (float)h);
    io.Fonts->AddFontDefault();

    style_hellborn();
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_init = true;
    LOGI("imgui up %dx%d", w, h);
}

static void draw_menu() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    if (g_menu_open) {
        ImGui::SetNextWindowSize(ImVec2(440, 520), ImGuiCond_FirstUseEver);
        ImGui::Begin("HELLBORN", &g_menu_open, ImGuiWindowFlags_NoCollapse);

        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("Aimbot")) {
                bool v;
                v = g_settings.aim_enabled.load();
                if (ImGui::Checkbox("Enable Aimbot", &v)) g_settings.aim_enabled = v;

                const char* parts[] = { "Head", "HumanoidRootPart", "UpperTorso" };
                int pi = g_settings.aim_part.load();
                if (ImGui::Combo("Aim Part", &pi, parts, 3)) g_settings.aim_part = pi;

                float fov = g_settings.aim_fov.load();
                if (ImGui::SliderFloat("FOV", &fov, 20, 400, "%.0f px")) g_settings.aim_fov = fov;

                float sm = g_settings.aim_smoothing.load();
                if (ImGui::SliderFloat("Smoothing", &sm, 0.01f, 1.0f, "%.2f")) g_settings.aim_smoothing = sm;

                float pr = g_settings.aim_prediction.load();
                if (ImGui::SliderFloat("Prediction", &pr, 0, 0.4f, "%.2f")) g_settings.aim_prediction = pr;

                v = g_settings.aim_team_check.load();
                if (ImGui::Checkbox("Team Check", &v)) g_settings.aim_team_check = v;

                v = g_settings.aim_visible_check.load();
                if (ImGui::Checkbox("Visible Check", &v)) g_settings.aim_visible_check = v;

                ImGui::Spacing();
                ImGui::TextDisabled("Segure o botao AIM na tela para puxar");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("ESP")) {
                bool v;
                v = g_settings.esp_enabled.load();
                if (ImGui::Checkbox("Master ESP", &v)) g_settings.esp_enabled = v;
                v = g_settings.esp_box.load();
                if (ImGui::Checkbox("Box", &v)) g_settings.esp_box = v;
                v = g_settings.esp_health.load();
                if (ImGui::Checkbox("Health", &v)) g_settings.esp_health = v;
                v = g_settings.esp_name.load();
                if (ImGui::Checkbox("Name", &v)) g_settings.esp_name = v;
                v = g_settings.esp_distance.load();
                if (ImGui::Checkbox("Distance", &v)) g_settings.esp_distance = v;
                v = g_settings.esp_tracer.load();
                if (ImGui::Checkbox("Tracer", &v)) g_settings.esp_tracer = v;
                v = g_settings.esp_team_check.load();
                if (ImGui::Checkbox("Team Check", &v)) g_settings.esp_team_check = v;

                float col[3] = { g_settings.esp_color[0], g_settings.esp_color[1], g_settings.esp_color[2] };
                if (ImGui::ColorEdit3("Enemy Color", col)) {
                    g_settings.esp_color[0] = col[0];
                    g_settings.esp_color[1] = col[1];
                    g_settings.esp_color[2] = col[2];
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings")) {
                ImGui::Text("HELLBORN native — arm64-v8a");
                ImGui::Separator();
                if (ImGui::Button("Close menu")) g_menu_open = false;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    // ESP draw sobre o jogo (background draw list)
    if (g_settings.esp_enabled.load()) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        int n = g_esp_count.load();
        for (int i = 0; i < n; i++) {
            const EspEntity& e = g_esp[i];
            if (!e.valid) continue;
            ImU32 col = IM_COL32(
                (int)(g_settings.esp_color[0] * 255),
                (int)(g_settings.esp_color[1] * 255),
                (int)(g_settings.esp_color[2] * 255),
                255);
            if (g_settings.esp_box.load()) {
                dl->AddRect(ImVec2(e.box_tl.x, e.box_tl.y),
                            ImVec2(e.box_tl.x + e.box_size.x, e.box_tl.y + e.box_size.y),
                            col, 0, 0, 2.0f);
            }
            if (g_settings.esp_name.load()) {
                dl->AddText(ImVec2(e.head_screen.x - 30, e.box_tl.y - 16), col, e.name);
            }
            if (g_settings.esp_distance.load()) {
                char buf[16]; snprintf(buf, 16, "%.0fm", e.distance);
                dl->AddText(ImVec2(e.head_screen.x - 14, e.box_tl.y + e.box_size.y + 2), IM_COL32(220,220,220,255), buf);
            }
            if (g_settings.esp_health.load()) {
                float h = e.box_size.y * e.health_pct;
                dl->AddRectFilled(ImVec2(e.box_tl.x - 6, e.box_tl.y),
                                  ImVec2(e.box_tl.x - 3, e.box_tl.y + e.box_size.y),
                                  IM_COL32(30,30,30,255));
                dl->AddRectFilled(ImVec2(e.box_tl.x - 6, e.box_tl.y + e.box_size.y - h),
                                  ImVec2(e.box_tl.x - 3, e.box_tl.y + e.box_size.y),
                                  IM_COL32((int)(255*(1-e.health_pct)), (int)(200*e.health_pct), 40, 255));
            }
            if (g_settings.esp_tracer.load()) {
                dl->AddLine(ImVec2(g_screen_w.load()*0.5f, g_screen_h.load()),
                            ImVec2(e.head_screen.x, e.head_screen.y),
                            col, 1.5f);
            }
        }
    }

    // botao "AIM" fixo (touch held)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(g_screen_w.load() - 130, g_screen_h.load() - 130), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(110, 110), ImGuiCond_Always);
        ImGui::Begin("##aim", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBackground);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.05f, 0.05f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 0.9f));
        ImGui::Button("AIM", ImVec2(90, 90));
        g_aimbot_held = ImGui::IsItemActive();
        ImGui::PopStyleColor(2);
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

static EGLBoolean eglSwapBuffers_hook(EGLDisplay dpy, EGLSurface surf) {
    if (!orig_eglSwapBuffers) return EGL_FALSE;
    if (!g_init) init_imgui(dpy, surf);
    if (g_init)  draw_menu();
    return orig_eglSwapBuffers(dpy, surf);
}

void start_overlay() {
    void* h = dlopen("libEGL.so", RTLD_NOW);
    if (!h) { LOGI("libEGL missing"); return; }
    void* target = dlsym(h, "eglSwapBuffers");
    if (!target) { LOGI("eglSwapBuffers missing"); return; }
    if (DobbyHook(target, (void*)eglSwapBuffers_hook, (void**)&orig_eglSwapBuffers) != 0) {
        LOGI("DobbyHook failed");
        return;
    }
    LOGI("eglSwapBuffers hooked");
}