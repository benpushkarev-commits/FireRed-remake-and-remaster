// host_window.h — minimal host window + input surface.
//
// Soft-dependency on SDL2. When the build can find SDL2 the cpp
// uses it; otherwise the same symbols compile as no-op stubs so
// headless builds (CI, BIOS smoke without --window) still link.
//
// The window owns a logical-size streaming texture matching the active GBA
// framebuffer pixel format (RGB888). Expanded views opt into a resizable,
// aspect-correct viewport; the faithful 240x160 path retains the historical
// fixed SDL presentation. pump() drains the OS event queue, returns a quit flag
// and a packed GBA KEYINPUT value (active-low, 1 = released).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct RecompRuntimeUi;

namespace gbarecomp {

class HostOverlay;

class HostWindow {
public:
    HostWindow();
    ~HostWindow();

    HostWindow(const HostWindow&) = delete;
    HostWindow& operator=(const HostWindow&) = delete;

    // True if this build was compiled against a real windowing
    // backend. When false, open() always fails.
    static bool is_available();

    // Open a window. `scale` is the integer scale factor applied to
    // the logical surface, whose size is `base_w` x `base_h` (240x160 for the
    // faithful view, wider when view-area expansion is active). Returns false on
    // failure (also when is_available() is false).
    // `screen` is the per-game color model from [video].screen in game.toml
    // (raw|unlit|frontlit|backlit|classic), or nullptr for none. The
    // GBARECOMP_SCREEN env var, when set, overrides it.
    // `linear_filter` selects ordinary linear scaling. `sharp_filter` uses a
    // GPU integer prescale followed by a small fractional linear pass and
    // takes precedence when both are requested.
    bool open(int scale = 3, int base_w = 240, int base_h = 160,
              const char* title = "gbarecomp", const char* screen = nullptr,
              bool linear_filter = false, bool sharp_filter = false,
              bool resize_driven_view = false,
              bool freely_resizable_window = false);
    void close();
    void set_title(const char* title);
    bool is_open() const { return open_; }

    // Resize the logical streaming surface without changing the host window.
    // Used only by the explicit resize-driven view policy; fixed-width callers
    // never invoke it. drawable_size() reports the live client-window extent
    // (and therefore follows drag-resize and borderless desktop fullscreen).
    bool set_surface_size(int base_w, int base_h);
    bool drawable_size(int* width, int* height) const;

    // Load player keybinds + system hotkeys from `dir` (the exe directory):
    //   * keybinds.ini — recomp-ui's generic keybinds format ([player1],
    //     SDL scancode names). Absent file => the built-in defaults below,
    //     which MATCH recomp-ui's defaults so the launcher rebind page and
    //     the game always agree: A=X B=Z L=C R=V Start=Return Select=RShift
    //     + arrow keys.
    //   * config.ini [KeyMap] — hotkey bindings (SDL keycode names with
    //     Ctrl+/Alt+/Shift+ prefixes): Fullscreen, Pause, Turbo,
    //     WindowBigger, WindowSmaller, VolumeUp, VolumeDown, DisplayPerf.
    // Never called => built-in defaults for both. Safe to call when the
    // files don't exist.
    void load_input_config(const char* dir,
                           bool assist_tools_default = true,
                           int fast_forward_multiplier_default = 4);

    // Live window/audio controls (hotkey + launcher-driven). All no-ops when
    // the window isn't open or this build has no SDL2.
    // Tri-state fullscreen mode: 0 windowed, 1 borderless
    // (SDL_WINDOW_FULLSCREEN_DESKTOP), 2 exclusive (SDL_WINDOW_FULLSCREEN).
    // Out-of-range values clamp to 0..2. fullscreen() reports the current mode.
    void set_fullscreen(int mode);
    int  fullscreen() const;
    void adjust_scale(int delta);       // integer window scale, clamped 1..8
    void set_volume(int pct);           // 0..100, applied to pushed samples
    int  volume() const;
    int  window_scale() const;
    void set_linear_filter(bool enabled);
    bool linear_filter() const;
    void set_audio_enabled(bool enabled);
    bool audio_enabled() const;
    void set_resize_driven_view(bool enabled);
#if defined(GBARECOMP_RUNTIME_UI)
    // Attach the capability-only shared model. HostWindow presents it through
    // Dear ImGui's SDL_Renderer2 backend over the existing game renderer.
    void set_runtime_ui(RecompRuntimeUi* ui);
#endif
    // ---- touch (touch_input.h) ---------------------------------------------
    // `policy` = the game consumes touch through RunOptions::input_frame;
    // non-pad touches are then routed to the TouchHub instead of only opening
    // settings. `claims` are the game's TouchClaim bits. `pad_default` is the
    // virtual-pad visibility when config.ini holds no saved choice (-1 =
    // platform default). `emulate_touch` turns desktop mouse input into touch
    // (left = finger, right = two-finger tap, middle = three-finger tap,
    // Backspace = platform Back) for development and validation.
    void configure_touch(bool policy, std::uint32_t claims, int pad_default,
                         bool emulate_touch);
    // Loads/persists the player's pad choice in <dir>/config.ini [Touch].
    void set_touch_config_dir(const char* dir);
    bool touch_pad_visible() const;
    void set_touch_pad_visible(bool visible);
    // Logical view margins inside the surface (for touch/overlay mapping).
    void set_view_margins(std::uint32_t left, std::uint32_t right,
                          std::uint32_t top, std::uint32_t bottom);
    // Game-owned host overlay, drawn each present (null = none).
    void set_host_overlay(void (*overlay)(HostOverlay*));
    // Called before every ImGui frame with the drawable size; the game may
    // (re)build fonts in the ImGui atlas there and return true, after which
    // the atlas texture is re-uploaded. Runtime-UI builds only.
    void set_host_ui_prepare(bool (*prepare)(int drawable_w, int drawable_h));
    // The SDL_Renderer* behind the window (nullptr without SDL), for host
    // chrome that uploads its own textures (ImTextureID = SDL_Texture*).
    void* sdl_renderer() const;

    // ---- game-owned modal menu ----------------------------------------------
    // A cartridge may supply its own settings screen (drawn in the game's own
    // style). While it reports open, every key/pad event is routed to
    // `input` instead of the guest, Esc/Guide open it when closed, `render`
    // replaces the runtime menu on each present, and the runtime holds the
    // guest still. Keys still held when it closes are swallowed until
    // released so the confirming press never leaks into the game.
    struct GameMenuHooks {
        bool (*is_open)() = nullptr;
        void (*open)() = nullptr;
        // kind: see GameMenuInputKind.
        void (*input)(int kind, bool repeat) = nullptr;
        void (*render)(HostOverlay* overlay) = nullptr;
        // While closed: offered each mapped key press first. Returning true
        // consumes it (the guest never sees it, and it stays masked until
        // released) — e.g. to page from a guest screen into the game menu.
        bool (*peek)(int kind, bool repeat) = nullptr;
        // false: a closed menu leaves Escape to the game's input policy (the
        // menu opens from its own entry points instead).
        bool esc_opens = true;
    };
    enum GameMenuInputKind {
        kMenuUp = 0, kMenuDown, kMenuLeft, kMenuRight,
        kMenuAccept, kMenuBack, kMenuClose, kMenuPageLeft, kMenuPageRight,
        kMenuStart,
    };
    void set_game_menu(const GameMenuHooks& hooks);
    bool game_menu_open() const;

    // ---- keyboard bindings for the ten GBA inputs (KEYINPUT bit order) ----
    // Each input accepts up to kKeySlots keys (slot 0 = primary).
    static constexpr int kKeySlots = 3;
    int  key_binding(int bit, int slot = 0) const;   // SDL scancode, 0 = unbound
    const char* key_binding_name(int bit, int slot = 0) const; // "" when unbound
    void set_key_binding(int bit, int scancode, int slot = 0);
    void reset_key_bindings();                  // defaults (set_default_key_bindings)
    bool save_key_bindings() const;             // <config dir>/<keybinds file>
    // Next key press binds `bit`/`slot` (and unbinds that key from any other
    // input); Esc cancels. key_capture_bit() is -1 when none is pending.
    void begin_key_capture(int bit, int slot = 0);
    int  key_capture_bit() const;
    int  key_capture_slot() const;
    // A game menu that reads raw keys itself (fast-forward key capture): every key event is swallowed so
    // hotkeys, save-state keys and fullscreen cannot fire; read the keys with key_down().
    // Game policy for key binding: called with the GBA input bit and the SDL scancode when a captured key is
    // about to be bound; return 0 to accept, any other value refuses it (the capture stays pending and
    // key_capture_refusal() reports the code until the capture ends). The engine stays generic about why.
    void set_key_bind_filter(int (*filter)(int bit, int scancode));
    int  key_capture_refusal() const;
    void set_raw_key_capture(bool on);
    bool raw_key_capture() const;
    // Raw keyboard state for game input policies (SDL scancode).
    bool key_down(int scancode) const;

    // ---- mouse, for game input policies ------------------------------------
    struct MouseState {
        bool  inside = false;          // pointer over the presented game image
        float view_x = 0, view_y = 0;  // logical view pixels incl. margins
        int   margin_left = 0;         // native x = view_x - margin_left
        unsigned buttons = 0;          // held: bit0 left, bit1 right, bit2 middle
        unsigned pressed = 0;          // went down since the previous take_mouse()
        unsigned released = 0;
        int   wheel = 0;               // notches since the previous take (+ = up)
        bool  moved = false;           // pointer motion since the previous take
        unsigned idle_ms = 0;          // time since the pointer last moved
    };
    MouseState take_mouse();
    // Hide the OS cursor over the window after ~2.5 s without mouse motion;
    // any motion shows it again.
    void set_cursor_autohide(bool enabled);
    // Game controller layout: swap_ab puts GBA A on the east face button
    // (Nintendo style); left_stick makes the left stick drive the D-pad.
    void set_pad_options(bool swap_ab, bool left_stick);
    bool controller_connected() const;
    // Close the game at the next pump, exactly like the window's close box.
    void request_quit();
    // Text entry (e.g. a name screen): while on, character keys (letters,
    // digits, space, punctuation, Enter) stop driving GBA inputs and the
    // typed text is collected for take_text() (UTF-8) instead.
    void set_text_input(bool on);
    std::string take_text();
    // Physical density of the window's display (drawable px per mm).
    float px_per_mm() const;
    // Portrait presentations may anchor the image to the top edge so host
    // chrome below it gets the remaining space (per-present game request).
    void set_presentation_anchor_top(bool anchor_top);
    // Orientation policy for rotating platforms: 0 landscape, 1 portrait,
    // 2 any. Must be set BEFORE open(); ignored on desktop.
    void set_orientation_policy(int policy) { orientation_policy_ = policy; }

    // Mobile lifecycle. pump() reports entering the background once; the
    // runtime then persists state and calls wait_for_foreground(), which
    // blocks (audio paused) until the app returns (true) or quits (false).
    bool wait_for_foreground();

    // Short vibration where the platform supports it (no-op otherwise).
    void haptic_pulse(int duration_ms, float strength);

    void set_fps_readout(bool on);      // presents-per-second in the title bar
    bool fps_readout() const;
    bool assist_tools_enabled() const;
    int  fast_forward_multiplier() const;

    // Upload one base_w x base_h RGB888 frame (the dimensions passed to open())
    // and present.
    void present(const uint8_t* rgb888);

    // Push `count` int16_t mono samples (32.768 kHz) into the audio
    // output queue. Backend converts to the host device's format.
    // No-op if audio init failed or this build has no SDL2.
    void push_audio_samples(const int16_t* samples, std::size_t count);

    // Service the native window-system queue without consuming input events.
    // Long guest frames use this to remain responsive between presentations.
    void service_events();

    struct Events {
        bool     quit = false;
        // GBA KEYINPUT layout. Active-low: 1 = released, 0 = pressed.
        // Bits: 0=A 1=B 2=Sel 3=Sta 4=Right 5=Left 6=Up 7=Down 8=R 9=L.
        uint16_t keyinput = 0x03FF;
        // Edge-triggered save-state slot hotkeys. F1..F9 load slot
        // 1..9; Shift+F1..F9 save slot 1..9. 0 = no request this pump.
        // The caller acts on these at the top of the loop (a clean
        // dispatch boundary), never mid-frame.
        int      save_slot = 0;
        int      load_slot = 0;
        // Optional, edge-triggered solar controls from config.ini [KeyMap].
        // They have no built-in bindings; recomp-ui exposes them only for
        // cartridges that declare a solar sensor.
        bool     solar_brighter = false;
        bool     solar_dimmer = false;
        bool     solar_live = false;
        // Level-triggered: true while the fast-forward (Turbo) binding is
        // held (default Tab). Uncaps the frame limiter for as long as it's
        // down.
        bool     fast_forward = false;
        // Repeating global Assist binding. It fires immediately, then every
        // half-second while held so one-second rewind steps move backward at
        // approximately normal speed.
        bool     rewind = false;
        // Edge-triggered system hotkeys (config.ini [KeyMap] bindings; see
        // load_input_config). The caller owns the semantics: fullscreen and
        // window scale route back into this window, pause gates stepping in
        // the run loop, volume adjusts pushed-sample gain, FPS toggles the
        // title-bar readout.
        bool     toggle_fullscreen = false;
        bool     toggle_pause = false;
        bool     window_bigger = false;
        bool     window_smaller = false;
        bool     volume_up = false;
        bool     volume_down = false;
        bool     toggle_fps = false;
        // Horizontal mouse velocity while the left button is held. The runtime
        // maps this host-neutral delta onto cartridge gyroscope sample units.
        int      gyro_delta_x = 0;
        bool     mouse_gyro_active = false;
        // Controller angular velocity around its face-normal axis, in rad/s.
        // A DualSense supplies this through SDL's standard gyro sensor API.
        float    gyro_rate_z = 0.0f;
        // Mobile lifecycle: the OS is moving the app to the background (or
        // terminating it). The runtime must flush saves NOW; see
        // wait_for_foreground().
        bool     enter_background = false;
        bool     terminating = false;
    };
    Events pump();

private:
    bool open_ = false;
    void* impl_ = nullptr;  // backend-specific opaque
    int orientation_policy_ = 0;
};

// Engine-wide haptic hook for game input policies (touch_input.h users).
// Installed by the open HostWindow; a no-op when no window/vibrator exists.
void host_haptic_pulse(int duration_ms, float strength);

// Ask the open window to show the runtime settings menu at its next pump
// (thread-safe). Lets a policy that claims Back/long-press still offer the
// menu from a context of its choosing, e.g. Back on the free overworld.
void host_request_settings_menu();

// The currently open HostWindow (nullptr when none). Game-owned menus use it
// to reach window/audio/binding controls without plumbing a reference.
HostWindow* active_host_window();

// Per-game keyboard defaults and binding file, set before the window opens.
// table[bit][slot] holds SDL scancodes (0 = none). Without a call the
// recomp-ui generic defaults (A=X B=Z ...) and keybinds.ini apply.
void set_default_key_bindings(const int table[10][HostWindow::kKeySlots]);
void set_keybinds_file(const char* file_name);
// Override a config.ini [KeyMap] hotkey default ("Turbo", "DisplayPerf", ...;
// "" = unbound) before the window opens; the player's [KeyMap] still wins.
void set_hotkey_default(const char* name, const char* value);
// F1..F9 / Shift+F1..F9 save-state slots (default on).
void set_savestate_hotkeys_enabled(bool enabled);

}  // namespace gbarecomp
