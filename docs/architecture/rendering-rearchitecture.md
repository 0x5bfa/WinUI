# Rendering rearchitecture

## Current direction

The agreed platform choice is WUC with lifted input. The native backend now constructs a `Windows.UI.Composition.Compositor`, connects its visual through `ContentIsland.CreateForSystemVisual`, and receives lifted pointer/keyboard input. See [Framework foundations](framework-foundations.md) for ownership, hosting, scale, and remaining work. The investigation and earlier smoke observations below describe their original implementation state; they do not validate the new backend at runtime.

## Upstream investigation (2026-10-24)

Reference checkout: `D:/source/mux-main`, revision `0df0a8869`. This document precedes any new rendering implementation. The legacy implementation uses Windows Composition visuals backed by DirectComposition, with Direct2D/Direct3D for drawing content.

The relevant code paths have distinct jobs:

| Upstream source | Observed responsibility | New boundary |
| --- | --- | --- |
| `dxaml/xcp/core/hw/hwwalk.cpp` | Walks dirty XAML elements and builds rendering commands. | Collect visual changes on the UI thread, after layout. |
| `dxaml/xcp/core/hw/VisualContentRenderer.cpp` | Reuses per-element pre/post child composition primitives; `RenderSolidColorRectangle` creates sprite visuals and brushes. | Realize primitive visuals and reuse composition resources keyed by a live element. |
| `dxaml/xcp/core/hw/BaseContentRenderer.cpp` | Uses the rendering target and composition-tree host. | Encapsulate drawing surfaces and drawing commands. |
| `dxaml/xcp/components/comptree/DCompTreeHost.cpp` | Owns compositor/device/target; creates graphics resources, connects island targets, requests commits and recovers after device loss. | One graphics host per UI session with per-content-root targets. |
| `dxaml/xcp/components/comptree/HWCompNode.cpp` and `HWCompNodeWinRT.cpp` | Bridge element render state to composition nodes. | Keep optional visual realization state associated with the owning UIElement. |
| `dxaml/xcp/core/compositor/UIThreadScheduler.cpp` and `docs/design-notes/scheduling.md` | Coordinate UI frame requests with the dispatcher and display timing. | Coalesce dirty roots and schedule one UI-thread update at a time. |
| `dxaml/xcp/plat/win/desktop/d3d11device.cpp` and `components/graphics/WindowsGraphicsDeviceManager.cpp` | Manage D3D11/D2D devices and recover drawing resources. | Own graphics device and surface generation independently of object identity. |
| `dxaml/xcp/core/compositor/windowrendertarget.cpp` | Renders into a window-associated composition target. | Connect root visuals to an HWND/desktop composition target. |

`VisualContentRenderer::PushRenderDataList` documents per-element reuse of pre- and post-children sprite visuals. `DCompTreeHost::EnsureDCompDevice` obtains the compositor, while `EnsureResources` binds D3D11/D2D graphics resources. `CommitMainDevice` submits changes. `DCompTreeHost` also explicitly handles release/recovery and XAML island targets. These jobs cannot all be represented by a `RenderVersion` counter.

The scheduling design note says the historical independent rendering thread has become predominantly a UI wake-up scheduler: Composition renders asynchronously from the UI thread. Copying its command locks and old high-priority ticks would preserve historical complexity without providing a needed feature. The same note warns against continuous UI frames when independent compositor animations suffice.

## Alternatives

| Approach | Advantages | Limitations | Decision |
| --- | --- | --- | --- |
| Port the HW render walk, HWCompNode, and DCompTreeHost | Broad compatibility with upstream algorithms | Preserves enormous legacy state machine and indirect references | Do not port wholesale. |
| Use raw `IDCompositionDevice` and DirectComposition for the whole runtime | Small initial native prototype | Exposes lower-level COM surfaces and makes WinUI Composition interop/animations harder | Restrict to isolated graphics interop if required. |
| Use `Microsoft.UI.Composition` visuals with Direct2D/D3D for content | Matches the Windows App SDK compositor and desktop target model; supports compositor-owned animations | Requires SDK ABI integration, resource recycling, target creation and device-loss handling | Earlier prototype; superseded by the WUC direction. |
| Use WUC visuals with lifted Content and Input | Delegates composition to Windows while retaining Windows App SDK input delivery | Requires system-visual hosting, platform queues, scale coordination, and ABI decisions | Current direction; native path implemented, runtime validation pending. |

## Proposed implementation contract

`src/runtime/rendering/` contains the graphics host, realization algorithm, and drawing surfaces. These remain native components of the unified runtime. **UIElement owns its native state and optional realization handle**; there is no mirrored Core object or independently activated graphics peer. Content roots own their composition targets. A root's host holds strong composition references, but any callbacks back into elements must use weak references and must be disconnected during teardown.

The UI-thread sequence is: property/tree change marks element dirty; propagation marks the root; dispatcher schedules one update; measure/arrange completes; realization synchronizes transforms, opacity, clips, children and drawing primitives; the host commits composition changes; frame-scoped resources retire. GPU presentation and compositor animations happen independently.

An isolated root can detach without disposing process-wide DP metadata or unrelated roots. The render host must survive transient graphics-device loss without resurrecting detached UI elements. Surface resources are tagged with a graphics-device generation; a device reset invalidates all surface realizations and requests a full rooted redraw. Shutdown cancels queued updates, detaches targets, releases GPU resources in a safe order and drops callbacks before releasing native objects.

### Invariants and validation gates

- All tree mutations, layout and composition-visual-tree updates happen on the owning UI thread; a host does not hold a lock when executing user callbacks.
- Rendering consumes settled layout geometry and never invokes Measure/Arrange recursively during a composition commit.
- A detached element has no retained content-root visual or event callback. Moving between roots recreates only the target-dependent resources.
- Visibility, z-order, transforms, clipping, opacity, DPI and layout rounding are reflected in the realized visual tree. The first drawable primitive is a solid rectangle; text, images, borders and effects require independent visual-content tests.
- Device loss and HWND destruction can happen between scheduling and drawing; each must cancel/recreate safely with no stale render target.
- Establish a real HWND smoke test and visual comparison before claiming UI rendering support. Test detach/reparent, resize, DPI change, root shutdown/restart, lost graphics device, repeated invalidations, and compositor animations.

## Integration sequence

1. Obtain generated C++/WinRT types from the chosen Windows App SDK WinMD, and provide a real desktop compositor target bound to the UI session.
2. Create/reuse per-element `ContainerVisual` realizations and apply tree, geometry, visibility and clip changes. Include a hosted HWND smoke test.
3. Add color rectangle drawing, graphics-device/surface management and device-reset invalidation; then add border, text and images.
4. Schedule realization after a stable layout pass, implement render invalidation coalescing and handle root detach/shutdown.
5. Add transitions, Composition animations, rasterization caches, effects and compatibility tests for external composed controls.

The initial native implementation now includes `CompositionRenderer` and `CompositionBackend` with a desktop Composition child site attached to a real HWND. The smoke test covers root creation, measure/arrange, visual reparenting, resize, opacity changes, rectangle clip updates, 100%-125% DPI switching, and shutdown using an actual dispatcher. It verifies state and completed frames, but does not compare rendered pixels or verify transforms, device loss, text, or images. The renderer currently paints a shared blue rectangle on every visible leaf; it has no public brush or content-rendering pipeline. `RenderVersion` signals invalidation, not rendered frame fidelity.

### Rectangle clipping review (2026-10-24)

`HWCompNodeWinRT.cpp::SetPrependClipRect` retains an `InsetClip` on a visual, updates its size-dependent left/top/right/bottom insets, and clears the clip when absent. The same file handles explicit `UIElement.Clip` (currently `RectangleGeometry`) separately from the **layout clip**, which may be applied in the parent coordinate space before render transforms. These are distinct and must not be collapsed into unconditional clipping to an element's arranged size.

The initial implementation can use an optional native rectangle clip and an `InsetClip` on a composition container, converting right and bottom edges relative to arranged width and height. No clip is applied by default. This is an internal rendering bridge until the public `RectangleGeometry` object, layout clip, transforms, animation and geometric clips are implemented. A real-window smoke exercises changing and clearing the clip, but pixel correctness still needs visual assertions.

### DPI and layout rounding review (2026-10-24)

The upstream `UIElement.cpp` rounds the arrange rectangle's position and dimensions through `LayoutRoundHelper`, using the content root's rasterization scale (`RootScale::GetRasterizationScaleForElement`). `core/compositor/compositortree.cpp` incorporates that scale into the transform to root. The new native `UseLayoutRounding` property defaults to true; the content root propagates a scale to the element tree, rounds slots, sizes, and layout offsets, and invalidates measure/arrange when the scale changes. This currently handles numeric layout rounding but does not implement the upstream property's inheritance rules or transformed-layout subtleties.

`CompositionRenderer::SetRasterizationScale` applies the session scale to the Composition root visual. Its child site retains physical-pixel bounds; element offsets and sizes use DIPs. The host must update both the renderer scale and `ContentRoot::SetRasterizationScale`, and pass DIP viewport dimensions to `ContentRoot::Resize` when its HWND pixel size or DPI changes. The HWND smoke switches between 100% and 125% and verifies a new settled frame. Per-monitor DPI messages, input coordinate conversion, text rasterization, pixel comparison, and automatic host synchronization remain pending. The root-level scaling currently assumes the bridge's visual coordinates correspond to physical pixels; a visual pixel comparison is needed to verify this with DPI-aware hosting.
