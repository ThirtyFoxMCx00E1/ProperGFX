# Proper Shaders (PC) → Proper GFX Mobile 1.4 (GTA SA): feature status

> 1.4: the 64-bit build (GTA SA 2.10) runs every effect from `.glsl` shaders (`glsl64/` in the source) compiled into `libProperGFX64.so`; the 32-bit build (GTA SA 2.00) has the same effects built into the `.so` (C++/H only), without the new SunVector / bokeh / custom-pass extras.

Legend: ✅ implemented · 🟡 approximated with a screen-space / shader-patch / single-shadow-map substitute · ❌ engine = cannot do in GTA SA Mobile

| # | Feature | Status | Notes |
|---|---|---|---|
| 1 | Full deferred rendering | ❌ engine = cannot do in GTA SA Mobile | Game is a forward renderer on GLES2; no G-buffer/MRT, cannot be injected by a mod loader. |
| 2 | Per-pixel lighting (hundreds of lights) | ❌ engine = cannot do in GTA SA Mobile | Lighting is per-vertex with max 3 directional lights in the game's own shaders. |
| 3 | Volumetric light beams | ❌ engine = cannot do in GTA SA Mobile | Needs a depth buffer. Not exposed on GLES2 default framebuffer. |
| 4 | Shadermaps (normal/roughness/metallic) | ❌ engine = cannot do in GTA SA Mobile | Needs deferred G-buffer + TXDP streaming. |
| 5 | Dynamic 4 cascaded shadows (PCSS) | 🟡 approximated | ONE sun shadow map (25-160 m, up to 2048 px, 4-tap PCF) rendered from the game's sun vector: buildings, roads, trees (alpha-cut leaves) and vehicles cast; lit AND unlit (prelit) world shaders receive (v1.3). Follows the game clock; strength follows timecyc.dat (Shdw). Low = off, Medium / High = on. No cascades / PCSS. |
| 6 | Soft shadow mask + gaussian blur | 🟡 approximated | PCF soft edges + fade at the map border; no separate blurred shadow mask. |
| 7 | Screen-space GI with AO | ❌ engine = cannot do in GTA SA Mobile | Needs depth + normals. |
| 8 | Skylight (sky occlusion) | ❌ engine = cannot do in GTA SA Mobile | Needs depth/world-space data. |
| 9 | Tricolored ambient lighting | ❌ engine = cannot do in GTA SA Mobile | Ambient is one time-cycle colour inside the game's shaders. |
| 10 | Volumetric clouds | ❌ engine = cannot do in GTA SA Mobile | Sky is textured polygons; no volume pass possible. |
| 11 | Volumetric height fog with godrays | 🟡 approximated | Distance fog curve/density patch + screen-space sun shafts (no height/depth). |
| 12 | Water surface | 🟡 approximated | Water-detail contrast control in the game's water shader. |
| 13 | Water caustics | 🟡 approximated | Screen-space caustic shimmer while underwater. |
| 14 | Underwater | ✅ implemented | Distortion, tint and caustics driven by CWeather::UnderWaterness. |
| 15 | Ambient occlusion (SSAO/HBAO/SSGI) | ❌ engine = cannot do in GTA SA Mobile | Needs depth buffer. |
| 16 | HDR simulation + tonemapping (ACES/Timothy) | ✅ implemented | Pseudo-HDR expansion + ACES / Lottes(Timothy) / Reinhard. |
| 17 | HDR composition / postfx (banding fix) | ✅ implemented | Triangular dithering in the final pass removes dark-gradient banding. |
| 18 | LUT | ✅ implemented | 7 built-in grades + any LUT image (1.4): PNG / BMP (any variant) / TGA (incl. RLE) / JPG, strips 256x16, 1024x32, 4096x64 or square atlases 64x64 / 512x512; file name not case-sensitive (`Lut.bmp`), upside-down LUTs auto-detected, live reload, `[LUT] Amount` (default 1.0). |
| 19 | Self-exposure | ✅ implemented | GPU-only log-average luma, no read-back stalls. |
| 20 | Bloom | ✅ implemented | Soft-knee bright pass + 2x separable gaussian at 1/4 res. |
| 21 | Sun rays | ✅ implemented | Screen-space radial shafts toward the sun (auto-off if camera not found). |
| 22 | SMAA with temporal | 🟡 approximated | FXAA 1.0 instead (no history buffer / depth). |
| 23 | AMD Contrast Adaptive Sharpening | ✅ implemented | FidelityFX CAS formula. |
| 24 | Adaptive Depth of Field | 🟡 approximated | Tilt-shift pseudo-DoF; 64-bit 1.4: `dof.glsl` with TiltShift / CenterFocus modes, bokeh highlights, underwater blur (UnderWaterness). True DoF needs depth. |
| 25 | Simple dithered sky shader | 🟡 approximated | Global dithering removes sky banding; sky shader itself unchanged. |
| 26 | PBR sky shader with sky blend | ❌ engine = cannot do in GTA SA Mobile | Would require replacing the sky pipeline. |
| 27 | New horizon fog | 🟡 approximated | Fog curve + density patched into the game's object shaders. |
| 28 | Improved Z-fighting | 🟡 approximated | Manual near-clip override (RwCameraSetNearClipPlane). |
| 29 | Soft particles | ❌ engine = cannot do in GTA SA Mobile | Needs depth texture. |
| 30 | GPU-instanced grass with physical deform | ❌ engine = cannot do in GTA SA Mobile | Needs engine/geometry changes. |
| 31 | Multithreaded procedural objects/grass | ❌ engine = cannot do in GTA SA Mobile | Engine-level change. |
| 32 | Shaders lit by directional light | 🟡 approximated | Specular/reflection strength multipliers on existing dir-light shaders. |
| 33 | Stochastic procedural texturing | ❌ engine = cannot do in GTA SA Mobile | Needs per-texture flags + shadermaps. |
| 34 | Realtime global reflections | ❌ engine = cannot do in GTA SA Mobile | Game's own vehicle env-map stays; strength adjustable. |
| 35 | World rainfall surface | 🟡 approximated | Wet darkening + sheen from CWeather::WetRoads. |
| 36 | Vehicle shader (PBR, fresnel, rain) | 🟡 approximated | Reflection / specular multipliers + wet look only. |
| 37 | Animated wind shader | ❌ engine = cannot do in GTA SA Mobile | Shaders cannot tell vegetation from other objects. |
| 38 | Animated pickup shine | ❌ engine = cannot do in GTA SA Mobile | Pickups share generic shaders. |
| 39 | First-person anti-clipping | 🟡 approximated | Manual near-clip override (set a small value). |
| 40 | Artistic: VHS | ✅ implemented | Scanlines, tracking noise, chroma bleed. |
| 41 | Artistic: GTA:CW outlines | ✅ implemented | Sobel outline pass on luma. |
| 42 | Artistic: Aurora borealis | ❌ engine = cannot do in GTA SA Mobile | Needs a sky mask. |
| 43 | Unlimited TXDP (TXD parents) in .ide | ❌ engine = cannot do in GTA SA Mobile | Needs IDE/streaming parser hooks (separate mod). |
| 44 | 11k+ textures pre-configured | ❌ engine = cannot do in GTA SA Mobile | PC data pack; not part of a code mod. |
| 45 | Cel / anime shading | ✅ implemented | Screen-space + world-shader light quantisation. |
| 46 | Decals projector (GPU) | ❌ engine = cannot do in GTA SA Mobile | Needs depth reconstruction. |
| 47 | Screen space reflections | ❌ engine = cannot do in GTA SA Mobile | Needs depth + normals. |
| 48 | Lens flare | 🟡 approximated | Screen-space ghosts/halo; game's own coronas remain. |
| 49 | Parallax occlusion mapping | ❌ engine = cannot do in GTA SA Mobile | Needs height maps + tangent space. |
| 50 | Specular anisotropic hair | ❌ engine = cannot do in GTA SA Mobile | Needs tangents / per-material shaders. |
| 51 | Subsurface scattering | ❌ engine = cannot do in GTA SA Mobile | Needs per-material pass. |
| 52 | Improve wind / vegetation shader | ❌ engine = cannot do in GTA SA Mobile | See wind. |
| 53 | Terrain splat maps shader | ❌ engine = cannot do in GTA SA Mobile | Needs new terrain data. |
| 54 | Floating draggable settings button + menu | ✅ implemented | Drag anywhere; tap to open 'Proper Shader Settings'. |

**Totals:** 12 implemented, 15 approximated, 27 cannot be done on GTA SA Mobile (out of 54).
