# EmbedLite on Gecko ESR 153 — porting notes

Upstream changes (ESR 140 → 153) that hit the EmbedLite series, with the fix
applied in this tree. Template in each case: the nearest upstream in-tree user.

## Widget layer
- `nsBaseWidget` was removed (Bug 1994157, 2025-10-14) and merged into `nsIWidget`.
  The series patch "Allow custom puppet child allocation" had resurrected the whole
  ESR 140 `widget/nsBaseWidget.h` as a side effect; it is deleted again. The hook it
  wanted, `AllocateChildPuppetWidget(const InitData&)`, is now a virtual method on
  `nsIWidget` with the upstream default (`CreatePuppetWidget(nullptr)`).
- `PuppetWidgetBase`, `nsWindow`, `EmbedLitePuppetWidget` derive from `nsIWidget`
  directly (template: `widget/PuppetWidget.h`): own `mBounds`, `GetBounds()` is pure,
  `NS_IMPL_ISUPPORTS_INHERITED0` (`nsIWidget` already inherits
  `nsSupportsWeakReference`), `Create(nsIWidget*, const LayoutDeviceIntRect&,
  const InitData&)`, `Move(const DesktopPoint&)`, `Resize(const DesktopSize&, bool)`,
  `Resize(const DesktopRect&, bool)`, `nsEventStatus DispatchEvent(WidgetGUIEvent*)`
  (no out-parameter), `nsIWidgetListener::WindowResized(nsIWidget*,
  const LayoutDeviceIntSize&)`, `HandleEvent(aEvent)` without the attached-events flag.
- `WillPaintWindow`/`DidPaintWindow` no longer exist (Bug 337801); painting is driven
  by the compositor. **Runtime check pending:** repaint behaviour without these hooks.
- `nsView`/`view/` were removed (Bug 337801): `frame->GetView()->GetWidget()` →
  `nsIFrame::GetNearestWidget()`.
- `Screen` constructor takes `IsPseudoDisplay`, `IsHDR`, SDR content brightness and
  HDR peak brightness (`80.0f, 80.0f`, as in `ScreenHelperGTK`).

## Clipboard, app info, misc
- `nsBaseClipboard::GetNativeClipboardData` gained `uint64_t aThreshold = 0`.
- `nsIXULRuntime.sessionHistoryInParent` was removed; the `EmbedLiteXulAppInfo`
  implementation is dropped.
- `MOZ_STRINGIFY` needs `mozilla/HelperMacros.h`.
- `KeyCodeConsensus_En_US.h` is now `.inc` (Bug 2010520); EmbedLiteViewChild.cpp
  includes it twice.
- `mozilla::Unused` was removed (Bug 1994397). A header-only compatibility shim
  `mfbt/Unused.h` is re-added in the inner tree; `Unused << already_AddRefed<T>` is
  deliberately `= delete` there (the old friend operator released the reference; a
  compile error is better than a silent leak).

## Message manager
- `MessageManagerCallback` lives in `dom/base/MessageManagerCallback.h`.
- `StructuredCloneData` is refcounted: callbacks take
  `NotNull<StructuredCloneData*>` and return values via
  `nsTArray<NotNull<RefPtr<StructuredCloneData>>>*`; `ReceiveMessage` has six
  arguments and no `ErrorResult`; use `aData->Read(cx, &rval, rv)` instead of
  `JS_ReadStructuredClone`. Template: `InProcessBrowserChildMessageManager.cpp`.

## APZ / events
- `GeckoContentController` has pure-virtual refcounting; subclasses declare
  `NS_INLINE_DECL_THREADSAFE_REFCOUNTING(…, override)` and need a non-public
  destructor.
- `nsIDOMWindowUtils.sendMouseEvent` was removed (Bug 1977774);
  `nsContentUtils::SynthesizeMouseEvent` with the WebIDL dictionaries from
  `WindowBinding.h` (plus `FunctionBinding.h` for `VoidFunction`). Template:
  `nsGlobalWindowInner::SynthesizeMouseEvent`.
- `PresShell::GetViewportCanvasBackground().mColor` replaces `GetCanvasBackground()`.
- `nsWebBrowser::Create` returns `nsresult` with an out-parameter and takes an
  `nsIOpenWindowInfo*` before it.
- `NS_NewTimerWithCallback` takes the name as `const nsACString&` (`"…"_ns`).

## Build environment
- mb2 builds in the target snapshot (`<target>.default`); manual sb2 runs must use
  the snapshot too, otherwise BuildRequires packages are missing.
- sb2 maps `/tmp` to the target; keep scripts and logs under `$HOME`.
- cargo/rustc/bindgen run as native binaries outside the sb2 path mapping; they see
  the container's `/usr/include/nspr4`, which is a symlink to the NSPR headers of the
  target (`~/nspr4-bridge`). Recreate after a container rebuild.
- With ccache active, preprocessor failures (`fatal error: X.h: No such file`) are
  swallowed from the mach log; a silent `Error 1` on one object means: rebuild that
  object by hand in the snapshot without ccache to see the message.

## 2026-08-22 evening — device state and open threads

Working on device: mobile layout (dpr 3), UA rv:153.0, context menu, text
selection, dialogs, forum, YouTube, WebAuthn up to the PIN prompt.
Fixed today: duplicate libxul (clean Qt repos before switching Gecko targets),
C++20 in qtmozembed, companion chain suffixes, nsOpenWindowInfo, profiler
init, Services getters (Ci.* nsIID, no env/locale redefinitions), moz-src
packaging, device scale (PuppetWidgetBase parent chain + recursive
BackingScaleFactorChanged; __PREFS_WRITTEN__ marker after crashed first
start), isDOMEventSynthesized, read-once message bridge (async + blocking),
SearchService ES module port, search-config dumps for mobile/sailfishos,
EmbedLiteAlertsService (embed:alert -> Nemo.Notifications), WebAuthnPromptHelper
registration, view id resolution for iframe focus targets.

Open, in order:
1. WebAuthn PIN: add case "promptPassword" to openEmbedLitePrompt (Prompter.sys.mjs,
   patch 0073): topic embed:auth / authresponse, passwordOnly, args.pass/args.ok.
   QML: check the embed:auth popup supports a password-only layout.
2. Address bar search: dumps are packaged, but RemoteSettings fails on
   chrome IndexedDB (UnknownError getLastModified; storage/permanent/chrome).
   Measure with MOZ_LOG=QuotaManager:5,IndexedDB:5 from startup.
3. Web notifications: retest notif.html with the 18:48 xulrunner; report perm=.
4. Cloudflare (ecosia): JS fingerprint, cookies, WebGL, input, focus, HTTP/3,
   ECH all identical to ESR 140; ESR 140 passes. Next: MITM proxy on the Mind2,
   diff the precursor POST payloads of both browsers.
5. JS drifts: legacyHistory (session history), ContentLinkHandler window,
   contentViewer->docViewer, nsISpeculativeConnect argument.
6. Pointer capabilities + GetMaxTouchPoints (both browsers report a desktop
   pointer profile; ui.primaryPointerCapabilities=1, ui.allPointerCapabilities=1
   belong in embedding.js; GetMaxTouchPoints override in PuppetWidgetBase).
7. Remove the EL-* GFX annotation probes before sharing packages.
8. Check the esr153 branch for a maintenance release and rebase.
Device notes: HW video decoding blocked by hybris linker namespace
(libandroidicu.so not accessible to libmedia.so) — port issue, not Gecko.

### 2026-08-22 late — release candidate state
- WebAuthn/FIDO2 works end to end (USB HID, PIN via EmbedLite auth popup, registration and login on webauthn.io).
  Fixes: webauthn_enable_usbtoken pref, WebAuthnPromptHelper registration + guards (no cancel on informational
  prompts, auto-select first credential), Prompter promptPassword -> embed:auth (password-only), args.pass outside
  the promptvalue guard, Fission answers in EmbedLiteXulAppInfo.
- Address bar search still open: search-config dumps packaged, makeChannel allows file: sources, init handshake
  runs (embedui:search init -> embed:search init with engines:[]), but the browser's loadxml replies never reach
  EmbedLiteSearchEngine. Next: read ~/final7.log on the device (all embedui:search messages are logged now);
  suspect the init reply (defaultEngine: null) being rejected by the Qt side.
- Cloudflare managed challenge (ecosia, -challenge demo): server-side scoring rejects 153 before any click;
  test-key Turnstile passes. JS fingerprint, cookies, WebGL, input, focus, geometry, HTTP/3, ECH identical to 140.
  Next: mitmproxy on the Mind2, diff the precursor POST payloads.
- Icon start: D-Bus activation (-prestart); a stale prestart instance holding the name blocks every start. The
  booster instance (stock booster-browser) is masked; packaging should drop the Wants= drop-in and --type=browser.
- Startup cache must be cleared after every component/libxul swap (fixed MOZ_BUILD_DATE).
- Known drifts left: legacyHistory, ContentLinkHandler window, contentViewer, speculativeConnect, blocking message
  return values (SelectionHandler InternalError), pointer capabilities/maxTouchPoints, EL-* GFX probes, HW video
  (hybris linker namespace, port issue).
- ID Austria login (login.id-austria.gv.at) rejects the password in BOTH ESR140 and ESR153 ports; the password
  field receives the pasted string byte-exact (verified with a test page), so this is server-side evaluation,
  same family as the Cloudflare managed challenge. Add to the MITM comparison.
- viewIdFor now tries the element window before its top-level window (embedlite-components c8…, 23:4x build).

### armv7hl (32-bit) build attempt, 2026-08-23 early
Separate tree at ~/share/esr153-armv7hl (branch fork-esr153-armv7hl in the overlay), SDK target
SailfishOS-5.2.0.15-armv7hl. Hurdles cleared: the copied tree needs an empty obj-build-mer-qt-xr and a real
%prep run (mb2 build -p; it refuses on a dirty work tree, and every failed run dirties build/config.status and
config/milestone.txt in the inner repo — git checkout -- . before each retry). rust-std for
armv7-unknown-linux-gnueabihf had to be installed into ~/rust190root (standalone install.sh from
static.rust-lang.org, same 1.90.0 as the existing aarch64/i686 targets).
Open hurdle: cargo build scripts compile for the host with host-cc but inherit the ARM CFLAGS
(-mthumb, -mfpu=neon are rejected). Setting HOST_CFLAGS/HOST_CXXFLAGS globally is wrong — the target
compiler then gets -m32/-march=i686 (reverted). The fix belongs in the %ifarch %arm32 branch, tripel-scoped
like the existing CFLAGS_i686_unknown_linux_gnu, or by clearing CFLAGS only for the build-script invocation.
The real test (linking a 277 MB libxul in a 32-bit address space) is still ahead of that.

### 2026-08-23 (late): the compositor path opened up
Three findings, in order of how much they matter:
1. gfxVars::SetUseEGL(true) is only ever called from the GTK backend, so on EmbedLite
   RenderCompositorEGL::Create() bailed out on its first line and the offscreen path
   was never entered. Now set in gfxPlatform::Init() under MOZ_EMBEDLITE.
2. gfx.egl.prefer-gles.enabled defaults to false off Android, so Gecko tried desktop GL
   first (eglBindAPI(EGL_OPENGL_API) -> EGL_BAD_ATTRIBUTE on Adreno). Now in embedding.js.
3. SwapChain now tracks how many Acquire() calls ago each SharedSurface was last used
   and RenderCompositorEGL::GetBufferAge() reports it, which is what EGL_BUFFER_AGE_EXT
   would give. Partial present works with it.

Measured on scroll2.html (viewport meta, 20s of scrolling), Xperia 10 V:
  software WR, no picture caching:        med 16.3  p95 17.3  max 199.8  7/400 over 32ms
  EGL, no picture caching, no age:        med 16.6  p95 167.3 max 349.8  39/400
  EGL, no picture caching, with age:      med 16.4  p95 17.3  max 232.9  7/400   <- clean
  EGL, picture caching, with age:         med 16.3  p95 17.3  max 34.2   3/400   <- artefacts
  software WR, picture caching, with age: med 17.1  p95 167.0 max 350.0  63/400

So EGL + buffer age is already as good as software and runs on the GPU. Picture caching
on top is clearly the fastest (max drops to 34ms) but still paints stale tiles, because
RenderCompositorEGL::SetBufferDamageRegion() returns early for mUseEmbedLiteOffscreen —
WebRender's damage rects are thrown away. That is the next patch.

### 24 Aug: the hardware compositor works — and why it took two days
Everything below was already correct on the evening of the 23rd, but none of it
had any effect, because the profile contained
`user_pref("layers.acceleration.disabled", true)` — left over from an earlier
experiment. With it, about:support reports FEATURE_FAILURE_COMP_PREF ->
OPENGL_COMPOSITING unavailable -> WEBRENDER unavailable, and Gecko silently uses
software WebRender. RenderCompositorEGL is then never constructed, so pool size,
buffer age, fences and partial present are all dead code. Check
`Compositing:` in about:support before believing any compositor measurement.

What actually makes it work, in order of discovery:
1. gfxVars::SetUseEGL(true) for MOZ_EMBEDLITE (only the GTK backend set it).
2. gfx.egl.prefer-gles.enabled — Adreno is GLES-only, Gecko tries desktop GL
   first off Android and fails with EGL_BAD_ATTRIBUTE.
3. A pool of two surfaces (mPoolLimit) so there is something to rotate.
4. Buffer age counted in *published frames*, not Acquire() calls, keyed on a
   stable per-surface id (freed surfaces get reallocated at the same address).
5. Partial present enabled — WebRender ignores the age otherwise
   ("only relevant if partial present is active", renderer/mod.rs).
6. RequestFullRender() only when the target surface changed.
7. ProducerRelease() on the *front* buffer in the bridge, so the fence belongs
   to the surface the consumer waits on.

scroll2.html, 20s of scrolling, Xperia 10 V:
  software WebRender:  med 16.3  p95 17.3  max 199.8  2/400 over 32ms
  hardware, all of the above: med 16.3  p95 17.2  max 17.3  0/400

Also worth writing down: the browser draws the web content itself, in
DeclarativeWebContainer::renderCompositedFrame() (sailfish-browser), not through
QuickMozView::updatePaintNode(). Hours went into instrumenting the qtmozembed
scene graph path before noticing it is never called.
