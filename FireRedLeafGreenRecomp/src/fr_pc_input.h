// fr_pc_input.h — FireRed Remaster PC controls (keyboard + mouse).
//
// Keyboard: WASD or arrows move, E / Enter / Space confirm, Q / Backspace go
// back, Tab opens the pause menu, F uses the registered item, Z / C are L / R.
// Escape opens the pause menu while walking and backs out everywhere else;
// Shift held runs (or walks, with AUTO RUN on). Every key is rebindable in
// OPTION → page 2 → CONTROLS (controls.ini), and the game's own button
// prompts are redrawn with the bound keys.
//
// Mouse: hovering a menu item, list row, battle command or move moves the
// game's own cursor there (verified D-pad presses, so sounds/redraws run as
// normal); left-click picks it, right-click goes back, the wheel scrolls,
// and a left-click anywhere else advances text. Right-click while walking
// opens the pause menu.
//
// The game is steered only through synthesized key presses; guest state is
// read, never written.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace gbarecomp { struct RunOptions; }

namespace frlg {

void install_pc_input(gbarecomp::RunOptions& opts, const std::string& exe_dir);

// ROM image is loaded (in-memory, writable): redraw the button prompts.
void pc_input_rom_loaded(std::uint8_t* rom, std::size_t size);
// Key bindings changed: redraw the button prompts.
void pc_input_bindings_changed();

bool pc_auto_run();
void set_pc_auto_run(bool on);
bool pc_mouse_enabled();
void set_pc_mouse_enabled(bool on);
// Write config.ini [Remaster] (auto run, mouse, HD interface, modern text).
void save_remaster_config();

// Game controller layout (applied to the window on the next frame).
bool pc_pad_swap_ab();            // Nintendo layout: right face button confirms
void set_pc_pad_swap_ab(bool on);
bool pc_pad_stick();              // left stick moves like the D-pad
void set_pc_pad_stick(bool on);
bool pc_controller_connected();
// Fast forward: speed 2..10x, a bindable key (SDL scancode) that toggles
// or is held; the music is muted while it runs.
int pc_ff_speed();
void set_pc_ff_speed(int x);
bool pc_ff_toggle();
void set_pc_ff_toggle(bool on);
int pc_ff_key();
void set_pc_ff_key(int scancode);
std::string pc_ff_key_name();
// The GBA input bit (0..9) that is ALSO bound to the fast-forward key, -1 when none. Such a key belongs to the game input:
// fast forward is inactive for it (see ini::ff_runtime_active), nothing is deleted from the user's bindings.
int pc_ff_conflict_bit();
int pc_fast_forward_now();        // current multiplier, 0 when off
// Infinite Rare Candy (the Items pocket always holds 99).
bool pc_rare_candy();
void set_pc_rare_candy(bool on);
// Experimental tilted 3D view of the overworld (F3 toggles).
bool pc_diorama();
void set_pc_diorama(bool on);
// First-run setup screen finished (config.ini [Remaster] setup_done).
bool pc_setup_done();
void set_pc_setup_done(bool on);

// Friendly upper-case key name for menus ("E", "ENTER", "L SHIFT").
std::string pc_key_name(const char* sdl_scancode_name);

}  // namespace frlg
