# Replacing the Core/DXaml object split

Status: proposal for the `reimpl` branch, not an adopted ADR or an implemented design.
Research dates: 2026-10-23 to 2026-10-24 (JST).
Baseline: local `main`, `0df0a8869ec4aa25d698cdc30d1f7733cf2d532e`.
Reference checkout: `D:\source\mux-main`, detached at that commit.
Reimplementation checkout: `D:\source\microsoft-ui-xaml`, branch `reimpl`, starting at `25b94cf83`.

## Recommendation

Give each framework object one authoritative runtime implementation, with its WinRT interfaces implemented on that same object. Its property storage, framework-owned references, tree participation, and lifecycle must have a single owner. Native framework algorithms operate on that implementation directly. Public ABI entry points adapt into those same operations.

Remove the second Core/DXaml class hierarchy, peer lookup/recreation, mirrored property references, and historical P/Invoke-shaped bridges. Preserve WinRT composition, managed reference tracking, public virtual dispatch, and the distinction between framework state and platform rendering resources.

The important decision is who owns identity, state, and references. Replacing headers with modules or `ctl::ComPtr` with `winrt::com_ptr` does not make that decision.

## What the evidence establishes

Source paths and line numbers below refer to the pinned reference checkout. Historical changes are reproducible with `git show <commit>`; for merge commits use `git diff <commit>^1 <commit>`.

### An obsolete deployment boundary became an object boundary

`docs/design-notes/dxamlvscore.md:18` describes Silverlight's native Core and its subsequently added C# framework. Windows 8 rewrote the framework in C++, retaining the two layers. The document says the layers now occupy the same DLL and explicitly recommends removing the old call indirection at line 106.

The present mechanism is concrete:

- `dxaml/xcp/dxaml/lib/DependencyObject.h:661` stores `m_pDO`; `DependencyObject.cpp:554` returns it through `GetHandle()`.
- `dxaml/xcp/core/inc/CDependencyObject.h:1486` retrieves the opposite peer pointer.
- `dxaml/xcp/dxaml/lib/DXamlCore.cpp:1538` begins the `GetPeerPrivate` overload family. It handles creation modes, reachability, resurrection, activation, registration, and initialization.
- `dxaml/xcp/dxaml/lib/DXamlCore.cpp:1767` warns that `PrepareState` can enter managed code and deadlock a background GC if performed under the peer-table lock.

The original separation served real managed/native and deployment boundaries. The architectural mistake for a new implementation would be preserving those boundaries after their original purpose has disappeared.

### Simple operations cross the historical boundary in both directions

For a property write, generated `FrameworkElementGenerated::put_Width` calls the framework property helpers (`dxaml/xcp/dxaml/lib/winrtgeneratedclasses/FrameworkElement.g.cpp:377`). `DependencyObject::SetValueCore` then calls `CoreImports::DependencyObject_SetValue` (`DependencyObject.cpp:1125`). The bridge (`dxaml/xcp/core/dll/CoreImports.cpp:1852`) updates ambient caller bookkeeping and invokes the Core object.

This is not permission to delete every bridge without understanding its semantics. The ambient write origin and expression-clearing behavior must become explicit inputs to the replacement property operation.

In reverse, `CFrameworkElement` dispatches custom layout through `FxCallbacks::FrameworkElement_MeasureOverride` (`dxaml/xcp/core/core/elements/framework.cpp:1715`). That calls `FrameworkElement::MeasureOverrideFromCore` (`dxaml/xcp/dxaml/lib/FrameworkElement_Partial.cpp:321`), which looks up the peer and invokes the protected override. The replacement must remove peer recovery while retaining the user's override and its base-call behavior.

### Property ownership and GC ownership are split

`dxaml/xcp/core/core/elements/depends.cpp:197` maintains a managed reference corresponding to a property stored on the Core side. `PropertySystem.cpp:2424` and surrounding paths update those peer references when property storage changes. `depends.cpp:2129` walks both Core properties and the framework peer for reference tracking.

Consequently, a property update can mean updating a value in one representation and updating the reference graph in another. The replacement should have one stored ownership edge from which both normal execution and GC traversal derive their information.

The problem is not reference tracking itself. `docs/design-notes/xaml-object-lifetime.md` demonstrates a managed event delegate capturing a native tree: ordinary COM strong references leak the cycle, while arbitrary weak references can collect a still-needed handler. Neither plain RAII nor making every back-reference weak solves that contract.

### Lifetime is a product of several independent states

`DependencyObject.cpp:777` documents a GC/final-release interleaving that can queue the same object after it has been deleted. The comments describe an additional final-release check as a mitigation with a remaining timing window. This is source commentary, not a runtime reproduction performed during this investigation.

Recent reachable history supplies a particularly useful lesson:

| Commit | Observed change | Design implication |
| --- | --- | --- |
| `7a0400b53`, 2026-09-11 | Introduces a peer-lifetime state and transition gate over existing bookkeeping. | A derived state description does not automatically become the authority that changes state. |
| `c90563207`, 2026-09-11 | Announces teardown transitions; explicitly observational. | Instrumentation is distinct from a new ownership protocol. |
| `282723088`, 2026-09-21 | Allows disconnect-before-final-release ordering. | Logical disconnection and physical destruction are independent. |
| `7ccd1f3ec`, 2026-09-22 | Removes fatal assertions on derived transitions after ordinary teardown crashed checked test hosts. | A linear state machine inferred from old flags can reject legal behavior. |
| `4f83defb7`, 2026-10-08 | Adds a wake for idle release cleanup. | Correct reclamation cannot require another render frame. |
| `a92b69f32`, 2026-10-08 | Synchronizes release-queue reads with background GC; adds a managed-cycle stress scenario. | The UI thread is not the only participant in lifetime bookkeeping. |
| `d0cf13fc0`, 2026-10-13 | Preserves metadata across shutdown so static custom DPs remain valid after restart. | Process metadata and thread/session services need different lifetimes. |
| `0df0a8869`, 2026-10-23 | Moves WebView2 weak resolution to the UI callback and removes a delayed strong control capture. | Even newer C++/WinRT controls still need explicit callback and final-release ownership. |

`WeakReferenceSourceNoThreadId.h:213` confirms that the current state is derived from old bookkeeping. Do not carry that observational model into the rewrite as a proven lifecycle algorithm.

`docs/design-notes/xaml-shutdown.md:166` still labels process restart as unfixed. Commit `d0cf13fc0` supersedes that statement: its `dllentry.cpp` change explains the static-DP problem and deliberately abandons metadata at process termination to avoid calling unloaded code. This is an example of why notes must be checked against source and history.

### Other old assumptions should not define the new kernel

`docs/design-notes/startup-overview.md` describes desktop startup creating a meaningless CoreWindow content root and a UWP window implementation. `docs/design-notes/scheduling.md` documents remnants of an old rendering thread whose present purpose is scheduling.

These are adjacent design lessons, not evidence that every startup or scheduling path is currently broken. Start a new runtime around an actual thread session and content roots, with rendering delegated to Composition. Do not copy browser/UWP bootstrap scaffolding as a prerequisite for creating an object.

The inspected history is the history reachable from local `main`, including imported/squashed source drops. It does not independently reconstruct every Silverlight-era decision. The early historical account above is attributed to the design notes.

## Alternatives considered

| Alternative | Benefit | Main cost | Assessment |
| --- | --- | --- | --- |
| Modernize Core and DXaml separately | Easy mechanical migration; retains lazy peers and existing code structure. | Keeps two identities, two ownership representations, and reverse bridges. | Useful for an incremental maintenance branch; wrong foundation for this rewrite. |
| Keep an independent native engine plus a WinRT wrapper per object | Makes the engine appear ABI-independent and easy to test. | Reintroduces peer lifetime, identity translation, and override dispatch. Sharing a state pointer only partially fixes it. | Reject as the default object model. |
| One runtime implementation with WinRT facets and native operations | One state owner; direct native access; public compatibility stays at entry points. | Requires deliberate reference tracking, composition support, and object-size measurement. | Recommended. |
| Make the whole runtime an ECS/handle store with WinRT facades | Could improve locality and batch execution. | Makes externally derived classes, COM identity, callbacks, and deterministic reclamation much harder. | Consider dense stores for measured hot data later, not for public object identity initially. |

The recommendation permits inline helper structs, optional side allocations, algorithm objects, and interface tear-offs. They must not become independently activatable counterparts that own another copy of the framework object's state.

## Object and ABI model

For a built-in object, one implementation owns the framework state and implements its public WinRT interfaces. A generated ABI thunk calls the same native operation used by internal callers. Public and internal paths converge before validation, storage updates, and invalidation.

```text
Public caller                         Native framework algorithm
     |                                          |
WinRT ABI thunk                                 |
     +-------------------+----------------------+
                         |
             One framework implementation
             - canonical property storage
             - tracked outgoing references
             - tree/layout state
             - lifecycle control
                         |
              Composition realization data
```

This is one framework state owner, not a promise of one allocation or one COM object in every scenario:

- An externally authored C# or C++ derived type may have an outer object and a framework inner object under WinRT composition. Preserve the controlling `IUnknown`, identity rules, and override protocol.
- RCWs, CCWs, delegates, collections, AutomationPeers, and Composition Visuals remain real objects with distinct roles.
- Optional interface tear-offs must resolve to the same controlling identity and lifetime authority. They must not recreate the old semantic peer.
- Removing Core/DXaml peers does not mean removing UI Automation peers or merging logical, visual, resource-scope, and rendering graphs.

Use typed native references to known framework implementations internally. Avoid repeated public `QueryInterface` and boxing on hot paths. At an external boundary, validate how an object relates to the framework implementation; do not `get_self`/cast an arbitrary projected object to an assumed concrete type. Composed user classes need a supported route to the framework base, not a second semantic peer table.

Place ABI adaptation in generated thunks/mixins on the implementation, not in another owning facade object. C++/WinRT is the intended production machinery for ABI support, but its default lifetime behavior must not be assumed to implement the framework's complete reference-tracking contract. The first prototype must establish the supported integration point. Avoid patching `winrt::impl` internals or stacking two independent refcount/weak-reference implementations.

Use normal native helpers and explicit service inputs for internal-only data. A rectangle, layout work item, parser instruction, or render command should not inherit `DependencyObject` merely to obtain services or participate in an internal algorithm.

## References and lifetime

### One graph, several reference meanings

Define reference semantics before selecting pointer templates:

| Reference kind | Required behavior |
| --- | --- |
| Framework-owned object/delegate/value edge | Strong semantic ownership, reported to the tracker protocol; native access must not silently introduce an unrelated external GC root. |
| Native root | Explicit protection from a host root, active operation, or external native reference; has a bounded owner and participates in tracker root accounting. |
| Parent/observer backlink | Non-owning, invalidated or resolved safely under the documented mutation rules. A shared Brush has multiple consumers, not a fabricated single visual parent. |
| Stack borrow | Valid for a bounded operation that owns its participants and follows reentrancy rules; cannot escape into an async capture. |
| Queued callback | Cancellable registration plus weak target/session generation; resolve the target on its owning UI thread. |

Tracker-visible property slots, event lists, and collections own the actual reference. Generate traversal from their storage description; do not maintain a shadow map solely to make the same references visible to GC. Include non-effective owned values such as animation base values, expressions, bindings, and resource references: tracing only the currently visible value is incorrect.

A registry of tracker participants can still be necessary. It enumerates canonical objects; it does not translate Core identities into DXaml identities. Removing the old peer table does not prove that a tracker registry or synchronization is unnecessary.

Preserve C# cycle collection and live-native-tree protection. Pure native application cycles that COM cannot collect need explicit revocation/ownership discipline; do not claim that the managed tracker protocol is a universal native GC.

### Separate lifecycle dimensions

Model at least these dimensions separately:

- Initialization/close: constructing, usable, closing, closed.
- Attachment: detached or attached to a particular root, with a tree generation.
- Tracker bookkeeping: sources, native roots, walk generation, and reachability established by the GC protocol.
- Reclamation: not queued, queued once, destroying; coordinated with zero-count/weak-resolution rules.

These are conceptual dimensions, not a mandate for four per-object enums. Encode compactly after correctness is demonstrated. Tree removal must not close an object that the app still owns. Reachability can change between collections. A closed implementation cannot acquire fresh UI resources or reconnect to a new runtime session.

One lifetime implementation owns the transitions and counters. It may need several counters to implement COM, tracking, and composition; a single naive atomic reference count is not the goal. Weak resolution and final-release admission must share a protocol that prevents resolution after destruction begins.

When final release requires UI work, transfer responsibility exactly once to a reclamation record protected by the lifetime protocol. Background GC queues cleanup rather than running control destructors. On the UI thread, unlink work under the graph lock, then release references and execute application callbacks outside that lock. Tracker-protocol callbacks have their own synchronization contract and must not be confused with arbitrary application callbacks. Never post an unprotected raw object address and hope the next frame arrives before deletion.

Cleanup has an explicit dispatcher wake independent of rendering. Shutdown stops new producers and drains accepted work while the queue is still serviceable. Rejected enqueue must have a defined owner/fallback; neither dropping a required cleanup nor recreating the runtime from a late `Release` is acceptable.

## One property system

All setters, XAML loading, styles, bindings, and animations use a common operation with explicit write origin. A typed built-in setter is an optimized entry into that operation, not a second path that bypasses precedence or reference maintenance.

The operation should:

1. Validate thread/session access, property ownership, value type, and association constraints.
2. Resolve source precedence and expression behavior; prepare any fallible allocation/conversion before changing observable storage.
3. Publish the new storage, tracked edges, associations, and version as one internally consistent change. Retain old/new participants through callbacks.
4. Record metadata-driven measure/arrange/render/resource invalidation.
5. Notify at the compatibility-required synchronous point after releasing graph locks. Nested calls must see a committed state, not half an update.

This does not promise arbitrary application callbacks can be rolled back. Specify the observable state when a callback throws or closes/reparents the target. Retire old values outside locks because releasing them may itself invoke foreign code.

Use generated typed accessors, compact scalar slots, and lazy sparse storage. Carry default/inherited/local/animated values and resource expressions deliberately; do not reduce all values to `IInspectable`. Box only where the public API requires it. Preserve numeric, null, unset, validation, and coercion semantics rather than mechanically narrowing all public doubles to floats.

Generate whether a property affects layout/rendering and whether it owns an association or merely references an object. `docs/design-notes/dependency-object.md` shows why a visual child, a shared Brush, and an ordinary object reference cannot all use the same parent rule. Existing built-in/custom DP differences are compatibility questions to test, not inconsistencies to normalize silently.

## Layout, overrides, and reentrancy

The layout scheduler calls the same element implementation that owns layout state. Built-in implementations use native dispatch. An externally composed override uses the proper WinRT overridable dispatch and a distinct base implementation entry, so a user's `base.MeasureOverride` cannot recurse through its own override. No Core-to-framework peer recovery is involved.

Calling application code is a reentrancy boundary. Hold necessary participants alive, publish consistent state, release graph locks, invoke, then check session/tree/property generations before continuing. An override or event handler can remove its element, replace children, close a window, or start a nested Win32 message pump.

Construction needs the same discipline: establish the controlling identity and lifetime support before running fallible markup initialization or callbacks that may escape the object. Test failure during initialization and derived component connection; an object's C++ constructor finishing does not alone establish that its public initialization protocol is complete. The distinction between in-component implementation inheritance and cross-component composition must be part of Stage 0; the official [authoring guide](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/author-apis) and [base-call guide](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/base-type) describe that distinction and the override entry points.

Use explicit execution phases for tree mutation, layout, event delivery, render realization, and reclamation. Suspend/defer conflicting internal work during a nested pump; permit documented synchronous property and tree operations. Do not solve reentrancy by making all events asynchronous or by copying an unconditional global fail-fast guard.

Preserve public event ordering through compatibility adapters. `docs/design-notes/loading-loaded-unloaded-events.md` documents different scheduling for Loading, Loaded, and Unloaded, including surprising ordering. A clean internal attachment model does not authorize silently changing those observable semantics. Tag internal queued work with generations; whether a legacy public event is suppressed or delivered must be specified and tested separately.

## Services and platform boundaries

Use three lifetime scopes:

| Scope | Owns |
| --- | --- |
| Process contract catalog | Built-in type/property descriptors and stable public identifiers; process-lifetime custom registration policy. |
| UI thread session | Dispatcher integration, reference-tracker coordination, execution phases, work queues, and session generation. |
| Content root | Actual host connection, visual root, layout scheduling, focus/input state, and Composition realization. Multiple roots can share a thread session. |

Pass concrete narrow service dependencies where needed. Keep thread-local lookup at public activation/entry points rather than turning a replacement `RuntimeContext` into another universal service locator. Platform contracts use operation-specific values/handles and callbacks; they do not expose a replacement `IDxamlCore` containing every subsystem.

The runtime provides platform-neutral policy and algorithms where useful. The Windows backend owns windowing, dispatcher integration, Composition, text services, and UIA adapters. Test doubles implement those narrow boundaries without pretending the product is an ABI-free engine.

A Composition node/surface is a derived realization of framework state with its own resource lifetime. Preserve dirty-subtree updates, shared resource use, device-loss recovery, external visuals, and independent animations. Avoid a one-Visual-per-element requirement, a full-tree rebuild on every setter, or inventing a new compositor as part of object-model unification.

During shutdown, stop external producers, detach/close roots and deliver required events, finish accepted cleanup, release thread-affine platform resources, and retire the session before the dispatcher becomes unavailable. Externally referenced closed objects may remain allocated; their late release must not require a live session. Public error/thread behavior must match the selected compatibility baseline.

Separate process metadata from session caches. Static custom DPs cannot become dangling identifiers when a thread runtime restarts. A purely immutable catalog is insufficient for arbitrary custom default values and callbacks: these may reference app objects and code in other modules. Specify registration ownership, module pinning, and the process-exit abandonment policy before claiming safe DLL unload. Do not attempt foreign releases from `DllMain`.

## Modules, generation, and build

The proposed starting compilation graph is intentionally coarser than the source folders in `docs/repo-structure.md`:

```text
Xaml.Foundation          standard values, IDs, diagnostics, small utilities
Xaml.Platform.Contracts narrow platform service contracts
Xaml.Runtime             objects, properties, tree, lifecycle, execution policy
Xaml.Controls            control implementations on Xaml.Runtime
Xaml.Markup              loading/compiled-markup execution on Xaml.Runtime
Xaml.Platform.Windows    implements Xaml.Platform.Contracts
Product                  wires services/factories and links Microsoft.UI.Xaml.dll
```

`Xaml.Runtime` imports Foundation and Platform.Contracts. Controls and Markup import Runtime; Windows imports Platform.Contracts and Foundation. Product supplies implementation factories and registrations; Runtime does not import Controls, Markup, or Windows. Contract types can use platform projection types where required by the Windows product, but must not import runtime implementation types.

Initially keep the strongly connected object/property/tree/lifecycle code in one named module with internal partitions and implementation units. Source directories can separate layout, input, resources, and scheduling without forcing separate modules or projects. Module partitions improve organization but are not an excuse to create cyclic import dependencies; design their interface dependencies as a DAG. Split independent algorithms into modules only when dependencies justify it.

New C++ uses `/std:c++latest`, named modules, and `import std` where the pinned compiler supports the required combination. Use standard ownership, ranges, spans, and constexpr tables where they simplify real code. Do not depend on unverified reflection/compiler features just to avoid a small generator. Module export is compile-time visibility, not a DLL export or a stable external C++ ABI.

Use `Microsoft.UI.Xaml.slnx` and a small number of MSBuild projects. Initially a product DLL project, generator/tool projects, focused test projects, and consumer samples are sufficient; multiple named modules can compile in one product project. Add a platform projection builder only when multiple native projects actually benefit. Do not create a project per directory or class.

Toolchain observations on 2026-10-23:

- Installed Visual Studio Enterprise 2026: 18.11.0; MSBuild: 18.11.0.47511; default MSVC tools: 14.51.36231; installed Windows SDK: 10.0.26100.0. These were inspected, not build-validated for the proposed runtime.
- NuGet's version index and package page identify `Microsoft.Windows.CppWinRT` 3.0.260818.1 as the current stable package. It is already present in the local package cache. Pin it for the initial experiment rather than floating a 3.x range.
- The packaged `modules.md` documents generated projection modules, `CppWinRTBuildModule`, `BuildStlModules`, and the restriction that optimized component projection IFCs are private to their producer. Consumers generate reference projections from WinMD. Share platform IFCs only under compatible toolset/configuration settings.

The official [C++/WinRT module guide](https://github.com/microsoft/cppwinrt/blob/master/nuget/modules.md), [NuGet package](https://www.nuget.org/packages/Microsoft.Windows.CppWinRT/3.0.260818.1), and [SLNX description](https://devblogs.microsoft.com/visualstudio/new-simpler-solution-file-format/) are supporting sources. For implementation, prefer the pinned package's shipped instructions over assuming repository `master` matches it.

Separate generated WinRT projection modules from hand-authored runtime modules. Generate contracts/WinMD before compiling the implementation, so the platform module builder never needs to build the product in order to generate modules required to build that same product. Do not import a second projection of the product's own types or link an existing WinUI DLL as the runtime underneath the rewrite.

Keep `src/contracts` as the authoritative contract area. Recommended initial format: IDL for public ABI facts plus narrowly scoped supplemental XAML metadata keyed to contract symbols. Avoid duplicating signatures across IDL, C++ and a second complete schema. Generate native accessors, property/type descriptors, tracing, and tracker traversal from validated inputs. Use standard MIDL/C++/WinRT tooling for the ABI rather than implementing a new WinRT compiler inside XamlGen.

Preserve explicit historical ABI identities when importing existing contracts; regenerating similar MIDL3 syntax alone does not prove identical GUIDs, interface order, Fast ABI layout, or composability. Validate generated metadata against a baseline. Keep runtime-internal compact IDs separate from public identity and stable serialized XBF indexes.

All generated sources/IFCs go under configuration-, architecture-, and toolset-specific build output. Make generation deterministic and incremental, including deletion/rename of inputs. Test clean restore/build, no-op build, and a contract edit; a successful build with stale generated files is insufficient evidence.

## Implementation order and exit criteria

This is a fresh implementation beside a pinned reference, not a mechanical merge of both old class hierarchies. Extend support in vertical slices; unsupported features must remain explicit rather than falling through to the old runtime.

| Stage | Deliverable | Exit criteria |
| --- | --- | --- |
| 0: contract and toolchain experiment | Minimal native WinRT component with a base/derived type, modules, C++ and C# callers. | Clean MSBuild build; stable identity across interfaces; external composition and base calls; reference projection isolation; supported tracker/final-release integration demonstrated. |
| 1: object/property kernel | A framework object, custom DP registration, primitive/object storage, event delegates, tracked references, thread/session lifecycle. | Native rooting preserves a managed handler; unreachable managed/native cycles collect; weak resolution races and off-thread final release are safe; rejected enqueue and idle cleanup have deterministic outcomes. |
| 2: real element slice | UIElement/FrameworkElement foundations, a small Panel and Rectangle/Brush path, actual content root and Composition output. | Code-created tree renders; C# and C++ layout overrides/base calls work; shared Brush invalidates both consumers; duplicate visual parenting fails without corrupting the tree; property mutations invalidate the correct work. |
| 3: markup and control slice | Metadata-driven loading, a template/resource path, and one interactive templated control. | XAML and code creation produce equivalent state; custom types/DPs/events load; resource scopes, template ownership, property precedence, and representative event ordering match the chosen baseline. |
| 4: expansion | Binding, styling, animation, text, focus/input, UIA, virtualization, popups, and further controls. | Each subsystem extends the same object/reference model and adds behavior, lifecycle, and performance coverage before being advertised as supported. |

Stage 0 is a feasibility gate, not proof of broad WinUI compatibility. If C++/WinRT cannot support the required native implementation inheritance/tracking hook cleanly, resolve that specific authoring issue before generating hundreds of classes. A targeted lifetime/ABI support layer may be necessary; a second Core runtime is not the fallback.

Do not begin with Button or the complete XAML compiler. The smallest rendering slice should prove the object-model decision independently of templates, resources, input, and automation. Conversely, a C++-only rectangle demo is not enough to validate the design: the C# ownership/composition tests belong before expanding the control surface.

## Validation matrix

Use behavioral tests, including adversarial interleavings, rather than tests that merely assert the proposed folder layout or enum names.

- Identity: interface round trips yield the controlling identity; C#/C++ derived objects preserve state and protected override behavior; helpers do not introduce another semantic object.
- Properties: built-in/custom DPs, unset/default/null, clear/local/style/animation precedence, expression ownership, inherited values, shared objects, and failure during association/conversion/callback.
- Managed lifetime: live native tree plus captured delegate, detached app-owned tree, unreachable Grid/delegate cycle, `Tag` managed cycle, shared resource/delegate, weak references, and user-derived outer objects.
- Concurrency: background GC against edge mutation, weak resolution against last release, duplicate cleanup admission, producer arrival during queue drain, and last release during/after shutdown.
- Reentrancy: event handler writes a property, removes itself, replaces the root, starts a nested pump, or shuts the host down; layout override changes children or throws. Check consistent state and bounded scheduling.
- Session lifetime: no further rendering, no remaining roots, two roots on one thread, multiple UI threads, dispatcher rejection, repeated startup/shutdown, static custom DP reuse, and late external release without context recreation.
- Rendering/resources: changing one shared Brush affects all consumers, unrelated subtrees stay clean, independent animation does not require continuous UI ticks, and device loss reconstructs realizations without replacing framework objects.
- Build/contracts: clean and incremental module builds, no duplicate IFCs, native/C# WinMD consumers, and API/ABI identity comparison for the implemented surface.

Existing reference material for tests includes `dxaml/xcp/components/lifetime/unittests/LifetimeUnitTests.cpp`, `WeakReferenceUnitTests.cpp`, `controls/test/MUXControlsTestApp/Utilities/ReleaseQueueTestHelper.cs`, and `controls/test/MUXControlsTestApp/LeakTests.cs:151`. Adapt the scenarios; do not import fakes that depend on the old peer model.

Measure object size, allocations per tree element, external interface overhead, native versus ABI property access, tree creation/destruction, GC walk/cleanup cost, clean build time, and incremental rebuild scope. Lazy peer creation currently saves some allocations and ABI state: unification is not automatically a memory improvement. Start with optional sparse data and add facet optimizations only after measurement, retaining stable identity.

No runtime build, stress test, benchmark, or compatibility test was executed for this proposal. The completed work is a source/history investigation, a clean reference worktree, and this proposed architecture. Full binary compatibility, serialized XBF compatibility, designer integration, supported OS versions, and safe DLL unloading remain separate acceptance scopes to establish; none is implied by the folder structure or the use of modern tools.

## First implementation decision

The proposed direction is **one framework state owner with native internal operations and boundary ABI adaptation; no Core/DXaml semantic peers**. The first coherent implementation would cover Stage 0 and Stage 1. Those stages resolve the highest-risk questions—composition, reference tracking, final release, and property ownership—before control count or rendering sophistication obscures them.
