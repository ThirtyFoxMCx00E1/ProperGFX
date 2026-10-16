// Host test of the touch plumbing: tap-to-open, drag-to-move, pass-through to the game, multi-finger
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <math.h>
#include "../src64/ps_fx.h"
#include "../src64/ps_glstate.h"
#include "../src64/ps_ui.h"
static int fails = 0;
bool UI_TestButtonRect(float out[4]);
void UI_TestOpen(bool o, int tab);
#include <unistd.h>
#define CHECK(c, msg) do { bool ok_ = (c); printf("%s  %s\n", ok_ ? "PASS" : "FAIL", msg); if (!ok_) ++fails; } while (0)
int main()
{
    const int W = 1600, H = 720;
    auto gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr); EGLint a, b; eglInitialize(d, &a, &b); eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE }; EGLConfig c; EGLint n; eglChooseConfig(d, ca, &c, 1, &n);
    EGLint pa[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE }; EGLSurface s = eglCreatePbufferSurface(d, c, pa);
    EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE }; EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa); eglMakeCurrent(d, s, s, x);
    PSGLState::InitCaps();
    PSSettings st; PSUIStatus us; UI_SetIniPath("");
    auto frame = [&](int n) { for (int i = 0; i < n; ++i) { PSGLState gs; gs.Save(); UI_Render(st, W, H, 0.016f, us); gs.Restore(); } };
    auto bx = [&]() { return (int)(st.buttonX * W); }; auto by = [&]() { return (int)(st.buttonY * H); };

    frame(3);
    CHECK(!UI_MenuOpen(), "menu starts closed");
    CHECK(!UI_OnTouch(2, 0, 100, 100), "touch away from the UI is passed to the game (not swallowed)");
    UI_OnTouch(1, 0, 100, 100);   // balance (not captured -> ignored)

    // 1) tap the floating button
    CHECK(UI_OnTouch(2, 0, bx(), by()), "push on button is swallowed");
    frame(2);
    CHECK(UI_OnTouch(3, 0, bx() + 2, by() + 1), "tiny move while pressing is swallowed");
    frame(1);
    CHECK(UI_OnTouch(1, 0, bx(), by()), "release is swallowed");
    frame(3);
    CHECK(UI_MenuOpen(), "tap opens the settings menu");

    // 2) while the menu is open, touches outside it still reach the game
    CHECK(!UI_OnTouch(2, 1, W - 50, 200), "touch outside the menu window reaches the game");
    UI_OnTouch(1, 1, W - 50, 200);
    // close with the X/close path: tap on the button again would be covered only if overlapping - use settings flag instead
    st.showButton = true;

    // 3) close the menu by tapping the button (menu window must not cover it)
    CHECK(UI_OnTouch(2, 0, bx(), by()), "push on button (menu open) swallowed");
    frame(2);
    UI_OnTouch(1, 0, bx(), by());
    frame(3);
    CHECK(!UI_MenuOpen(), "tapping the button again closes the menu");

    // 4) drag the button
    float x0 = st.buttonX, y0 = st.buttonY;
    int sx = bx(), sy = by();
    CHECK(UI_OnTouch(2, 0, sx, sy), "push on button for drag swallowed");
    frame(2);
    for (int i = 1; i <= 8; ++i) { UI_OnTouch(3, 0, sx - i * 60, sy - i * 50); frame(1); }
    UI_OnTouch(1, 0, sx - 8 * 60, sy - 8 * 50);
    frame(3);
    printf("      button moved from (%.3f,%.3f) to (%.3f,%.3f)\n", x0, y0, st.buttonX, st.buttonY);
    CHECK(fabsf(st.buttonX - x0) > 0.1f && st.buttonY < y0 - 0.1f, "dragging moves the button");
    CHECK(!UI_MenuOpen(), "a drag does NOT open the menu");

    // 5) second finger on the UI while the first holds it: swallowed but ignored; game finger unaffected
    int nx = bx(), ny = by();
    CHECK(UI_OnTouch(2, 0, nx, ny), "finger 0 pressed on the new button position");
    CHECK(UI_OnTouch(2, 1, nx + 5, ny), "second finger on UI swallowed");
    CHECK(!UI_OnTouch(2, 2, 40, 40), "third finger elsewhere goes to the game");
    UI_OnTouch(1, 2, 40, 40); UI_OnTouch(1, 1, nx + 5, ny); UI_OnTouch(1, 0, nx, ny);
    frame(3);
    CHECK(!UI_OnTouch(3, 0, 10, 10), "after release the UI no longer owns finger 0");

    // ---------- regression tests for "I cannot reopen the Cutscene Controller settings" ----------
    auto frameDt = [&](int n, float dt) { for (int i = 0; i < n; ++i) { PSGLState gs; gs.Save(); UI_Render(st, W, H, dt, us); gs.Restore(); } };
    // (a) a finger whose RELEASE was lost must not keep swallowing that finger id forever
    UI_TestOpen(false, 0); frame(4);                                              // start from a closed menu
    CHECK(!UI_MenuOpen(), "menu closed for the regression tests");
    int px0 = bx(), py0 = by();
    UI_OnTouch(2, 5, px0, py0); frame(2);                                        // pressed on the pill ... and the release never arrives
    CHECK(!UI_OnTouch(2, 5, 60, 60), "new push on a stuck finger id goes to the game (other mods' buttons work again)");
    CHECK(!UI_OnTouch(3, 5, 90, 70), "its MOVE reaches the game");
    CHECK(!UI_OnTouch(1, 5, 90, 70), "its RELEASE reaches the game");
    frame(3);
    // (b) watchdog: captured finger silent for >2.5 s is released without toggling the menu
    bool menuBefore = UI_MenuOpen();
    UI_OnTouch(2, 6, px0, py0); frame(2);
    usleep(2700 * 1000);
    frame(2);
    CHECK(!UI_OnTouch(3, 6, 20, 20), "watchdog drops a capture that lost its release");
    CHECK(UI_MenuOpen() == menuBefore, "the watchdog release does not count as a tap");
    // (c) the touch area is the drawn button, nothing more (+3 px)
    frameDt(2, 0.016f);
    float r[4]; UI_TestButtonRect(r);
    float pillW = r[2] - r[0];
    CHECK(!UI_OnTouch(2, 7, (int)r[0] - 6, (int)((r[1] + r[3]) * 0.5f)), "a touch 6 px outside the button is not swallowed");
    UI_OnTouch(1, 7, 0, 0);
    // (d) idle -> the pill shrinks to a dot, so it cannot cover another mod's label
    frameDt(12, 0.5f);                                                            // 6 s idle
    UI_TestButtonRect(r); float dotW = r[2] - r[0];
    printf("      pill width %.0f px -> collapsed dot %.0f px\n", pillW, dotW);
    CHECK(dotW < pillW * 0.4f, "after a few seconds idle the button collapses to a small dot");
    CHECK(!UI_OnTouch(2, 8, (int)(r[0] - 40), (int)((r[1] + r[3]) * 0.5f)), "the area the pill used to cover is free for other mods");
    UI_OnTouch(1, 8, 0, 0);
    bool wasOpen = UI_MenuOpen();
    CHECK(UI_OnTouch(2, 9, (int)((r[0] + r[2]) * 0.5f), (int)((r[1] + r[3]) * 0.5f)), "the dot still takes the touch");
    frame(2); UI_OnTouch(1, 9, (int)((r[0] + r[2]) * 0.5f), (int)((r[1] + r[3]) * 0.5f)); frame(3);
    CHECK(UI_MenuOpen() != wasOpen, "tapping the dot opens/closes the menu in one tap");
    printf(fails ? "RESULT: %d FAILED\n" : "RESULT: all touch tests passed\n", fails);
    return fails;
}
