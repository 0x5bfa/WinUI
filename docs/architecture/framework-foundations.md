# Framework foundations

Status: native implementation, not a public WinUI ABI or completed application compatibility milestone. See [Project goals](../project-goals.md) and [Layer overview](layer-overview.md) for the agreed scope.

## Ownership

- `UIElement` remains the single owner of element state, tree participation, and native input subscriptions. The change introduces no Core/DXaml peer objects.
- `ContentRoot` owns one `InputManager` and schedules layout and visual updates on its UI thread.
- `InputManager` owns element routing, per-pointer logical capture, pointer-over transitions, and programmatic logical focus. Focus and capture targets are weak references. A synchronous route temporarily retains its elements across application callbacks.
- `DesktopWindowHost` connects a caller-owned window to a content root and renderer. The caller owns the HWND, Windows App SDK initialization, message loop, and DispatcherQueueController.
- `CompositionRenderer` realizes the element tree into WUC visuals. Its backend owns platform resources and receives platform input facts; it does not own UIElement references or duplicate dependency-property state.

## Platform path

The native backend uses these APIs from the pinned IXP 2.1.9 metadata:

1. An existing `Microsoft.UI.Dispatching.DispatcherQueue` ensures a system DispatcherQueue for WUC.
2. A `Windows.UI.Composition.Compositor` creates the root visual and display resources.
3. `ContentIsland.CreateForSystemVisual` associates that system visual with lifted Content.
4. `DesktopChildSiteBridge.CreateWithDispatcherQueue` connects the island to the caller's WindowId.
5. `InputPointerSource.GetForIsland`, `InputKeyboardSource.GetForIsland`, and `InputFocusController.GetForIsland` provide input delivery and host focus.

No MUC Compositor is constructed by this backend. The dependency on lifted Content, Input, and Dispatching remains. Metadata availability and successful compilation do not establish runtime availability or feature-gate enablement on a deployed Windows App SDK runtime. The backend propagates creation failures rather than silently choosing a different compositor.

## Input contract

`InputEventArgs` is an internal native contract, not a new public WinRT interface. Positions are root-relative DIPs; `UIElement::PointFromRoot` converts them to element-local coordinates for the currently implemented layout and RTL mapping.

Pointer and keyboard events bubble along a retained route. `handled` suppresses ordinary handlers on the remaining route; handlers registered with `handledEventsToo` still receive them. Enter/exit transitions are delivered directly to the affected elements. Removing a subscription prevents its later invocation in an existing subscription snapshot.

Pointer capture requires an active press and a visible, enabled, hit-testable element in the same content root. This is logical element capture within the platform input source, not an independent replacement for platform capture. Release, capture loss, host focus loss, and shutdown clear the corresponding state. Cross-island contact routing cancels the current interaction; gesture continuation across islands is pending.

Keyboard events target the root's programmatically focused element. `DesktopWindowHost::Focus` requests platform focus before selecting that element. Disabled, collapsed, or detached targets are invalidated before input dispatch and during the next root update. This does not implement Control's existing prototype focus field or Tab/directional navigation.

Callbacks may change the tree, remove handlers, or shut down a root. Routes retain their targets during synchronous delivery, and platform callbacks retain only a weak backend plus a local strong reference while executing. Shutdown revokes platform subscriptions and clears the input callback before closing the site and island.

## Layout, scale, and shutdown

The host receives physical client bounds and a rasterization scale, then derives the content root's DIP viewport. It updates both layout scale and the Content bridge's override scale. PointerPoint positions are already DIPs. The renderer applies an explicit visual scale only if the Content site does not apply rasterization scale itself.

The host flushes pending root layout before hit testing. Root updates validate input targets, run layout, and synchronize WUC visuals. The current drawing content remains the existing diagnostic blue leaf rectangle, not a brush, text, or image rendering implementation.

Host shutdown disconnects platform input delivery, cancels element input state, detaches the content root, and closes Composition resources. Only the thread/message-loop owner shuts down the platform queue. Public UI-affine final release and managed/native cycle tracking are still separate pending work.

## Native desktop consumer

[NativeDesktop](../../samples/NativeDesktop/README.md) is a small integration application using FrameworkElement directly. It wires client resize and DPI messages to the host and uses routed input to change the diagnostic rectangle's opacity. It does not require Button or the Controls library.

The sample currently references the runtime project. NuGet-only consumption, public activation and metadata, XAML, C#, and deployment milestone completion remain pending. Compilation and execution must be recorded separately; no existing rendering-smoke result establishes correctness of this new WUC/input path.

Build evidence (2026-10-24): the runtime and NativeDesktop application compile and link in Debug and Release x64 with the pinned toolchain. The application was not launched and tests were not added or run for this change. Runtime feature availability, visual output, input behavior, and DPI behavior remain unverified.
