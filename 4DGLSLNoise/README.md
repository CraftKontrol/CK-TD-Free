# 4D GLSL Noise
## Version 1.004
### Author: Arnaud Cassone © Artcraft Visuals

This is a GLSL implementation of 4D procedural noises. It includes three versions of the shader : TOP, MAT, POP. The TOP version is a pixel shader that generates a noise texture. The MAT version is a material shader that can be used to create 3D noise effects. The POP version is a compute shader that can be used to generate noise values for point clouds or other data.

## Noise Types

Filled with 16 different noise types:

| # | Type | Description |
|---|------|-------------|
| 0 | Value FBM | 4D hypercube (16-corner) value interp, smoothstep |
| 1 | Gradient FBM | Perlin 4D quintic ease, 16-corner 4D gradients |
| 2 | Simplex FBM | Ashima/McEwan true 4D simplex (5 vertices) |
| 3 | Cellular FBM | Worley F1 Euclidean; W shifts all seed positions |
| 4 | Voronoi FBM | Worley F2-F1 Euclidean border (cracked glass) |
| 5 | Ridged Value | 1-\|2n-1\|^ridge fold on Value; sharp ridges |
| 6 | Turbulence | abs(2n-1) on Value; cloud/fire texture |
| 7 | Domain Warp | IQ double-FBM 4D warp; organic swirling |
| 8 | Billow Gradient | 1-\|2n-1\| on Gradient; puffy billows |
| 9 | Ridged Gradient | Ridge fold on Gradient FBM; crisp ridges |
| 10 | Cell Turbulence | abs-fold on Worley F1; soft cellular |
| 11 | Cell Ridged | Ridge fold on Worley F1; crystalline spikes |
| 12 | Pyramids | fbm(Manhattan F1); square/diamond grid |
| 13 | Blocks | fbm(Chebyshev F1); hard square block tiling |
| 14 | Tech | fbm(Manhattan F2-F1); PCB circuit lines |
| 15 | Chebyshev F2-F1 | fbm(Chebyshev F2-F1); boxy grid boundaries |
