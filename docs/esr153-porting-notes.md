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
