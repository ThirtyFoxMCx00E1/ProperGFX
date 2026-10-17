// Proper GFX Mobile - Dear ImGui overlay (floating draggable button + settings window)
#pragma once
#include "ps_settings.h"
#include "ps_fx.h"
#include "ps_shadow.h"

struct PSUIStatus
{
    const char* dir = "";               // files/ProperGFX (absolute)
    const char* fxStatus = "";
    const char* gameInfo = "";
    int  patchedShaders = 0;
    int  shadowReceivers = 0;
    const struct PSBuildStats* build = nullptr;
    bool sunTracked = false;
    bool lutLoaded = false;
    const PSFxStats* fx = nullptr;
    const PSShadowStats* shadow = nullptr;
    unsigned int shadowTex = 0;
};

void UI_SetIniPath(const char* path);
bool UI_Init();                                  // GL thread, context current
void UI_Shutdown();
void UI_ContextLost();
bool UI_OnTouch(int type, int finger, int x, int y);   // any thread; true = event belongs to the UI (swallow it)
void UI_Render(PSSettings& s, int w, int h, float dt, const PSUIStatus& st, bool gameplay = true);   // GL thread; gameplay = false hides everything (main / pause menu)
bool UI_MenuOpen();
void UI_RequestOpen();                            // any thread (SAUtils Settings > Mods button): opens the menu on the next frame
