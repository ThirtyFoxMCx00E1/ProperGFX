#include "../src/ps_settings.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static int fails = 0;
#define CHECK(c, msg) do { bool ok_ = (c); printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) ++fails; } while (0)
int main()
{
    PSSettings a; PS_ApplyQuality(a, 2); PS_ApplyLook(a, 1);
    a.exposure = DN(0.1f, -0.4f); a.bloomIntensity = DN(0.2f, 0.9f); a.tonemap = TM_TIMOTHY; a.timeOfDay = 2; a.cel = true; a.buttonX = 0.123f;
    CHECK(PS_SaveSettings(a, "/tmp/ps_test.ini"), "save ini");
    PSSettings b;
    CHECK(PS_LoadSettings(b, "/tmp/ps_test.ini"), "load ini");
    CHECK(fabsf(b.exposure.day - 0.1f) < 1e-3 && fabsf(b.exposure.night + 0.4f) < 1e-3, "day/night pair round-trips");
    CHECK(fabsf(b.bloomIntensity.night - 0.9f) < 1e-3, "second day/night pair round-trips");
    CHECK(b.tonemap == TM_TIMOTHY && b.timeOfDay == 2 && b.cel && b.quality == Q_HIGH && b.look == 1, "enums/bools round-trip");
    CHECK(fabsf(b.buttonX - 0.123f) < 1e-3, "button position round-trips");
    FILE* f = fopen("/tmp/ps_test.ini", "r"); char buf[4096]; size_t n = fread(buf, 1, sizeof(buf) - 1, f); buf[n] = 0; fclose(f);
    CHECK(strstr(buf, "[SimulateHDR]") && strstr(buf, "Tonemapper = Timothy") && strstr(buf, "Exposure.Night = -0.4000"), "file uses per-feature sections, named enums and .Day/.Night keys");
    CHECK(strstr(buf, "[Bloom]") && strstr(buf, "[AutoExposure]") && strstr(buf, "[HorizonFog]"), "feature sections present");
    // user edits: single value for a DN key sets both; unknown keys ignored; case-insensitive; inline comments lines ignored
    FILE* g = fopen("/tmp/ps_user.ini", "w");
    fputs("; my edit\n[simulatehdr]\nenabled = 0\nexposure = 0.5\n[Bloom]\nIntensity.Night = 0.8\nBogusKey = 3\n[Nothing]\nX=1\n", g); fclose(g);
    PSSettings c; PS_LoadSettings(c, "/tmp/ps_user.ini");
    CHECK(!c.hdr && fabsf(c.exposure.day - 0.5f) < 1e-3 && fabsf(c.exposure.night - 0.5f) < 1e-3, "plain value applies to day and night; keys case-insensitive");
    CHECK(fabsf(c.bloomIntensity.night - 0.8f) < 1e-3, "only Night overridden");
    // old flat format from the previous build must not be mistaken for the new one
    g = fopen("/tmp/ps_old.ini", "w"); fputs("[ProperShader]\nenabled=1\ntonemap=1\n", g); fclose(g);
    PSSettings d; CHECK(!PS_LoadSettings(d, "/tmp/ps_old.ini"), "old flat file is ignored (defaults used)");
    // garbage / out of range values are clamped
    g = fopen("/tmp/ps_bad.ini", "w"); fputs("[Bloom]\nIntensity = 99\n[ColorGrading]\nGrade = 77\n[General]\nQuality = 9\n", g); fclose(g);
    PSSettings e; PS_LoadSettings(e, "/tmp/ps_bad.ini");
    CHECK(e.bloomIntensity.day <= 1.5f && e.gradePreset >= 0 && e.gradePreset < PS_GradeCount && e.quality <= 3, "out-of-range values are sanitised");
    // time of day blending
    CHECK(PS_NightFactor(13, 0) == 0.f && PS_NightFactor(23, 0) == 1.f && PS_NightFactor(2, 0) == 1.f && PS_NightFactor(12, 0) == 0.f, "day / night extremes");
    float dusk = PS_NightFactor(19, 30), dawn = PS_NightFactor(5, 30);
    CHECK(dusk > 0.3f && dusk < 0.8f && dawn > 0.3f && dawn < 0.8f, "dusk and dawn blend smoothly");
    PSSettings t; t.exposure = DN(0.f, 1.f); PS_UpdateLive(t, 0.25f); CHECK(fabsf(t.exposure.v - 0.25f) < 1e-4, "live value = day + (night-day)*factor");
    t.timeOfDay = 2; PS_UpdateLive(t, 0.f); CHECK(fabsf(t.exposure.v - 1.f) < 1e-4, "forced Night overrides the clock");
    // presets
    PSSettings q; PS_ApplyQuality(q, Q_LOW); CHECK(!q.bloom && !q.sunRays && !q.cas && q.fxaa, "Low preset");
    PS_ApplyQuality(q, Q_HIGH); CHECK(q.bloom && q.sunRays && q.lensFlare && q.cas && q.autoExp, "High preset");
    {   PSSettings q; PS_ApplyQuality(q, 0); bool low = !q.shadows && !q.bloom && !q.sunRays;
        PS_ApplyQuality(q, 1); bool med = q.shadows && q.bloom && q.sunRays;
        PS_ApplyQuality(q, 2); bool high = q.shadows && q.bloom && q.sunRays && q.lensFlare;
        printf("%s  tier shadows/bloom/sunrays: Low off, Medium+High on\n", (low && med && high) ? "PASS" : "FAIL"); if (!(low && med && high)) ++fails; }
    printf(fails ? "RESULT: %d FAILED\n" : "RESULT: all settings tests passed\n", fails);
        return fails;
}
