// Portable OpenGL 3.3 core renderer: logical size offscreen target, batched triangles,
// a CPU 16-bit surface overlay for direct pixel writes, gamma, present and frame capture.
// The offscreen target is stored top row first (y down) so edge ties follow D3D's top-left fill rule.
// Optional enhancements (RenderOptions): true colour target, bloom, per pixel lighting, mipmaps, MSAA.
#pragma once

#include "gl_funcs.hpp"
#include "renderer.hpp"
#include <vector>

struct RTexture
{
    GLuint mId;
    s32 mWidth;
    s32 mHeight;
    s32 mLevels; // mip levels uploaded, 1 for the base level only
};

class GLRenderer : public IRenderer
{
  public:
    GLRenderer();
    ~GLRenderer();

    // Loader comes from the platform context, which must be current.
    bool Init(GLFuncs::Loader loader, const char** ppError);
    void Shutdown();

    void SetLogicalSize(s32 width, s32 height);
    // Menus: magnified textures with transparency get smooth edges (SmoothSample in the shaders).
    void SetSmoothMagnify(bool smooth) { mSmoothMagnify = smooth; }
    s32 LogicalWidth() const { return mLogicalW; }
    s32 LogicalHeight() const { return mLogicalH; }
    // The render target: the logical frame at the size it is shown (sharp scaling), or the logical
    // size itself with GTA2GL_NATIVE=0.
    s32 TargetWidth() const { return mTargetW; }
    s32 TargetHeight() const { return mTargetH; }

    // Rectangle in logical pixels, right/bottom exclusive. Empty rect means everything.
    void Clear(u8 r, u8 g, u8 b, s32 left, s32 top, s32 right, s32 bottom);

    // 16-bit RGB565 pixels written by the game; key marks pixels that were not written.
    void CompositeSurface(const u16* pPixels, s32 pitchBytes, s32 width, s32 height, u16 key);

    // Post effects of the finished frame (bloom); call once before capture and Present.
    void FinishFrame();
    // Draws the finished frame into the default framebuffer, letterboxed.
    void Present(s32 drawableW, s32 drawableH);
    // Call after the swap: waits for the previous frame, so at most one frame is queued ahead of the display.
    void ThrottleFrames();
    void SetGamma(f32 r, f32 g, f32 b);

    // RGBA8 rows of the finished frame, top row first.
    void ReadLogical(std::vector<u8>& out);

    // Pixel offset added to every vertex. D3D puts pixel centres on integers, GL on halves. The extra
    // 1/2048 resolves exact ties like the reference without catching the original's "w - 0.001" quad edges.
    void SetVertexOffset(f32 x, f32 y) { mOffsetX = x; mOffsetY = y; }

    // Texture sampling moved by this many pixels towards the top left of the pixel centre.
    void SetSampleShift(f32 pixels) { mSampleShift = pixels; }

    void SetOptions(const RenderOptions& options);
    // The most samples per pixel the render target can have (MSAA).
    s32 MaxSamples() const { return mMaxSamples; }

    // IRenderer
    RTexture* CreateTexture();
    void UploadTexture(RTexture* pTex, s32 width, s32 height, const u32* pRgba, RMipmaps mipmaps);
    void FreeTexture(RTexture* pTex);
    void BeginScene();
    void EndScene();
    void DrawTriangles(const RVertex* pVerts, s32 count, RTexture* pTex, u32 state);
    void SetClipRect(f32 left, f32 top, f32 right, f32 bottom);
    u32 GetDrawCallCount() const { return mDrawCalls; }
    u32 GetUploadCount() const { return mUploads; }
    u32 GetVertexCount() const { return mVertices; }
    const RenderOptions& Options() const { return mOptions; }
    void SetLights(const RLight* pLights, s32 count, f32 ambient);

  private:
    void Flush();
    void BindTarget();
    void ApplyState(u32 state);
    void CreateTargets();
    void CreateMultisampleTarget(GLenum colourFormat);
    void ResolveMultisample();
    void ApplyTextureFilter();
    void DrawFullscreen();
    GLuint CompileProgram(const char* vs, const char* fs, const char** ppError);

    GLFuncs gl;
    bool mReady;
    s32 mLogicalW;
    s32 mLogicalH;
    s32 mTargetW;
    s32 mTargetH;
    s32 mDrawableW;
    s32 mDrawableH;
    void UpdateTargetSize();

    GLuint mFbo;
    GLuint mFboTex;      // scene colour (attachment 0)
    GLuint mBloomSrcTex; // additive contributions (attachment 1, bloom only)
    GLuint mPostFbo;     // scene + bloom
    GLuint mPostTex;
    GLuint mBlurFbo[2];
    GLuint mBlurTex[2];
    s32 mBlurW;
    s32 mBlurH;
    bool mTargetsTrueColour;
    bool mTargetsBloom;
    bool mPostValid;
    GLsync mFrameFence;
    // MSAA: the scene is drawn into these and resolved into mFbo by FinishFrame.
    GLuint mMsFbo;
    GLuint mMsColour[2];
    s32 mTargetsSamples; // 0 without MSAA
    s32 mMaxSamples;

    // Scene programs: 0 faithful, 1 enhanced (bloom output, per pixel lighting).
    struct SceneProgram
    {
        GLuint mId;
        GLint mScale, mOffset, mAlphaTest, mUseTex, mSampleShift, mSmooth, mAdditive, mPerPixel, mNumLights, mAmbient;
    };
    SceneProgram mScene[2];
    int mActiveScene;
    GLuint mProgram; // enhanced scene program
    GLuint mPresentProgram;
    GLint mUniGamma;
    GLint mUniFlipY;
    GLuint mDownProgram;
    GLint mUniDownTexel;
    GLuint mBlurProgram;
    GLint mUniBlurStep;
    GLuint mComposeProgram;
    GLint mUniBloomIntensity;
    GLuint mVao;
    GLuint mVbo;
    GLuint mLightUbo;
    GLuint mSamplerNearest;
    GLuint mSamplerLinear;
    GLuint mSamplerMip;
    f32 mMaxAnisotropy; // 1 without EXT_texture_filter_anisotropic
    std::vector<u32> mMipBuffer;
    GLuint mWhiteTex;
    GLuint mSurfaceTex;
    s32 mSurfaceW;
    s32 mSurfaceH;
    std::vector<u32> mSurfaceRgba;

    std::vector<RVertex> mBatch;
    RTexture* mBatchTex;
    u32 mBatchState;
    u32 mAppliedState;
    bool mStateValid;

    f32 mOffsetX;
    f32 mOffsetY;
    f32 mSampleShift;
    bool mSmoothMagnify = false;
    RenderOptions mOptions;
    s32 mNumLights;
    f32 mAmbient;
    f32 mGamma[3];
    u32 mDrawCalls;
    u32 mUploads;
    u32 mVertices;
};
