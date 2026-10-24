# Project goals

Status: agreed project direction. This document defines intended outcomes, not current implementation completeness.

## Final outcome

Deliver a WinUI implementation built around one authoritative native runtime object model, without the legacy Core/DXaml peer split. Distribute it as NuGet packages that an external application can reference to build, run, and deploy.

Selected existing WinUI applications must be able to migrate by changing package references and making documented source changes for deliberately retired APIs. Their required runtime behavior and supported binary contracts must remain intact.

Choose and record a concrete reference application before expanding into application compatibility. Record its language, Windows baseline, dependency versions, controls, XAML features, third-party libraries, and deployment requirements. Support for every existing WinUI application is not the initial completion criterion.

## Compatibility contract

Treat source compatibility, ABI compatibility, and behavioral compatibility as separate requirements.

| Contract | Required outcome |
| --- | --- |
| Source migration | The reference application rebuilds against the new packages with explicitly documented changes for retired or redesigned APIs. Full source API compatibility is not required. |
| WinRT ABI | Preserve the identities, interface layouts, method signatures, activation contracts, and composition behavior of the supported existing binary surface. |
| Runtime behavior | Layout, property evaluation, resources, bindings, input, focus, lifetime, and shutdown behave as required by the reference application. |
| Deployment | The deployed application resolves and loads the intended replacement implementation and its declared dependencies. |

Inventory and pin the supported ABI before publishing compatibility claims. Identify which existing compiled libraries must work without rebuilding. APIs outside the initial supported surface remain explicitly pending; narrowing the initial scope is not evidence that their ABI can safely change.

Removing an API from the recommended source surface must not remove or reorder members of an existing supported ABI interface. A retained member that fails at runtime may preserve interface layout but does not establish behavioral compatibility. Document each retirement and its migration path.

Composition and dispatching types need an early decision. Existing signatures exposing `Microsoft.UI.Composition.Visual` or `Microsoft.UI.Dispatching.DispatcherQueue` cannot simply change to WUC or custom types while preserving those signatures. Record the compatibility strategy before implementing the affected public contracts.

## Required product capabilities

The runtime is one part of the deliverable. An application-facing package also needs the following capabilities, introduced as the milestones require them.

| Area | Responsibilities |
| --- | --- |
| Runtime | Object identity and lifetime, dependency properties, element ownership, layout, visual realization, input routing, focus, scheduling, and shutdown. |
| Application host | Application and window entry points, HWND integration, UI-thread ownership, startup, and teardown. |
| WinRT surface | WinMD, implementation DLLs, activation factories, runtime-class resolution, subclassing and overrides, and the supported ABI. |
| XAML toolchain | Compiler integration, generated code, XBF loading, type resolution, resources, styles, templates, and the bindings used by the reference application. |
| Language integration | C++/WinRT and C#/WinRT projections, including managed/native lifetime and reference tracking when managed objects participate. |
| Controls | Concrete controls and their default resources, implemented under `src/controls` as the reference application requires them. |
| Build and distribution | NuGet contents, MSBuild imports, metadata and projection generation, PRI generation, native dependencies, and deployment registration. |

Reuse existing XAML tools where their generated output and binary formats can be supported. Determine those contracts before deciding to rewrite a compiler. Do not make compiler reimplementation a prerequisite for the first native application.

## Platform and Windows App SDK integration

Use WUC (`Windows.UI.Composition`) as the intended rendering foundation and lifted input (`Microsoft.UI.Input`) for platform input delivery. The runtime translates settled element state into visuals and manages its drawing resources; it does not implement a replacement compositor or GPU driver stack.

### Input boundary

Lifted input is the selected input foundation. Use its pointer and keyboard sources through a hosted `Microsoft.UI.Content.ContentIsland`. The candidate WUC integration is `ContentIsland.CreateForSystemVisual`, followed by `InputPointerSource.GetForIsland` and `InputKeyboardSource.GetForIsland`. Establish availability and any feature gates in the pinned Windows App SDK version, HWND hosting, thread ownership, coordinate mapping, and teardown before treating this path as implemented.

The runtime owns element hit testing, routed events, element-level pointer capture, logical focus, and focus navigation. The platform provides input delivery and host-level facilities. Coordinate platform capture and focus with the runtime's element state. IME and text editing require a separate integration contract; keyboard input delivery alone does not establish text input support.

Retain the Windows App SDK IXP and Content dependencies required by this path. WUC rendering does not imply an IXP-free deployment. `CreateForSystemVisual` accepts a `Microsoft.UI.Dispatching.DispatcherQueue`; keep that platform queue while designing any custom runtime task queue separately. Also satisfy WUC's system DispatcherQueue requirements. A custom scheduler does not by itself replace either platform's queue or registration.

### Component integration

Replace the WinUI component while reusing the required Windows App SDK components. Foundation, MRT, App Lifecycle, IXP, and other components are dependencies to select and integrate according to actual use; rebuilding the entire Windows App SDK is not required by this project goal.

The integration work includes:

- Selecting the replacement WinUI package and compatible versions of its dependencies.
- Adjusting package aggregation or MSBuild imports where they otherwise select the official WinUI implementation.
- Supplying the required runtime initialization, activation registration, metadata, and resources.
- Ensuring native DLL resolution and the package dependency graph select one intended XAML implementation.
- Making coexistence with retained Windows App SDK features explicit.

Start with an unpackaged application carrying its required redistributable dependencies locally. This makes the selected implementation and binary versions observable. Later support the reference application's MSIX and, where required, shared framework deployment. Application packaging and self-contained versus framework-dependent runtime deployment are separate choices.

## Milestones and completion criteria

### 1. Packaged native runtime

A small C++ application outside the runtime source tree restores a local NuGet package, builds with normal MSBuild, and runs using the packaged runtime. It must not depend on source project references or manual DLL replacement.

The application opens a real window, constructs a minimal element tree in code, measures and arranges it, draws visible content, receives input, and shuts down normally. A minimal diagnostic drawable is sufficient; Button, XAML markup, C#, and a complete control catalog are not prerequisites.

This milestone proves package consumption and the runtime-to-platform path. It does not establish public WinRT ABI or existing-application compatibility.

### 2. Public WinRT contracts

Publish the supported metadata and implementation contracts. An external consumer can activate supported types, use the required interfaces, and exercise supported subclassing, overrides, and lifetime behavior. Establish the ABI baseline and the Composition/dispatching compatibility strategy.

### 3. XAML and C# application support

A minimal external C# application builds its XAML and generated code against the packages and runs with the new implementation. Include the required resources, bindings such as `x:Bind`, managed subclassing, and managed/native lifetime behavior. Record unsupported language and markup features explicitly.

### 4. Required controls

Implement the controls, templates, styles, and interactions used by the chosen reference application. Keep concrete controls separate from runtime primitives. Do not use a complete control catalog as a prerequisite for earlier milestones.

### 5. Existing-application migration

Build and run the reference application using the replacement packages and the documented migration changes. Establish its required visual and interaction behavior, and compatibility with the selected existing compiled libraries. A successful rebuild alone does not establish ABI or behavioral compatibility.

### 6. Deployment

Produce and install the reference application's intended distribution artifact on a machine without the development checkout. Declare all prerequisites and establish that the application loads the replacement implementation, finds its resources, and starts and exits correctly.

## Completion evidence

For each milestone, record the package version, dependency versions, application revision, Windows version, architecture, build and deployment configuration, and the behavior actually established. Distinguish implemented features from proposals and static review from executed validation.

Unit-level runtime results are supporting evidence. Final completion requires an external application to build, run, and deploy through the delivered packages.

## Related documents

- [Existing and proposed layers](architecture/layer-overview.md)
- [Framework foundations](architecture/framework-foundations.md)
- [Repository structure](repo-structure.md)
- [Core/DXaml rearchitecture](architecture/core-dxaml-rearchitecture.md)
- [Primitive WinUI API compatibility](architecture/primitives-api-compatibility.md)
- [Reference tracking rearchitecture](architecture/reference-tracking-rearchitecture.md)
- [Rendering rearchitecture](architecture/rendering-rearchitecture.md)

Subsystem design documents describe their own proposals and implementation status. The milestones above define the product goal; they do not imply that those proposals have already been adopted or completed.

## Upstream references

- [Using locally built WinUI binaries and component packages](https://github.com/microsoft/microsoft-ui-xaml/blob/main/GettingStarted.md#using-the-built-winui-binaries-in-your-own-project)
- [Windows App SDK deployment overview](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/deploy-overview)
- [ContentIsland.CreateForSystemVisual](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.content.contentisland.createforsystemvisual?view=windows-app-sdk-2.0)
- [InputPointerSource.GetForIsland](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.input.inputpointersource.getforisland?view=windows-app-sdk-2.0)
- [InputKeyboardSource.GetForIsland](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.input.inputkeyboardsource.getforisland?view=windows-app-sdk-2.0)
