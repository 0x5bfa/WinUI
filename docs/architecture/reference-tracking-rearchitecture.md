# Reference tracking and managed object lifetime

## Design constraints

The runtime has one canonical native `DependencyObject` identity. Visual children currently hold strong native COM references and the visual parent holds a weak reference. Object-valued dependency properties and callbacks can also hold COM references. These edges are safe only for acyclic native ownership; a managed event handler capturing its parent can form a cycle through a CCW, RCW, and native tree.

The upstream analysis in `D:/source/mux-main/docs/design-notes/xaml-object-lifetime.md` describes two distinct requirements: when a XAML root is live, managed targets reachable through the tree must stay alive; when the tree is reachable only through an otherwise dead managed cycle, the CLR must be able to collect it. Permanently strong COM references leak, while unconditionally weak targets lose live event handlers.

The upstream interfaces are defined in `dxaml/xcp/dxaml/idl/com/microsoft.ui.xaml.hosting.referencetracker.idl` and the Windows SDK reference tracker headers. They include `IReferenceTracker` (`11d3b13a-180e-4789-a8be-7712882893e6`), `IReferenceTrackerTarget` (`64bd43f8-bfee-4ec4-b7eb-2935158dae21`), `IReferenceTrackerManager` (`3cf184b4-7ccb-4dda-8455-7e6ce99a3298`), `IReferenceTrackerHost`, and `IFindReferenceTargetsCallback`. The manager's GC handshake and tracker-target enumeration are part of the contract, not optional methods to stub.

## Implementation boundary

Keep native/managed lifetime policy alongside `DependencyObject` and its property/event edges. Retain one controlling COM identity across composable derived WinRT objects. Distinguish native root-owned references from managed tracker edges, and ensure GC traversal sees dependencies stored in properties, event delegates, collections, and the visual tree. When a managed subclass is the controlling unknown, account for `IReferenceTrackerExtension` and `ITrackerOwner` from the upstream IDL.

The implementation must coordinate add/release, peg/unpeg, target enumeration, GC callbacks, reentrant property changes, object shutdown, and thread-affine destruction. A no-op `IReferenceTracker` implementation would incorrectly advertise GC interoperability and is prohibited.

## Required validation before ABI exposure

1. Create a C# `DependencyObject` subclass and a C++/WinRT composable subclass; verify one identity and correct virtual dispatch.
2. Keep a managed delegate alive while its control is externally rooted. Remove all external roots; run forced GC and verify the delegate, RCW, CCW, and native graph are collectible even with a cycle.
3. Exercise property replacement, event unsubscribe during invocation, detach, dispatcher shutdown, and GC target enumeration occurring near native release.
4. Verify correct behavior in Debug and Release with a real reference tracker host, not merely native COM `AddRef`/`Release` tests.

Status (2026-10-24): **not implemented**. Native COM identity, weak visual parent, and strong native property storage are tested, but CLR participation, public composability, and managed cycle collection are unverified.
