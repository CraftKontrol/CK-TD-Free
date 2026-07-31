# GridPOPLOD 
 Version 1.039
# Author: Arnaud Cassone © Artcraft Visuals

A geometry-clipmap style LOD plane: dense tessellation near the camera,
coarse far away, entirely driven by one `GLSL Advanced POP` compute shader.
Vertices sit on a **fixed world-space lattice** - a given lattice point is
always at the exact same position every frame; the camera only chooses
which snapped window of each detail level is active, so there is no
continuous vertex movement/shimmer (positions only jump a whole cell at a
time when the camera crosses a snap boundary).

 Features:
- Nested square LOD "rings" (levels), each a fixed `Levelres x Levelres`
  grid. Level 0 spacing = `Mintess`; each further level doubles the spacing
  (`Mintess * 2^level`), automatically stopping once coverage reaches
  `Fardist` or spacing would exceed `Maxtess` (whichever comes first).
- Each level's own window snaps to the nearest multiple of its spacing, so
  vertices are always exactly on that level's world-space lattice - fixed,
  not continuously camera-dependent.
- Level `L>0` leaves a hole in its inner half where the next finer level
  already covers the same area at 2x detail (written as zero-area
  degenerate quads, not compacted out - simple, fixed buffer layout).
- No stitching between levels - expect a minor seam where levels meet
  (accepted tradeoff for simplicity).
- Optional **Infinite Plane** mode: no clamping to `rectangle1`'s bounds at

## Parameters
| Parameter | Type | Description |
|----------------------|------|---------------------------------|
|Cam|COMP||
|Mintess|Float||
|Maxtess|Float||
|Fardist|Float||
|Infiniteplane|Toggle||
|Mingridres|Int||
|Maxgridres|Int||
|Levelres|Int||
|Maxlevels|Int||