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

Keyboard and character events target the focused element. `DesktopWindowHost::Focus` requests platform focus before selecting that element. Disabled, collapsed, or detached targets are invalidated before input dispatch and during the next root update. Attached Controls use this same focus owner. Tab/Shift+Tab traverse eligible controls by TabIndex, with Cycle/Once scopes and weak remembered focus; directional navigation and leaving an island through host focus navigation are pending.

Callbacks may change the tree, remove handlers, or shut down a root. Routes retain their targets during synchronous delivery, and platform callbacks retain only a weak backend plus a local strong reference while executing. Shutdown revokes platform subscriptions and clears the input callback before closing the site and island.

## Layout, scale, and shutdown

The host receives physical client bounds and a rasterization scale, then derives the content root's DIP viewport. It updates both layout scale and the Content bridge's override scale. PointerPoint positions are already DIPs. The renderer applies an explicit visual scale only if the Content site does not apply rasterization scale itself.

The host flushes pending root layout before hit testing. Root updates validate input targets, run layout, and synchronize WUC visuals. Elements record rectangle/text commands through `OnRender`. WUC realizes rounded rectangles; DirectWrite and Direct2D provide text content through WUC drawing surfaces. Content caches include element identity, render version, geometry, RTL, DPI, and device generation. Surface device loss causes one device reset and full realization retry. Images, transforms, effects, and compositor animation APIs are pending.

Host shutdown disconnects platform input delivery, cancels element input state, detaches the content root, and closes Composition resources. Only the thread/message-loop owner shuts down the platform queue. Public UI-affine final release and managed/native cycle tracking are still separate pending work.

## Native desktop consumer

[NativeDesktop](../../samples/NativeDesktop/README.md) displays TextBlock and templated Button controls. It wires client resize and DPI messages to the host and demonstrates input activation and Tab navigation. [Control authoring](control-authoring.md) describes styles, property expressions, resources, templates, and state setters.

The sample references the native projects. [PackagedDesktop](../../samples/PackagedDesktop/README.md) and `eng/packaging` provide a separate source-NuGet consumption path with no repository project references. Public activation and metadata, XAML, C#, and deployment milestone completion remain pending. Compilation and execution must be recorded separately; no existing rendering-smoke result establishes correctness of the new path.

## Text input

`TextInputClient` is an optional interface implemented on the element itself. It supplies UTF-16 text, selection, replacement, composition ranges, root-DIP text geometry, and point-to-text mapping. `TextInputSession` connects the focused client to a desktop TSF `ITextStoreACP`, without duplicating its text or element state. TSF owns IME services; the framework supplies the document lock protocol, notifications, focus association, and geometry conversion to screen pixels.

The host's message loop calls `PreTranslateMessage` before `TranslateMessage`/`DispatchMessage`. A control calls `TextInput().NotifyTextChanged` and `NotifySelectionChanged` for application-originated edits. Changes made in response to TSF are not echoed back. Layout frames and DPI changes update candidate-window geometry. Host shutdown terminates composition, disconnects the text store, removes focus association, and releases its document. A thread-shared manager balances activation/deactivation across hosts.

This is a plain text-store foundation, not a TextBox/RichEdit replacement. Attribute-rich composition rendering, input scopes, reconversion features, screen keyboard integration, and actual Japanese IME validation remain pending.

Builds establish compilation/linking only. Applications have not been launched and tests were not added or run for this change. Runtime feature availability, visual output, input/IME behavior, device recovery, and DPI behavior remain unverified.
