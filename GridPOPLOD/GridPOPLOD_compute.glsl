// GridPOPLOD_compute.glsl
// GLSL Advanced POP compute shader.
// Fixed-lattice geometry-clipmap tessellation of a quad plane.
// Author: Arnaud Cassone - Artcraft Visuals 2026
//
// Unlike a continuous camera-centered warp, every vertex sits on an ABSOLUTE
// world-space lattice: for a given level L, valid positions are exact
// multiples of stepL = MinTess * 2^L from the plane's own origin. The camera
// only selects which snapped window of each level's lattice is currently
// active, so vertices never glide - they only jump an entire cell at a time
// when the camera crosses a snap boundary.
//
// Each level's window is centered on ONE SHARED point: the camera position
// snapped ONCE to the COARSEST active level's step. Because steps double
// per level (stepL = MinTess * 2^L), that single snapped center is
// automatically an exact multiple of every FINER level's step too (a
// multiple of the largest step is a multiple of all its power-of-2
// divisors) - so every level's grid lands on its own lattice points
// relative to the SAME center, and all levels are truly concentric. (An
// earlier version snapped each level's center independently/recursively;
// that gives adjacent levels genuinely different centers and produces a
// visible, accumulating diagonal shift between nested levels - it was
// wrong, not just imprecise.)
//
// Levels are nested square "frames": level 0 is a solid R x R grid; level
// L>0 is the same R x R grid but leaves its inner half as degenerate
// (zero-area) quads, since level L-1 already fully covers that area at
// twice the detail. The hole is tested in CONTINUOUS local space (not
// integer index thresholds like R/4..3R/4, which are asymmetric relative
// to the grid's true center and cause a constant directional shift). No
// cross-level stitching is performed - a minor seam at level boundaries is
// expected and accepted (see AI_CONTEXT.md).
//
// STRUCTURAL CONSTRAINT: the hole's half-width, in the COARSE level's OWN
// cells, is k = (R-1)/4 - this must be a WHOLE number of coarse cells,
// since a quad can only be wholly degenerate or wholly real (no partial
// cells). If (R-1) is not divisible by 4, the hole's true (continuous)
// boundary falls in the middle of a coarse cell, forcing a rounding that
// leaves up to a full cell of gap/overlap between the coarse hole edge and
// the fine level's actual footprint - this is a real, visible shift, not a
// rounding nicety, and no hole-test formula can avoid it for other R. R is
// therefore SNAPPED to the nearest valid 4k+1 (5, 9, 13, 17, ...) right
// after reading LevelRes, below - pop_lod_utils.py snaps identically so
// point/quad counts still match this shader's actual R.
//
// Single dispatch, numLevels*R*R threads (Number of Threads = "Manual
// Number of Elements", custom expression -> pop_lod_utils.numPoints()).
// Each thread writes one output point (P, N, Tex) and, if it is the
// bottom-left corner of a grid cell, also writes that cell's quad (real or
// degenerate) into the index buffer.
//
// Uniforms expected (Vectors page on GridLOD, values driven by custom
// parameters on the parent COMP GridPOPLOD -- see AI_CONTEXT.md):
//   vec3  CamPos         - camera world position        (parent().par.Camx/y/z)
//   float MinTess        - level 0 point spacing (m)     (parent().par.Mintess)
//   float InfinitePlane  - 0 = clamp to input quad bounds, 1 = infinite plane
//   float LevelRes       - R, fixed points per side per level (parent().par.Levelres)
//
// Input 0: exactly one Quad primitive, 4 points, wound BL, BR, TR, TL.
// numLevels is derived CPU-side (pop_lod_utils.levelCount()) from
// Mintess/Maxtess/Fardist/Maxlevels and folded into TDNumElements() via the
// Output page custom expressions; the shader recovers R from the LevelRes
// uniform and numLevels implicitly via TDNumElements()/(R*R).

void main() {
	const uint id = TDIndex();
	if (id >= TDNumElements())
		return;

	// ---- Decode (level, i, j) from the flat thread id ----
	// R is SNAPPED to the nearest valid 4k+1 (see top-of-file comment) -
	// pop_lod_utils.py must compute the identical snapped R for dispatch
	// sizing (TDNumElements()) to stay consistent with this shader.
	const uint rawR = uint(max(LevelRes, 2.0));
	const uint kLevels = max(uint(floor(float(rawR - 1u) / 4.0 + 0.5)), 1u);
	const uint R = 4u * kLevels + 1u;
	const uint cellsPerLevel = R * R;
	const uint level = id / cellsPerLevel;
	const uint localId = id % cellsPerLevel;
	const uint i = localId % R;
	const uint j = localId / R;

	// ---- Read the 4 quad corners (BL, BR, TR, TL) ----
	vec3 P0 = TDInPoint_P(0, 0u, 0u);
	vec3 P1 = TDInPoint_P(0, 1u, 0u);
	vec3 P2 = TDInPoint_P(0, 2u, 0u);
	vec3 P3 = TDInPoint_P(0, 3u, 0u);

	vec3 rightVec = P1 - P0;
	vec3 upVec    = P3 - P0;
	float width  = length(rightVec);
	float height = length(upVec);
	vec3 rightN = (width  > 1e-6) ? (rightVec / width)  : vec3(1.0, 0.0, 0.0);
	vec3 upN    = (height > 1e-6) ? (upVec    / height) : vec3(0.0, 1.0, 0.0);
	vec3 planeNormal = normalize(cross(rightN, upN));
	vec3 planeCenter = (P0 + P1 + P2 + P3) * 0.25;

	float halfW = width  * 0.5;
	float halfH = height * 0.5;

	bool infinite = (InfinitePlane > 0.5);

	// ---- This level's fixed lattice step (doubles every level) ----
	float minTess = max(MinTess, 1e-5);
	float stepL = minTess * pow(2.0, float(level));

	// ---- Project camera onto the plane's local basis ----
	vec3 rel = CamPos - planeCenter;
	float camX = dot(rel, rightN);
	float camY = dot(rel, upN);
	if (!infinite) {
		camX = clamp(camX, -halfW, halfW);
		camY = clamp(camY, -halfH, halfH);
	}

	// ---- SNAP ONCE, to the COARSEST active level's step, and share that
	//      SAME center across every level (see top-of-file comment). ----
	const uint numLevels = uint(TDNumElements()) / cellsPerLevel;
	float coarsestStep = minTess * pow(2.0, float(numLevels - 1u));
	float centerX = floor(camX / coarsestStep + 0.5) * coarsestStep;
	float centerY = floor(camY / coarsestStep + 0.5) * coarsestStep;

	// ---- World offset for this cell within the level's R x R grid ----
	float localI = float(i) - float(R - 1u) * 0.5;
	float localJ = float(j) - float(R - 1u) * 0.5;
	vec2 localPos = vec2(centerX + localI * stepL, centerY + localJ * stepL);

	if (!infinite) {
		localPos = clamp(localPos, vec2(-halfW, -halfH), vec2(halfW, halfH));
	}

	// ---- Output point attributes ----
	vec3 worldP = planeCenter + rightN * localPos.x + upN * localPos.y;
	vec2 uv = (vec2(halfW, halfH) + localPos) / vec2(max(width, 1e-6), max(height, 1e-6));

	oTDPoint_P[id]   = worldP;
	oTDPoint_N[id]   = planeNormal;
	oTDPoint_Tex[id] = vec3(uv, 0.0);

	// ---- Output quad topology ----
	if (i < R - 1u && j < R - 1u) {
		const uint quadsPerLevel = (R - 1u) * (R - 1u);
		uint quadId = level * quadsPerLevel + j * (R - 1u) + i;
		uint vertStart = quadId * 4u;

		// Point indices are GLOBAL (id = level*cellsPerLevel + local), so the
		// level's own point offset must be added - local-only indices would
		// silently reference level 0's points for every level > 0.
		uint base = level * cellsPerLevel;

		// Inner half of the grid is already covered by the next finer level
		// (level-1) at 2x the detail - skip it here by writing a degenerate
		// (zero-area) quad instead of compacting. Tested in CONTINUOUS local
		// space (localI/localJ, symmetric around 0 by construction). No offset
		// is needed here: every level shares the SAME center (see the
		// single shared-snap above), so localI*stepL is directly comparable
		// to the finer level's own half-extent around that same center.
		bool inHole = false;
		if (level > 0u) {
			float fineHalfExtent = float(R - 1u) * 0.25 * stepL; // (R-1)/2 * step(level-1)
			inHole = (abs(localI * stepL) < fineHalfExtent) && (abs(localJ * stepL) < fineHalfExtent);
		}

		if (inHole) {
			uint v0 = base + j * R + i;
			I[vertStart + 0u] = v0;
			I[vertStart + 1u] = v0;
			I[vertStart + 2u] = v0;
			I[vertStart + 3u] = v0;
		} else {
			uint v0 = base + j * R + i;
			uint v1 = base + j * R + (i + 1u);
			uint v2 = base + (j + 1u) * R + (i + 1u);
			uint v3 = base + (j + 1u) * R + i;
			I[vertStart + 0u] = v0;
			I[vertStart + 1u] = v1;
			I[vertStart + 2u] = v2;
			I[vertStart + 3u] = v3;
		}
	}
}
