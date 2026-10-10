#include "recomp_runtime_ui.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int get_value(void *context, const RecompRuntimeUiItem *, int *out) {
    *out = *static_cast<int *>(context);
    return 1;
}

int set_value(void *context, const RecompRuntimeUiItem *, int value) {
    *static_cast<int *>(context) = value;
    return 1;
}

void render_case(uint32_t presentation_flags, ImVec2 display,
                 float minimum_width, float minimum_height,
                 float maximum_width) {
    static const char *const modes[] = { "Standard (4:3)", "16:9", "Adaptive" };
    static const RecompRuntimeUiItem items[] = {
        { "view", "Display", "View mode", "Choose the visible game area.",
          RECOMP_RUNTIME_UI_CHOICE, 0, 2, 1, modes, 3, nullptr },
    };
    int value = 0;
    RecompRuntimeUiConfig config{};
    config.title = "Runtime UI";
    config.subtitle = "NINTENDO 64";
    config.items = items;
    config.item_count = 1;
    config.theme = "n64";
    config.callbacks.context = &value;
    config.callbacks.get_value = get_value;
    config.callbacks.set_value = set_value;
    config.presentation_flags = presentation_flags;

    RecompRuntimeUi *ui = recomp_runtime_ui_create(&config);
    assert(ui != nullptr);
    recomp_runtime_ui_open(ui);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = display;
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char *font_pixels = nullptr;
    int font_w = 0, font_h = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_w, &font_h);

    ImGui::NewFrame();
    recomp_runtime_ui_render_imgui(ui);
    ImGuiWindow *window = ImGui::FindWindowByName("##recomp-runtime-ui");
    assert(window != nullptr);
    std::fprintf(stderr, "runtime-ui test display=%.0fx%.0f window=%.0fx%.0f\n",
                 display.x, display.y, window->Size.x, window->Size.y);
    assert(window->Size.x >= minimum_width);
    assert(window->Size.y >= minimum_height);
    assert(window->Size.x <= maximum_width);
    const float first_frame_font_scale = window->FontWindowScale;
    ImGui::Render();
    assert(ImGui::GetDrawData()->CmdListsCount > 0);

    ImGui::NewFrame();
    recomp_runtime_ui_render_imgui(ui);
    window = ImGui::FindWindowByName("##recomp-runtime-ui");
    assert(window != nullptr);
    assert(std::fabs(window->FontWindowScale - first_frame_font_scale) <
           0.001f);
    ImGui::Render();

    ImGui::DestroyContext();
    recomp_runtime_ui_destroy(ui);
}

// A toast draws with the menu closed (its own window, no menu window), and
// both draw when the menu is open.
void toast_case() {
    static const RecompRuntimeUiItem items[] = {
        { "x", "Display", "X", nullptr, RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, nullptr, 0, nullptr },
    };
    int value = 0;
    RecompRuntimeUiConfig config{};
    config.items = items;
    config.item_count = 1;
    config.theme = "nes";
    config.callbacks.context = &value;
    config.callbacks.get_value = get_value;
    config.callbacks.set_value = set_value;
    RecompRuntimeUi *ui = recomp_runtime_ui_create(&config);
    assert(ui != nullptr);

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(768.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char *font_pixels = nullptr;
    int font_w = 0, font_h = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_w, &font_h);

    ImGui::NewFrame();
    recomp_runtime_ui_render_imgui(ui);                  // nothing to draw
    ImGui::Render();
    assert(ImGui::GetDrawData()->CmdListsCount == 0);

    recomp_runtime_ui_set_toast(ui, "DISK 1 SIDE A", "MOTOR OFF\nD AGAIN: DISK 1 SIDE B");
    for (int frame = 0; frame < 2; ++frame) {            // auto-resize settles on frame 2
        ImGui::NewFrame();
        recomp_runtime_ui_render_imgui(ui);
        ImGui::Render();
    }
    ImGuiWindow *toast = ImGui::FindWindowByName("##recomp-runtime-ui-toast");
    assert(toast != nullptr && toast->Active);
    ImGuiWindow *menu = ImGui::FindWindowByName("##recomp-runtime-ui");
    assert(menu == nullptr || !menu->Active);
    assert(toast->Pos.y < 60.0f && toast->Size.y > 30.0f);
    assert((toast->Flags & ImGuiWindowFlags_NoInputs) != 0);

    recomp_runtime_ui_open(ui);
    ImGui::NewFrame();
    recomp_runtime_ui_render_imgui(ui);
    ImGui::Render();
    assert(ImGui::FindWindowByName("##recomp-runtime-ui")->Active);
    assert(ImGui::FindWindowByName("##recomp-runtime-ui-toast")->Active);

    recomp_runtime_ui_close(ui);
    recomp_runtime_ui_set_toast(ui, nullptr, nullptr);
    ImGui::NewFrame();
    recomp_runtime_ui_render_imgui(ui);
    ImGui::Render();
    assert(ImGui::GetDrawData()->CmdListsCount == 0);
    ImGui::DestroyContext();
    recomp_runtime_ui_destroy(ui);
}

} // namespace

int main() {
    toast_case();
    render_case(0, ImVec2(1280.0f, 720.0f), 779.0f, 671.0f, 781.0f);
    render_case(RECOMP_RUNTIME_UI_PRESENTATION_TOUCH_FRIENDLY,
                ImVec2(3088.0f, 1440.0f), 3000.0f, 1300.0f, 3020.0f);
    return 0;
}
