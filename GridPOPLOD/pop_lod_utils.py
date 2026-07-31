"""
pop_lod_utils

Helper functions for the fixed-lattice geometry-clipmap GLSL Advanced POP
(GridLOD / Tesselation_compute.glsl).

Load this file into a Text DAT (e.g. named 'pop_lod_utils') in the same
network as GridLOD, using the DAT's File parameter, or paste its contents.

GLSL Advanced POP does not support custom parameters directly, so the
Tessellation custom parameters live on the *parent COMP* of GridLOD
instead. Every function below takes that COMP as its argument - always
pass `parent()` from an expression written on GridLOD itself (parent() =
the COMP containing GridLOD, evaluated relative to GridLOD). Do NOT use
`me` (that refers to GridLOD, which has no custom pars) and do NOT
hardcode `op('GridLOD')` (fragile if renamed, and wrong COMP anyway).

Referenced from parameter expressions on GridLOD (Vectors page and Output
page), e.g.:
    op('pop_lod_utils').module.numPoints(parent())
    op('pop_lod_utils').module.numQuads(parent())

Expects these custom parameters to exist on the parent COMP,
custom page "Tessellation":
    Camx, Camy, Camz   (XYZ)    - camera world position
    Mintess            (Float)  - level 0 (finest) point spacing, meters
    Maxtess            (Float)  - caps how many times spacing doubles
    Fardist            (Float)  - desired coverage radius, meters (best
                                   effort - see levelCount() docstring)
    Infiniteplane      (Toggle) - 0 = clamp to quad bounds, 1 = infinite
    Levelres           (Int)    - R, fixed points per side, per level
    Maxlevels          (Int)    - safety cap on number of levels (perf)
"""

import math


def snappedR(rawR):
	"""Snap a raw Levelres value to the nearest valid R = 4k+1 (5, 9, 13,
	17, ...). MUST match the identical snap done in Tesselation_compute.glsl
	(rawR -> kLevels -> R) exactly, or Python's dispatch-size counts
	(numPoints/numQuads) will disagree with what the shader actually
	 produces internally, breaking the Output page counts again.

	Required because the hole's half-width, in the COARSE level's own
	cells, is k = (R-1)/4 - this must be a WHOLE number of coarse cells
	(a quad can only be wholly degenerate or wholly real), or the hole's
	true boundary falls inside a coarse cell and produces a visible shift
	between adjacent LOD levels that no hole-test formula can avoid.
	"""
	rawR = max(int(rawR), 2)
	# floor(x + 0.5), matching Tesselation_compute.glsl's rounding exactly -
	# Python's round() uses round-half-to-even, which disagrees with GLSL's
	# floor(x+0.5) (round-half-up) at values like (rawR-1)/4 == 2.5, and that
	# mismatch would make numPoints/numQuads disagree with the shader's
	# actual R again.
	k = max(math.floor((rawR - 1) / 8.0 + 0.5), 1)
	return int(8 * k + 1)


def levelCount(comp):
	"""Number of clipmap levels (rings), each R x R, step doubling from
	Mintess. Levels are added (doubling step each time) until the outermost
	level's half-extent (R/2 * step) reaches Fardist, OR until spacing would
	exceed Maxtess, whichever comes first - if Maxtess is the limiting
	factor, actual coverage stops short of Fardist (raise Maxtess or
	Maxlevels to extend it). Always clamped to [1, Maxlevels].
	"""
	if comp is None:
		raise ValueError(
			"pop_lod_utils.levelCount(comp): comp is None. The expression "
			"that called numPoints()/numQuads() passed an OP reference that "
			"resolved to None (wrong path, or parent() evaluated somewhere "
			"unexpected). Check the exact expression text on this "
			"parameter: it must be "
			"op('pop_lod_utils').module.numPoints(parent()) with parent() "
			"referring to the COMP that owns the Tessellation custom "
			"parameters (GridPOPLOD)."
		)

	R = snappedR(comp.par.Levelres.eval())
	minTess = max(comp.par.Mintess.eval(), 1e-5)
	maxTess = max(comp.par.Maxtess.eval(), minTess * 1.0001)
	farDist = max(comp.par.Fardist.eval(), minTess)
	maxLevels = max(int(comp.par.Maxlevels.eval()), 1)

	# levels needed (pure doubling from level 0) to reach Fardist
	neededL = math.ceil(math.log2(max(2.0 * farDist / (R * minTess), 1.0)))
	# levels allowed before spacing would exceed Maxtess
	cappedL = math.floor(math.log2(max(maxTess / minTess, 1.0)))

	numLevels = min(neededL, cappedL) + 1
	numLevels = max(1, min(numLevels, maxLevels))
	return numLevels


def numPoints(comp):
	"""Total output points = numLevels * R * R. Use for Max Points / Number
	of Elements / Point Count custom expressions."""
	R = snappedR(comp.par.Levelres.eval())
	return levelCount(comp) * R * R


def numQuads(comp):
	"""Total output quads = numLevels * (R-1) * (R-1) (includes degenerate
	hole quads, kept for a fixed, compaction-free buffer layout). Use for
	Max Quads / Quad Count custom expressions."""
	R = snappedR(comp.par.Levelres.eval())
	return levelCount(comp) * (R - 1) * (R - 1)


def debugInfo(comp):
	"""Diagnostic helper - NOT wired into any parameter expression by default.
	Run this from a Textport / DAT to sanity-check the Python side in
	isolation from the GridLOD Output-page wiring, e.g.:
		op('pop_lod_utils').module.debugInfo(op('GridPOPLOD'))
	numPoints and numQuads must ALWAYS be different values (numPoints is
	always strictly larger, since R*R > (R-1)*(R-1) for any R). If, when
	checking the actual values plugged into GridLOD's Max Quads / Quad
	Count parameters, you see them equal to Max Points / Point Count,
	those fields are not calling numQuads(parent()) - re-check their
	expression text and that their Mode dropdown is set to "Custom".
	"""
	R = max(int(comp.par.Levelres.eval()), 2)
	levels = levelCount(comp)
	pts = numPoints(comp)
	quads = numQuads(comp)
	info = (
		f"Levelres(raw)={R}  Levelres(snapped)={snappedR(R)}  numLevels={levels}\n"
		f"Mintess={comp.par.Mintess.eval()}  Maxtess={comp.par.Maxtess.eval()}  "
		f"Fardist={comp.par.Fardist.eval()}  Maxlevels={comp.par.Maxlevels.eval()}\n"
		f"CamPos=({comp.par.Camx.eval()}, {comp.par.Camy.eval()}, {comp.par.Camz.eval()})\n"
		f"numPoints={pts}  numQuads={quads}  (must differ - numPoints > numQuads)"
	)
	print(info)
	return info

