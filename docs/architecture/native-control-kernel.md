# Native control kernel

This slice implements native behavior on the unified object model; it does not yet expose the public WinUI control contracts.

## Ownership and dependencies

`Xaml.Runtime` contains `DependencyProperty`, `DependencyObject`, `UIElement`, and `FrameworkElement` under `src/runtime/primitives/<Type>/`. Its internal partitions share one named C++ module; `:Primitives` provides UIElement and small geometry types, and `:FrameworkElement` provides its derived type. `Xaml.Controls` imports Runtime and contains `Control`, `ContentControl`, `ButtonBase`, `Button`, `Panel`, and `StackPanel`. These native implementation types share the same C++/WinRT object identity without Core/DXaml peers. Runtime and Controls currently build as separate static libraries; this does not introduce a runtime DLL or a WinRT peer.

Every control has a named source directory in `src/controls`, including foundational types such as `ButtonBase` and `Panel`. Place each control's future XAML resource sources alongside its implementation (for example, `src/controls/Button/Button.xaml`); shared theme services can live in `src/themes`. No XAML resource is functional until markup compilation, resource lookup, templates, and rendering exist. The XAML resource does not own the control's native state. Folder, C++ module, static library, and public WinRT namespace are separate choices.

## Supported behavior

- Typed arithmetic/enum and small trivially-copyable value properties with owner checks, attached registration, validation, default/local storage, local-value reads, clear, and committed-state notifications. Registration metadata can call a typed old/new-value callback. Per-object property callbacks support token-based removal and reentrant changes. Reference properties still need tracked ownership. A local entry retains its metadata. Equal effective values do not produce notifications; NaN equals NaN for automatic lengths.
- Strong owning visual-child edges and weak parent backlinks. `ContentControl` accepts one UIElement; `Panel` holds ordered children. Duplicate parenting, null children, and cycles fail before changing the tree. Moves retain parent associations. Tree mutations retain participants across reentrant notifications.
- Basic native measure/arrange overrides, finite-result validation, cached layout, and ancestor invalidation. An invalidation during a pass survives its completion. Render versions expose changes to a future renderer without claiming that any rendering has completed.
- Width/Height, MinWidth/MaxWidth, MinHeight/MaxHeight, margin, Visibility (Collapsed), auto sizing through NaN, inherited effective IsEnabled, and content measurement/arrangement. `Control.Padding` stores a validated `Thickness` value and offsets the content measure/arrange rectangle. These are deliberately a subset of full XAML layout.
- `StackPanel` measures children without a constraint along its stacking axis, positions them in order with nonnegative spacing, and supports horizontal/vertical layout. Collection changes and orientation/spacing changes invalidate the containing tree. Child enumeration for layout uses strong snapshots so removal during an override cannot destroy an active participant.
- Button release/press/hover modes, pointer identity, pointer-over/pressed/disabled state, and Space/Enter activation. Disabling, focus loss, detaching an ancestor, cancellation, or capture-loss notification cancels pending activation. Direct parent changes and ancestor connection changes are distinct notifications; reordering retained siblings sends neither.
- Native Click subscriptions. Removing a handler during dispatch suppresses its later invocation; additions participate on the next dispatch. The sender stays alive through callbacks, including callbacks that remove it from its parent and release the last external reference. Release/key-up state commits before Click.

Input facts are already targeted to the element. `SetFocused` records a host notification; it is not a global focus manager. `ActivePointer` tracks an interaction; it does not acquire OS capture. A future input backend must hit-test, arbitrate focus/capture, and deliver cancellation. Parent destruction is not yet a content-root shutdown protocol.

Property and event callbacks run synchronously without graph locks. If a callback throws after a mutation, the mutation remains committed. Native callbacks can form application-owned cycles; there is no managed cycle collection in this slice.

The weak visual-parent edge resolves through the canonical DependencyObject weak source and converts back only to the known native UIElement that created the edge. It never casts an arbitrary external WinRT object, recreates a peer, or uses a global identity map.

## Reference behavior

The reference checkout is `D:\source\mux-main` at `0df0a8869`.

`docs/design-notes/dxamlvscore.md` traces the split from Silverlight's native Core and managed `System.Windows` controls, including the later move of Button behavior into `CButton` for performance. `docs/design-notes/control-overview.md` notes that newer WinUI 2 controls do not follow the classic DXaml/Core peer model. Thus the old `dxaml/xcp` versus `controls/dev` directory division is historical, not a suitable new implementation boundary. `Button_themeresources.xaml` holds visual resources while `ButtonBase_Partial.cpp` and the native Button implementation hold behavior. `NavigationView.cpp` is a higher-level MUXC implementation.

| Old source | Responsibility in the new implementation |
| --- | --- |
| `dxaml/xcp/core/core/elements/depends.cpp` and DXaml DependencyObject | One property owner, without a mirrored peer-reference table. |
| `dxaml/xcp/components/Collection/DOCollection.cpp` | Single-parent association checks and ordered child mutation directly on UIElement, without a separate Core object graph. |
| `dxaml/xcp/core/core/elements/framework.cpp` | Native layout dispatch and cached state on the actual element. |
| `dxaml/xcp/dxaml/lib/ContentControl_Partial.cpp` | Content ownership and its layout on the same control. |
| `dxaml/xcp/dxaml/lib/ButtonBase_Partial.cpp` | Button interaction and native Click dispatch. |
| `dxaml/xcp/components/controls/KeyDownUp/inc/ButtonBaseKeyProcess.h` | Space/Enter activation and repeat suppression. |
| `dxaml/xcp/dxaml/lib/Button_Partial.cpp` | Common Button visual-state selection. |
| `dxaml/xcp/core/core/elements/Button.cpp` | Flyout tree participation is deferred with Flyout itself. |

This is a selective reimplementation, not a claim of behavioral or ABI parity with all of those files. In particular, native release/key-up callbacks observe completed input state, and this native slice does not reproduce all legacy event-timing quirks.

Before implementing rendering, re-investigate the upstream GPU render walk, Composition tree, graphics devices/surfaces, and scheduling, including design notes and history. Document the alternatives, responsibilities, and chosen boundaries first. This review is deferred until rendering implementation begins.

## Subsequent slices

1. Establish tracker-aware reference storage and UI session/reclamation rules before accepting managed object-valued properties, delegates, or public composed controls.
2. Add public contract generation and adapters onto these implementations; validate native/C# identity, composition, override/base-call, and GC behavior.
3. Connect content roots, actual input focus/capture/routing, and Composition realization.
4. Expand layout, resources/templates, commands, Flyout, accessibility, binding, and animation with explicit compatibility tests.

There is no style/binding/animation property precedence, general object Content, routed event system, complete layout policy (alignment/clipping/DPI and other behaviors), template/VSM rendering, ICommand, Flyout, UI Automation, or public WinMD activation in this slice. No existing Microsoft.UI.Xaml DLL supplies those behaviors behind the new types.
