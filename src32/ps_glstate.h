// Saves / restores every piece of GL state we touch, so the game's own cached
// render-state tracker (RQ) never notices that we drew something in between.
#pragma once
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

typedef void (*PS_BindVAOFn)(GLuint);

struct PSGLState
{
    enum { NUNITS = 5, NATTR = 8 };

    GLint prog = 0, arrayBuf = 0, elemBuf = 0, fbo = 0, rbo = 0, activeTex = 0, vao = 0;
    GLint tex[NUNITS] = {0};
    GLint viewport[4] = {0}, scissor[4] = {0};
    GLboolean blend = 0, cull = 0, depth = 0, scissorTest = 0, stencil = 0, dither = 0, polyOff = 0;
    GLboolean sampleCov = 0, sampleAlpha = 0;
    GLint bSrcRGB = 0, bDstRGB = 0, bSrcA = 0, bDstA = 0, bEqRGB = 0, bEqA = 0;
    GLboolean colorMask[4] = {1, 1, 1, 1}, depthMask = 1;
    GLfloat clearColor[4] = {0};
    GLint unpackAlign = 4;
    struct Attr { GLint enabled, size, type, norm, stride, buf; GLvoid* ptr; } at[NATTR];

    static bool        s_vaoSupported;
    static PS_BindVAOFn s_bindVao;
    static void InitCaps();   // call once with a current context

    void Save();
    void Restore();
};
