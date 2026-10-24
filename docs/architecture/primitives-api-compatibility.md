# Primitive WinUI API compatibility

The target is the **public WinUI 3 runtime contract** and its observable behavior, including C# and externally composed C++/WinRT subclasses. The current `xaml::runtime` native classes are an implementation prototype. Matching native C++ method names does not provide WinRT API or ABI compatibility.

The provisional local baseline is `Microsoft.WindowsAppSDK.WinUI` **2.3.9**, resolved from the current Windows App SDK dependency graph. The checked-in snapshot at `eng/contracts/WinUI-2.3.9-primitives.txt` captures the runtime classes and their associated interfaces (22 definitions for these five types). Verify the snapshot against the installed package:

```powershell
$winmd = "$env:USERPROFILE\.nuget\packages\microsoft.windowsappsdk.winui\2.3.9\metadata\Microsoft.UI.Xaml.winmd"
dotnet run --project eng/tools/WinmdContractInventory -- $winmd --verify eng/contracts/WinUI-2.3.9-primitives.txt DependencyObject DependencyProperty PropertyMetadata UIElement FrameworkElement
```

The reader prints runtime classes, their composable/default interfaces, versioned `I*` interfaces, methods and signatures, properties, events, and metadata attributes (including raw GUID attribute blobs). Its `--snapshot <path>` mode refreshes the comparison file, and `--verify <path>` returns a failing exit code and the first mismatched line on drift. This verifies the reference WinMD snapshot; it does not implement our WinRT ABI or compare a generated WinMD from our runtime.

The snapshot includes accessors and projected methods as well as associated ABI interfaces, factories, statics, events, and property descriptors. No method counts in this file constitute an implementation progress measure.

## Contract sources and exclusions

Use the exact Windows App SDK version chosen for the product as the final public API authority. Extract its `Microsoft.UI.Xaml.winmd` runtime classes, interfaces, method signatures, interface IIDs, contract versions, activatable/composable attributes, threading attributes, and deprecation attributes. Keep an automated metadata comparison in the build once this baseline is selected. The current reference implementation is `D:/source/mux-main` at `0df0a8869`:

- `dxaml/xcp/tools/XCPTypesAutoGen/XamlOM/Model/Microsoft.UI.Xaml.cs` defines `DependencyObject`, `DependencyProperty`, `UIElement`, `FrameworkElement`, and `PropertyMetadata`.
- `dxaml/xcp/tools/XCPTypesAutoGen/XamlOM/Model/UIElement.cs` and `FrameworkElement.cs` contain additional partial class members.
- `dxaml/xcp/dxaml/idl/winrt/main/microsoft.ui.xaml.idl` contains generated-contract inputs; the final WinMD, rather than comments or native-only model attributes, determines public availability.
- `docs/design-notes/dependency-object.md`, `xaml-object-lifetime.md`, `dxamlvscore.md`, `scheduling.md`, and `loading-loaded-unloaded-events.md` explain implementation and lifetime constraints.

Microsoft migration references:

- https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/migrate-to-windows-app-sdk/api-mapping-table
- https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/migrate-to-windows-app-sdk/what-is-supported
- https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/migrate-to-windows-app-sdk/feature-mapping-table

The mapping describes **UWP-to-WinUI migration**, and some exclusions explicitly apply to earlier Windows App SDK versions. Do not delete a member merely because its UWP equivalent is unsupported, has moved namespaces, or is deprecated. For example, the upstream `DependencyObject` model still declares `Dispatcher` as deprecated and `DispatcherQueue` as public; verify the target WinMD before omitting either. `CoreWindow`, `CoreDispatcher`-based app hosting, and UWP activation APIs do not need to be recreated when they are absent from the target WinUI contract.

## Current implementation and contract gaps

| Type | Native behavior available | Required public/runtime behavior |
| --- | --- | --- |
| `DependencyProperty` | Typed and erased native handles, native non-generic boxed registration (including attached properties), stable native `UnsetValue` sentinel, per-instance generated defaults, owner/type validation and metadata callbacks. | Public WinRT `DependencyProperty` factories, `GetMetadata`, `TypeName`, runtime-class projection, delegate ABI, reference tracking, correct enum type identity and immutable registered identity. |
| `DependencyObject` | Native COM identity, weak references, UI-thread checks, typed and boxed local value operations, per-property notifications, current-dispatcher lookup. | Public WinRT methods, event delegates, composable factories, full property precedence and inheritance, expression support, managed reference tracking and cross-thread final-release policy. |
| `UIElement` | Strong visual children, weak parent, layout invalidation/measure/arrange, basic root lifecycle, `Opacity`, `Visibility`, `IsEnabled`, inherited native `UseLayoutRounding` with root DPI scale, optional native rectangle clip forwarded to Composition, and parent-local point hit testing with visual z-order and clip checks. | Full DP set, logical tree, routed input/focus/automation, transforms and animation, public `RectangleGeometry` clip and `IsHitTestVisible` ABI, DPI and rendering parity. |
| `FrameworkElement` | Native width/height constraints, margin, alignment, size-changed native callbacks, overridable measure/arrange. | Public layout API/events, namescopes, resource lookup, bindings, styles/templates, theme, and C#/C++ overridable behavior. |

All four types still lack published WinRT metadata, activation, and composability. The native templated `DependencyProperty<T>` and boxed descriptor remain internal conveniences and cannot serve as the final publicly projected `Microsoft.UI.Xaml.DependencyProperty`. See `reference-tracking-rearchitecture.md` for the CLR ownership requirements.

Native `UseLayoutRounding()` inherits the nearest ancestor's effective value when no local, style, or animation value overrides it. Reparenting and ancestor changes invalidate descendant arrangements. Native `HitTest(Point)` walks children in reverse composition order, rejecting dirty layout, collapsed/disabled/unhittable ancestors, and explicit rectangular clips. This is a geometry input primitive only; it does not implement public `VisualTreeHelper`, pointer routing, transforms, hit-test visibility of brushes or input capture.

The installed `Microsoft.WindowsAppSDK.WinUI` 2.3.9 WinMD declares `MarshalingBehavior(Agile)` and `Threading(Both)` on `DependencyObject` and the element classes. The native prototype advertises COM agility while enforcing UI-thread access in its methods. Agility alone does not authorize cross-thread property or visual-tree access.

## Dependencies and implementation order

1. **Public contract and type system.** Pin the destination SDK and generate a per-member WinMD inventory/diff. Define non-generic dependency property registration and metadata, boxed/unboxed values, `TypeName` resolution, attached properties, and local/unset semantics. Define interface implementations and factory/composition shapes before shipping new public members.
2. **Reference and lifetime model.** Implement tracked object-valued properties, delegates and collections; handle C# GC, `IReferenceTracker` interoperability, weak references, external WinRT composition, the canonical controlling COM identity, and cross-thread final release. Verify with a C# derived element and a C++/WinRT derived element. Simple `winrt::implements` and `com_ptr` ownership alone do not prove these semantics.
3. **UI-thread session and scheduling.** Associate objects with a content root and an actual `Microsoft.UI.Dispatching.DispatcherQueue`. Define shutdown behavior and queue ownership. Connect invalidations to a dispatcher-driven layout/update tick; coalesce repeated changes and protect reentrancy. `DispatcherQueue` alone cannot schedule complete XAML layout and rendering frames.
4. **Full property evaluation and layout.** Add effective-value priority, callbacks, inheritance and expression changes, logical/visual associations, tree connect/disconnect, dirty subtree propagation, layout constraints/alignment/rounding, namescopes, and lifecycle events. Make public `Measure`/`Arrange`, actual sizes and override dispatch behave consistently in managed and native subclasses.
5. **Input, focus, events and automation.** Implement hit testing against realized geometry, routing and handled-event semantics, pointer capture/cancellation, focus navigation, keyboard/drag/drop, AutomationPeer creation and live state. Input and UI Automation need roots and a coherent tree, but they need not block property registration.
6. **Rendering and animations.** Connect the visual tree to `Microsoft.UI.Composition` and DirectComposition, realize drawing content through D2D/D3D where required, handle surfaces/devices/DPI/clipping/transforms, and synchronize UI-thread rendering state with compositor animations. **Before implementing this layer**, re-investigate upstream `dxaml/xcp/core/hw/`, `components/comptree/`, `core/compositor/`, `components/graphics/`, `plat/win/desktop/`, plus relevant history and design notes. Write and review a separate rendering architecture document with alternatives, lifetime, synchronization and failure modes first.
7. **Higher-level XAML services.** Implement resource dictionaries, styles, templates, data binding, theme resource lookup, animation/storyboards and namescopes as required by the public primitive APIs. XAML compilation is deferred, but runtime binding/resource APIs cannot be stubbed for behavioral parity.
8. **Conformance testing.** Compare exported WinMD against the pinned SDK and run Windows App SDK behavioral differential tests on each API. Cover property precedence, events, dispatcher access, layout invalidation, rendering snapshots, managed GC/cycles, subclass virtual calls and error HRESULTs in Debug/Release. Track each method/property/event as `unimplemented`, `native only`, `ABI exposed`, or `behavior verified`; only the final state constitutes compatibility.

The existing native tests establish the small native implementation's behavior. They do not validate XAML activation, C# consumers, WinRT API compatibility, Composition, or rendering. Adding a source-level method without an implementation and behavior test is not progress toward full compatibility.
