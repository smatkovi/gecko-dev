/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <unistd.h>
#include "EmbedLog.h"

#include "EmbedLiteCompositorBridgeParent.h"
#include "EmbedLiteApp.h"
#include "EmbedLiteWindow.h"
#include "EmbedLiteWindowParent.h"
#include "mozilla/layers/WebRenderBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/webrender/RenderThread.h"
#include "nsThreadUtils.h"

#include "GLContext.h"                  // for GLContext
#include "GLScreenBuffer.h"             // for GLScreenBuffer
#include "SharedSurfaceEGL.h"           // for SurfaceFactory_EGLImage
#include "SurfaceTypes.h"               // for SurfaceStreamType

using namespace mozilla::layers;
using namespace mozilla::gfx;
using namespace mozilla::gl;

namespace mozilla {
namespace embedlite {

static nsTArray<EmbedLiteCompositorBridgeParent*>& LiveCompositors()
{
  static nsTArray<EmbedLiteCompositorBridgeParent*> sList;
  return sList;
}

mozilla::gl::GLContext*
EmbedLiteCompositorBridgeParent::AnyLiveGLContext(EmbedLiteCompositorBridgeParent* aExcept)
{
  for (EmbedLiteCompositorBridgeParent* c : LiveCompositors()) {
    if (c != aExcept && c->PeekGLContext()) {
      return c->PeekGLContext();
    }
  }
  return nullptr;
}

EmbedLiteCompositorBridgeParent::EmbedLiteCompositorBridgeParent(uint32_t windowId,
                                                                 CompositorManagerParent* aManager,
                                                                 uint32_t aNamespace,
                                                                 CSSToLayoutDeviceScale aScale,
                                                                 const TimeDuration &aVsyncRate,
                                                                 const CompositorOptions &aOptions,
                                                                 bool aRenderToEGLSurface,
                                                                 const gfx::IntSize &aSurfaceSize,
                                                                 uint64_t aInnerWindowId)
  : CompositorBridgeParent(aManager, aNamespace, aScale, aVsyncRate,
                           aOptions, aRenderToEGLSurface, aSurfaceSize,
                           aInnerWindowId)
  , mWindowId(windowId)
  , mCurrentCompositeTask(nullptr)
  , mSurfaceOrigin(0, 0)
  , mRenderMutex("EmbedLiteCompositorBridgeParent render mutex")
  , mPlatformImageMutex("EmbedLiteCompositorBridgeParent platform image mutex")
  , mPlatformImageGeneration(0)
  , mPlatformImageRetryPending(false)
{
  LOGT("EmbedLiteCompositorBridgeParent::EmbedLiteCompositorBridgeParent");
  if (mWindowId == 0) {
    mWindowId = EmbedLiteWindowParent::Current();
  }
  EmbedLiteWindowParent* parentWindow = EmbedLiteWindowParent::From(mWindowId);
  LOGT("this:%p, window:%p, sz[%i,%i]", this, parentWindow, aSurfaceSize.width, aSurfaceSize.height);

  // Verdienstbasierte Uebergabe: Ctor registriert nur, wenn frei - der
  // Present-Refresh uebertraegt das Amt, sobald der Neue ERFOLGREICH
  // published (P4-Screen + EGLImage-Morph machen ihn dazu faehig).
  if (!parentWindow->GetCompositor()) {
    parentWindow->SetCompositor(this);
  }
  LiveCompositors().AppendElement(this);
  parentWindow->GetListener()->CompositorCreated();

  // Post open parent?
  //
}

EmbedLiteCompositorBridgeParent::~EmbedLiteCompositorBridgeParent()
{
  LOGT("EmbedLiteCompositorBridgeParent::~EmbedLiteCompositorBridgeParent this=%p", this);
  LiveCompositors().RemoveElement(this);
  // Registrierung zuruecknehmen - sonst zeigt der WindowParent auf einen
  // toten Compositor und WithPlatformImage liefert ewig fb=0.
  if (EmbedLiteWindowParent* parentWindow = EmbedLiteWindowParent::From(mWindowId)) {
    if (parentWindow->GetCompositor() == this) {
      parentWindow->SetCompositor(nullptr);
    }
  }
}

void
EmbedLiteCompositorBridgeParent::SetWebRenderGLContext(GLContext* aGL)
{
  LOGT("gl:%p", aGL);
  MOZ_ASSERT(wr::RenderThread::IsInRenderThread());
  MutexAutoLock imageLock(mPlatformImageMutex);
  MutexAutoLock lock(mRenderMutex);
  if (mGLContext != aGL) {
    mFrontBuffer.reset();
    ++mPlatformImageGeneration;
    mGLContext = aGL;
    // Amtswechsel: Der RendererOGL vergibt den GL-Kontext genau einmal pro
    // Renderer-Generation - wer ihn frisch bekommt, ist der lebende
    // Compositor. Der Vorgaenger haelt sonst mit eingefrorenem FrontBuffer
    // (fb=1, about:blank) das Amt und die App zeigt ewig Weiss.
    // (Gestriger Rueckbau war Fehlurteil auf Kulissen-Daten der
    // geschlossenen Ladefront.)
    if (aGL) {
      if (EmbedLiteWindowParent* pw = EmbedLiteWindowParent::From(mWindowId)) {
        if (pw->GetCompositor() != this) {
          pw->SetCompositor(this);
        }
      }
    }
  }
}

void
EmbedLiteCompositorBridgeParent::EnsureSurfaceSizeFromWindow()
{
  EmbedLiteWindowParent* parentWindow = EmbedLiteWindowParent::From(mWindowId);
  if (!parentWindow) {
    return;
  }

  const int width = static_cast<int>(parentWindow->mSize.width);
  const int height = static_cast<int>(parentWindow->mSize.height);
  if (width <= 0 || height <= 0) {
    return;
  }

  MutexAutoLock lock(mRenderMutex);
  // 140-Fix Rotation: frueher Einmal-Gate - nach dem Drehen blieb die
  // Surface im Portrait stehen (Stride-Riss). Jetzt nur noch abbrechen,
  // wenn sich die Groesse wirklich nicht geaendert hat.
  if (mEGLSurfaceSize.width == width && mEGLSurfaceSize.height == height) {
    return;
  }
  LOGT("EL-SZ window size %dx%d -> %dx%d", mEGLSurfaceSize.width, mEGLSurfaceSize.height, width, height);
  mSurfaceOrigin.MoveTo(0, 0);
  SetEGLSurfaceRect(0, 0, width, height);
}

bool
EmbedLiteCompositorBridgeParent::CompositeToDefaultTarget(WebRenderBridgeParent* aWrBridge,
                                                          VsyncId aId,
                                                          wr::RenderReasons aReasons)
{
  aWrBridge->CompositeToTarget(aId, aReasons, nullptr, nullptr);
  return true;
}

bool
EmbedLiteCompositorBridgeParent::PresentOffscreenSurface()
{
  LOGT("EmbedLiteCompositorBridgeParent::PresentOffscreenSurface this=%p", this);
  // Publish-Amtssperre: Zwei CBPs publizieren sonst konkurrierend in
  // dieselbe Kette (Ring-Beweis: Erstling-Weiss/Schwarz ueberschreibt die
  // Seite des Amtsinhabers -> Touch-Degradation, Blinken, Standbild).
  if (EmbedLiteWindowParent* pw = EmbedLiteWindowParent::From(mWindowId)) {
    if (pw->GetCompositor() && pw->GetCompositor() != this) {
      LOGT("EL-VETO publish skipped (not incumbent) this=%p", this);
      return true;
    }
  }
  MOZ_ASSERT(wr::RenderThread::IsInRenderThread());
  RefPtr<GLContext> context;
  uint64_t generation;
  {
    MutexAutoLock lock(mRenderMutex);
    context = mGLContext;
    generation = mPlatformImageGeneration;
  }
  // 140-Fix Rotation: Screen wurde nur EINMAL gebaut - nach dem Drehen las
  // die App mit neuer Breite aus einem Portrait-Buffer (Stride-Riss).
  bool screenStale = false;
  if (context && context->Screen()) {
    EnsureSurfaceSizeFromWindow();
    gfx::IntSize cur;
    { MutexAutoLock lock(mRenderMutex); cur = mEGLSurfaceSize; }
    if (!cur.IsEmpty() && context->Screen()->Size() != cur) {
      screenStale = true;
      LOGT("EL-P4 screen stale %dx%d -> %dx%d", context->Screen()->Size().width,
           context->Screen()->Size().height, cur.width, cur.height);
    }
  }
  if (context && (!context->Screen() || screenStale)) {
    // Amtsnachfolger: RendererOGL hat den Kontext verdrahtet, aber der
    // 0072-Screen entsteht nur im Ensure-Pfad des Erstlings. Nachziehen.
    EnsureSurfaceSizeFromWindow();
    gfx::IntSize sz;
    {
      MutexAutoLock lock(mRenderMutex);
      sz = mEGLSurfaceSize;
    }
    if (!sz.IsEmpty() && context->MakeCurrent()) {
      if (context->CreateOffscreenScreenBuffer(sz)) {
        // GLScreenBuffer::Create startet mit SurfaceFactory_Basic (0072);
        // fuer WithPlatformImage braucht der FrontBuffer die EGLImage-Factory.
        if (gl::GLScreenBuffer* screen = context->Screen()) {
          if (UniquePtr<gl::SurfaceFactory> factory =
                  gl::SurfaceFactory_EGLImage::Create(*context)) {
            screen->Morph(std::move(factory));
          }
        }
        LOGT("EL-P4 screen created %dx%d this=%p", sz.width, sz.height, this);
        // Frischer Screen ist leer - WebRender einmal anstossen,
        // damit der Amtsinhaber echten Inhalt rendert statt Leere zu publishen.
        if (nsIThread* ct = CompositorThread()) {
          ct->Dispatch(NewRunnableMethod(
              "EmbedLiteCompositorBridgeParent::FullInvalidateOnCompositorThread",
              this, &EmbedLiteCompositorBridgeParent::FullInvalidateOnCompositorThread));
        }
      }
    }
  }
  if (!context || !context->Screen()) {
    LOGT("EL-P1 no ctx/screen this=%p", this);
    MutexAutoLock lock(mRenderMutex);
    if (context == mGLContext) {
      mFrontBuffer.reset();
    }
    return false;
  }

  GLScreenBuffer* screen = context->Screen();
  MOZ_ASSERT(screen);

  // EL-DUMP: Renderziel-Inhalt VOR Publish als PPM sichern (max 3, env-gated).
  if (getenv("EL_DUMP")) {
    static int sDumpN = 0;
    static int sPresentN = 0;
    ++sPresentN;
    static int sFrom = getenv("EL_DUMP_FROM") ? atoi(getenv("EL_DUMP_FROM")) : 0;
    bool elDumpNow = access("/tmp/dumpnow", F_OK) == 0;
    if (elDumpNow) unlink("/tmp/dumpnow");
    if ((getenv("EL_DUMP_RING") || elDumpNow || (sDumpN < 10 && sPresentN >= sFrom && (sPresentN % 3) == 0)) && !screen->Size().IsEmpty() && context->MakeCurrent()) {
      const int w = screen->Size().width, h = screen->Size().height;
      UniquePtr<uint8_t[]> px(new (fallible) uint8_t[size_t(w) * h * 4]);
      if (px) {
        context->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER, context->GetDefaultFramebuffer());
        context->fReadPixels(0, 0, w, h, LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE, px.get());
        char path[64];
        if (getenv("EL_DUMP_RING")) {
          snprintf(path, sizeof(path), "/tmp/elring_%d_%p.ppm", sPresentN % 8, this);
        } else {
          snprintf(path, sizeof(path), "/tmp/eldump_%d_%p.ppm", sDumpN, this);
        }
        if (FILE* f = fopen(path, "wb")) {
          fprintf(f, "P6\n%d %d\n255\n", w, h);
          for (size_t i = 0; i < size_t(w) * h; ++i) fwrite(px.get() + i * 4, 1, 3, f);
          fclose(f);
          LOGT("EL-DUMP wrote %s", path);
        }
        if (!getenv("EL_DUMP_RING")) ++sDumpN;
      }
    }
  }
  LOGT("EL-ROT pre-publish fbo=%u front=%p gen=%llu", context->GetDefaultFramebuffer(), mFrontBuffer.get(), (unsigned long long)mPlatformImageGeneration);
  if (screen->Size().IsEmpty() || !screen->PublishFrame(screen->Size())) {
    LOGT("EL-P2 publish failed this=%p", this);
    NS_ERROR("Failed to publish context frame");
    MutexAutoLock lock(mRenderMutex);
    if (context == mGLContext && generation == mPlatformImageGeneration) {
      mFrontBuffer.reset();
    }
    return false;
  }

  // SYNC-PROBE (Holzhammer): Render-Fertigstellung erzwingen, bevor der
  // FrontBuffer an den Konsumenten geht - Punkte-Muster = fehlende Fence.
  context->fFinish();
  std::shared_ptr<SharedSurface> frontBuffer = screen->FrontBuffer();
  MutexAutoLock lock(mRenderMutex);
  if (context != mGLContext || generation != mPlatformImageGeneration) {
    LOGT("EL-P3 generation/ctx race this=%p", this);
    return false;
  }
  mFrontBuffer = std::move(frontBuffer);
  // Der publizierende Compositor ist der, den die App fragen soll.
  if (EmbedLiteWindowParent* parentWindow = EmbedLiteWindowParent::From(mWindowId)) {
    // Konservative Uebernahme: nur bei vakantem Amt (Dtor raeumt) - ein
    // erfolgreicher Publish allein beweist keinen Inhalt (Boot-Regression).
    if (!parentWindow->GetCompositor()) {
      parentWindow->SetCompositor(this);
    }
  }
  return !!mFrontBuffer;
}

void
EmbedLiteCompositorBridgeParent::WebRenderComposited()
{
  LOGT("WebRenderComposited");
  if (!PresentOffscreenSurface()) {
    return;
  }

  if (EmbedLiteWindowParent* parentWindow = EmbedLiteWindowParent::From(mWindowId)) {
    parentWindow->GetListener()->CompositingFinished();
  }
}

bool EmbedLiteCompositorBridgeParent::GetScrollableRect(CSSRect&)
{
  LOGT("EmbedLiteCompositorBridgeParent::GetScrollableRect");
  return true;
}

void EmbedLiteCompositorBridgeParent::SetSurfaceRect(int x, int y, int width, int height)
{
  LOGT("EmbedLiteCompositorBridgeParent::SetSurfaceRect");
  MutexAutoLock lock(mRenderMutex);
  if (width > 0 && height > 0 && (mEGLSurfaceSize.width != width ||
                                  mEGLSurfaceSize.height != height ||
                                  mSurfaceOrigin.x != x ||
                                  mSurfaceOrigin.y != y)) {
    mSurfaceOrigin.MoveTo(x, y);
    SetEGLSurfaceRect(x, y, width, height);
  }
}

void
EmbedLiteCompositorBridgeParent::SchedulePlatformImageRetry()
{
  if (!mPlatformImageRetryPending.compareExchange(false, true)) {
    return;
  }

  nsISerialEventTarget* compositorThread = CompositorThread();
  if (!compositorThread) {
    mPlatformImageRetryPending = false;
    return;
  }

  RefPtr<EmbedLiteCompositorBridgeParent> self = this;
  RefPtr<Runnable> retry = NS_NewRunnableFunction(
    "EmbedLiteCompositorBridgeParent::SchedulePlatformImageRetry",
    [self]() {
      self->mPlatformImageRetryPending = false;
      if (EmbedLiteWindowParent* parentWindow =
              EmbedLiteWindowParent::From(self->mWindowId)) {
        parentWindow->GetListener()->CompositingFinished();
      }
    });
  if (NS_FAILED(compositorThread->DelayedDispatch(retry.forget(), 16))) {
    mPlatformImageRetryPending = false;
  }
}

bool
EmbedLiteCompositorBridgeParent::WithPlatformImage(
  const PlatformImageCallback& callback)
{
  LOGT("EmbedLiteCompositorBridgeParent::WithPlatformImage this=%p fb=%d", this, (int)!!mFrontBuffer);
  if (!callback) {
    return false;
  }

  RefPtr<GLContext> context;
  std::shared_ptr<SharedSurface> frontBuffer;
  uint64_t generation;
  {
    MutexAutoLock lock(mRenderMutex);
    context = mGLContext;
    frontBuffer = mFrontBuffer;
    generation = mPlatformImageGeneration;
  }
  if (!context || !frontBuffer) {
    LOGT("EL-W1 no ctx/frontbuffer");
    return false;
  }

  MutexAutoLock imageLock(mPlatformImageMutex);
  {
    MutexAutoLock lock(mRenderMutex);
    if (generation != mPlatformImageGeneration) {
      LOGT("EL-W2 generation race");
      return false;
    }
  }

  SharedSurface* sharedSurf = frontBuffer.get();
  if (sharedSurf->mDesc.type != SharedSurfaceType::EGLImageShare) {
    LOGT("EL-W3 type=%d != EGLImageShare", int(sharedSurf->mDesc.type));
    return false;
  }

  if (!sharedSurf->IsBufferAvailable()) {
    LOGT("EL-W4 buffer busy - retry");
    SchedulePlatformImageRetry();
    return false;
  }

  SharedSurface_EGLImage* eglImageSurf =
    static_cast<SharedSurface_EGLImage*>(sharedSurf);
  PlatformImageTextureTarget textureTarget;
  switch (eglImageSurf->EmbedderTextureTarget()) {
    case LOCAL_GL_TEXTURE_2D:
      textureTarget = PlatformImageTextureTarget::Texture2D;
      break;
    case LOCAL_GL_TEXTURE_EXTERNAL:
      textureTarget = PlatformImageTextureTarget::ExternalOES;
      break;
    default:
      NS_WARNING("Unsupported EmbedLite platform image texture target");
      return false;
  }

  const PlatformImageDescriptor descriptor = {
    PlatformImageHandleType::EGLImage,
    eglImageSurf->mImage,
    textureTarget,
    sharedSurf->mDesc.size.width,
    sharedSurf->mDesc.size.height
  };

  sharedSurf->ProducerReadAcquire();
  callback(descriptor);
  sharedSurf->ProducerReadRelease();
  return true;
}

void
EmbedLiteCompositorBridgeParent::ClearPlatformImage()
{
  LOGT("EmbedLiteCompositorBridgeParent::ClearPlatformImage");
  MutexAutoLock imageLock(mPlatformImageMutex);
  MutexAutoLock lock(mRenderMutex);
  mFrontBuffer.reset();
  ++mPlatformImageGeneration;
}

void
EmbedLiteCompositorBridgeParent::FullInvalidateOnCompositorThread()
{
  // Frischer Screen ist leer; ein normaler Frame malt nur dirty tiles
  // (= Punktemuster). Volles Invalidate erzwingt einen kompletten Render.
  if (mWrBridge) {
    // CONFIG_CHANGE erzwingt den vollen Szenen-/Frame-Neuaufbau - der
    // richtige Hebel nach Renderer-Neubau (vgl. WRBP:1681).
    mWrBridge->ScheduleForcedGenerateFrame(wr::RenderReasons::CONFIG_CHANGE);
  }
}

void
EmbedLiteCompositorBridgeParent::SuspendRendering()
{
  LOGT("EmbedLiteCompositorBridgeParent::SuspendRendering");
  if (nsIThread* thread = CompositorThread()) {
    MOZ_ALWAYS_SUCCEEDS(SyncRunnable::DispatchToThread(
      thread,
      NewRunnableMethod("EmbedLiteCompositorBridgeParent::PauseComposition",
                        this,
                        &EmbedLiteCompositorBridgeParent::PauseComposition)));
  }
}

void
EmbedLiteCompositorBridgeParent::ResumeRendering()
{
  LOGT("EmbedLiteCompositorBridgeParent::ResumeRendering");
  EnsureSurfaceSizeFromWindow();
  int x;
  int y;
  int width;
  int height;
  {
    MutexAutoLock lock(mRenderMutex);
    x = mSurfaceOrigin.x;
    y = mSurfaceOrigin.y;
    width = mEGLSurfaceSize.width;
    height = mEGLSurfaceSize.height;
  }
  LOGT("ResumeRendering size: %dx%d thread:%p", width, height, CompositorThread());
  if (width > 0 && height > 0 && CompositorThread()) {
    bool resumeOk = false;
    MOZ_ALWAYS_SUCCEEDS(SyncRunnable::DispatchToThread(
      CompositorThread(),
      NS_NewRunnableFunction(
        "EmbedLiteCompositorBridgeParent::ResumeCompositionAndResize",
        [self = RefPtr<EmbedLiteCompositorBridgeParent>(this), &resumeOk, x, y, width, height]() {
          resumeOk = self->ResumeCompositionAndResize(x, y, width, height);
        })));
    LOGT("ResumeCompositionAndResize=%d IsPaused=%d", (int)resumeOk, (int)IsPaused());
    CompositorBridgeParent::ScheduleRenderOnCompositorThread(wr::RenderReasons::WIDGET);
  }
  ScheduleForcedRenderOnCompositorThread(wr::RenderReasons::WIDGET);
}

void
EmbedLiteCompositorBridgeParent::ScheduleForcedRenderOnCompositorThread(
    wr::RenderReasons aReasons)
{
  if (CompositorThreadHolder::IsInCompositorThread()) {
    ScheduleForcedRender(aReasons);
    return;
  }

  if (CompositorThread()) {
    CompositorThread()->Dispatch(NewRunnableMethod<wr::RenderReasons>(
      "EmbedLiteCompositorBridgeParent::ScheduleForcedRender",
      this,
      &EmbedLiteCompositorBridgeParent::ScheduleForcedRender,
      aReasons));
  }
}

void
EmbedLiteCompositorBridgeParent::ScheduleForcedRender(wr::RenderReasons aReasons)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  EnsureSurfaceSizeFromWindow();
  LOGT("ScheduleForcedRender paused=%d", (int)IsPaused());
  if (WebRenderBridgeParent* wrBridge = GetWrBridge()) {
    wrBridge->ScheduleForcedGenerateFrame(aReasons);
    wrBridge->CompositeToTarget(VsyncId(), aReasons, nullptr, nullptr);
    wrBridge->FlushRendering(aReasons, /* aBlocking */ true);
    return;
  }

  ScheduleComposition(aReasons);
}

} // namespace embedlite
} // namespace mozilla
