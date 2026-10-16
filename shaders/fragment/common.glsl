// Proper GFX Mobile 1.4 (64-bit) - shared header of every post-process fragment shader.
// All .glsl files of shaders/ are compiled INTO libProperGFX64.so by build.sh (tools/glsl2h.py) - edit them here and rebuild.
// A shader that does not compile on a GPU is reported in the menu + propergfx_log.txt and that effect is skipped (the game keeps running).
#ifdef GL_FRAGMENT_PRECISION_HIGH
#define HP highp
#else
#define HP mediump
#endif
precision mediump float;
varying HP vec2 vUV;

// ---- SAShaderL-compatible uniforms (same names and meaning as RusJJ's SA Shader Loader) ----
uniform int ShaderFlags;        // A flags for the shaders! Post-process passes use 0x20000000 (FLAG_CUSTOM_POSTPROCESS) + the pass id
uniform vec3 SunVector;         // A XYZ vector of the sun! (CTimeCycle::m_VectorToSun, points toward the sun, z up)
uniform int Time;               // Just a time (in milliseconds)
uniform int GameTimeSeconds;    // A clock time of the game in seconds! 01:02 AM is 62 seconds!
uniform float UnderWaterness;   // If the camera is underwater, this value is NOT 0! (0.0 - 1.0)
uniform float RoadsWetness;     // Shows how much wet roads are (0.0 - 1.0)
uniform float FarClipDist;      // Almost all the time it's 800.0, but not with the mods!
uniform int EntityModel;        // Model index of the last entity being drawn (-1 if no model)

// ---- Proper GFX extras ----
uniform HP float PGFX_TimeSec;  // seconds, wraps every hour - use this for animation (an int millisecond Time overflows mediump)
uniform vec4 PGFX_Sun;          // sun on screen: x, y (0..1), visibility (0..1), elevation (= SunVector.z)
uniform vec4 PGFX_User0;        // menu > GLSL SHADERS > Custom parameters 1-4  ([GLSL] Param0..Param3)
uniform vec4 PGFX_User1;        // menu > GLSL SHADERS > Custom parameters 5-8  ([GLSL] Param4..Param7)

float lum3(vec3 c){ return dot(c, vec3(0.299, 0.587, 0.114)); }
