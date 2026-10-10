// fr_hd_ui.h — FireRed Remaster HD interface.
//
// A host-drawn, resolution-independent UI that replaces the game's own
// window graphics: dialogue, menus, the pause menu, battle HUD and the big
// menu screens are redrawn with vector panels, TrueType text and upscaled
// ROM sprites, while the game keeps running its own menu logic underneath
// (cursor positions, selections and every action stay the game's). The
// replaced BG layers / sprites are hidden in the presentation only.

#pragma once

#include <string>

namespace gbarecomp { struct RunOptions; class HostOverlay; }

namespace frlg {

void install_hd_ui(gbarecomp::RunOptions& opts);
void hd_ui_rom_loaded();
// Called once per guest frame (VBlank, before the next frame is drawn):
// decides which layers/sprites the next frame hides.
void hd_ui_guest_frame();

// The HD interface is split into parts the player can switch separately.
enum HdPart {
    kHdPartBattle = 0,   // battle HUD
    kHdPartText,         // field menus: Start menu, Yes/No, lists, popups
    kHdPartTitle,        // title-screen main menu (Continue / New Game)
    kHdPartDex,          // Pokédex
    kHdPartScreens,      // bag, party, summary, PC, maps, naming, options ...
    kHdPartDialogue,     // field dialogue boxes (kHdPartText = the other field windows)
    kHdPartCount
};
bool hd_ui_part(HdPart part);
void set_hd_ui_part(HdPart part, bool on);
bool hd_ui_enabled();            // any part on
void set_hd_ui_enabled(bool on); // all parts
bool hd_ui_modern_text();
void set_hd_ui_modern_text(bool on);

// Mouse targets laid out by the last HD frame (view coordinates).
enum HdHitKind {
    kHdHitNone = 0,
    kHdHitMenu,          // sMenu item index
    kHdHitList,          // ListMenu absolute item index
    kHdHitBattleAction,  // 0 FIGHT 1 BAG 2 POKéMON 3 RUN
    kHdHitBattleMove,    // move slot 0..3
    kHdHitParty,         // party slot 0..5, 7 = CANCEL
    kHdHitMainMenu,      // title main menu box
    kHdHitOption,        // OPTION page row
    kHdHitKey,           // press a GBA key (index = KEYINPUT bit)
    kHdHitAdvance,       // advance text (A)
    kHdHitSetting,       // settings page row: index = row | (dir + 1) << 8
    kHdHitOptionsTab,    // OPTION tabs: 0 game page, 1 system page
    kHdHitBoxCell,       // PC storage cursor target: area * 64 + position
    kHdHitBoxScroll,     // PC storage box arrows: 0 previous, 1 next
    kHdHitMapCell,       // region map cursor cell: y * 32 + x
    kHdHitSummaryMove,   // summary move list row 0..4 (4 = new move / back)
    kHdHitNamingKey,     // naming keyboard: row * 16 + col; 0xFD delete, 0xFE page, 0xFF OK
    kHdHitSettingsCat,   // settings category tab: 0 main, 1 controls, 2 interface, 3 extras
};
struct HdHit {
    HdHitKind kind = kHdHitNone;
    int index = 0;
};
bool hd_ui_hit(float view_x, float view_y, HdHit* out);
// True while the HD layer owns the screen's pointer targets (the native
// GBA-layout hit tests must not run).
bool hd_ui_owns_pointer();
// Draw the HD settings page (page 2 / stand-alone) from the game-menu hook.
void hd_ui_render_menu(gbarecomp::HostOverlay* ov);

}  // namespace frlg
