#include "ps_glstate.h"
#include <EGL/egl.h>
#include <string.h>

bool PSGLState::s_vaoSupported = false;
PS_BindVAOFn PSGLState::s_bindVao = nullptr;

void PSGLState::InitCaps()
{
    const char* ver = (const char*)glGetString(GL_VERSION);
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    bool es3 = ver && strstr(ver, "OpenGL ES 3");
    bool oes = ext && strstr(ext, "GL_OES_vertex_array_object");
    s_bindVao = nullptr;
    if (oes) s_bindVao = (PS_BindVAOFn)eglGetProcAddress("glBindVertexArrayOES");
    if (!s_bindVao && es3) s_bindVao = (PS_BindVAOFn)eglGetProcAddress("glBindVertexArray");
    s_vaoSupported = (oes || es3) && s_bindVao != nullptr;
}

void PSGLState::Save()
{
    while (glGetError() != GL_NO_ERROR) {}
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuf);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elemBuf);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &rbo);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
    for (int i = 0; i < NUNITS; ++i)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex[i]);
    }
    glActiveTexture(activeTex);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    blend = glIsEnabled(GL_BLEND); cull = glIsEnabled(GL_CULL_FACE); depth = glIsEnabled(GL_DEPTH_TEST);
    scissorTest = glIsEnabled(GL_SCISSOR_TEST); stencil = glIsEnabled(GL_STENCIL_TEST);
    dither = glIsEnabled(GL_DITHER); polyOff = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    sampleCov = glIsEnabled(GL_SAMPLE_COVERAGE); sampleAlpha = glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glGetIntegerv(GL_BLEND_SRC_RGB, &bSrcRGB); glGetIntegerv(GL_BLEND_DST_RGB, &bDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &bSrcA); glGetIntegerv(GL_BLEND_DST_ALPHA, &bDstA);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &bEqRGB); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &bEqA);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlign);
    vao = 0;
    if (s_vaoSupported) { glGetIntegerv(0x85B5 /*GL_VERTEX_ARRAY_BINDING(_OES)*/, &vao); if (vao && s_bindVao) s_bindVao(0); }
    for (int i = 0; i < NATTR; ++i)
    {
        at[i].ptr = nullptr;
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &at[i].enabled);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &at[i].size);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &at[i].type);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &at[i].norm);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &at[i].stride);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &at[i].buf);
        glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &at[i].ptr);
    }
    while (glGetError() != GL_NO_ERROR) {}   // state queries must never leave an error for the game to find
}

static void SetEnable(GLenum cap, GLboolean on) { if (on) glEnable(cap); else glDisable(cap); }

void PSGLState::Restore()
{
    while (glGetError() != GL_NO_ERROR) {}
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    for (int i = 0; i < NUNITS; ++i) { glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, tex[i]); }
    glActiveTexture(activeTex);
    glUseProgram(prog);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    SetEnable(GL_BLEND, blend); SetEnable(GL_CULL_FACE, cull); SetEnable(GL_DEPTH_TEST, depth);
    SetEnable(GL_SCISSOR_TEST, scissorTest); SetEnable(GL_STENCIL_TEST, stencil); SetEnable(GL_DITHER, dither);
    SetEnable(GL_POLYGON_OFFSET_FILL, polyOff); SetEnable(GL_SAMPLE_COVERAGE, sampleCov);
    SetEnable(GL_SAMPLE_ALPHA_TO_COVERAGE, sampleAlpha);
    glBlendFuncSeparate(bSrcRGB, bDstRGB, bSrcA, bDstA);
    glBlendEquationSeparate(bEqRGB, bEqA);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glDepthMask(depthMask);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlign);
    for (int i = 0; i < NATTR; ++i)
    {
        glBindBuffer(GL_ARRAY_BUFFER, at[i].buf);
        glVertexAttribPointer(i, at[i].size ? at[i].size : 4, at[i].type ? at[i].type : GL_FLOAT,
                              (GLboolean)at[i].norm, at[i].stride, at[i].ptr);
        if (at[i].enabled) glEnableVertexAttribArray(i); else glDisableVertexAttribArray(i);
    }
    glBindBuffer(GL_ARRAY_BUFFER, arrayBuf);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elemBuf);
    if (vao && s_bindVao) s_bindVao((GLuint)vao);
    while (glGetError() != GL_NO_ERROR) {}
}
