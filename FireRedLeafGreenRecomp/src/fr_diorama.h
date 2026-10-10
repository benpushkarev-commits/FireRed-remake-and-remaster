// fr_diorama.h — tilted "diorama" 3D view of the overworld.
//
// Presentation only (reads guest RAM/VRAM/OAM, never writes guest state).
// Towns, routes and rooms are redrawn with a perspective camera by a small
// software rasterizer with a depth buffer. Geometry comes from an explicit
// table per tileset (flat ground, standing objects, individual tree models,
// building and furniture boxes, tall grass); tiles without a table entry stay
// flat, buildings without one fall back to "solid block with a door = box".
// Every sprite stands up at its feet. Scenes the view does not understand
// (caves' cutscene camera, battles, fades to another scene) fall back to 2D.

#pragma once

namespace gbarecomp { class HostOverlay; }

namespace frlg {

// Draw the 3D view for this present (before the HD interface, which draws
// its windows on top). Does nothing while the view is off or not usable.
void diorama_draw(gbarecomp::HostOverlay* ov);

}  // namespace frlg
