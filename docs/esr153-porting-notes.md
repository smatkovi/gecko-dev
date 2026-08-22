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
