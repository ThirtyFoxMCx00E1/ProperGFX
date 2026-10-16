// [SimulateHDR] tone mappers. TM_MODE is set by the mod: 1 = ACES, 2 = Timothy (Lottes), 3 = Reinhard (extended)
#if TM_MODE == 1
vec3 tonemap(vec3 x){ x *= 0.7232; return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }
#elif TM_MODE == 2
vec3 tonemap(vec3 x){ x = max(x*0.7203, vec3(0.0001)); return pow(x, vec3(1.6)) / (pow(x, vec3(1.5632))*1.07304 + 0.16742); }
#elif TM_MODE == 3
vec3 tonemap(vec3 x){ x *= 1.1998; return x*(1.0 + x/16.0)/(1.0 + x); }
#endif
