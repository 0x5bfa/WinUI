# Existing and proposed layers

The proposed architecture follows [Project goals](../project-goals.md): one runtime object model, WUC composition, and lifted input. These diagrams show responsibilities and platform dependencies, not DLLs, source directories, inheritance trees, or complete call graphs.

Solid arrows show use or data flow. Dotted arrows show supporting dependencies or a connection still to establish. The existing diagram describes the lifted-compositor path; it does not claim that every WinUI configuration uses that engine.

## Existing WinUI: Core/DXaml and lifted composition

```mermaid
flowchart TB
    App["Application"]

    subgraph Framework["Framework layer: existing WinUI"]
        Controls["MUX controls, such as NavigationView"]
        DXaml["DXaml objects<br/>Public WinRT ABI and framework behavior"]
        Core["Core objects<br/>Properties, element tree, layout and input"]
        Render["HW render walk<br/>Visual realization and drawing resources"]
        Controls --> DXaml
        DXaml <-->|"Peer lookup and bridges"| Core
        Core --> Render
    end

    subgraph Visual["Visual layer: Windows App SDK"]
        MUC["Microsoft.UI.Composition<br/>Lifted composition engine"]
    end

    Input["Lifted input and hosting services<br/>Windows App SDK"]
    UDK["FrameworkUdk<br/>Internal platform integration"]
    Graphics["Graphics and presentation<br/>DirectX, Windows and GPU driver"]

    App --> DXaml
    App --> Controls
    Render --> MUC
    MUC --> Graphics
    Input -->|"Input notifications"| Core
    UDK -.-> Framework
```

The Core/DXaml split is inside the framework layer. For example, the public-facing `UIElement` and internal `CUIElement` are peers; this is distinct from the Composition Visual used to display the element. Controls such as Button have implementation responsibilities spread across both sides. Newer MUX controls also use the underlying framework; they do not bypass it.

## Proposed WinUI: unified runtime, WUC and lifted input

```mermaid
flowchart TB
    App["Application"]

    subgraph Framework["Framework layer: our implementation"]
        Controls["Controls<br/>Button, Panel, NavigationView and others"]
        Runtime["Unified runtime<br/>WinRT ABI and native operations on the same object<br/>Properties, element tree, layout, routing and focus"]
        Realization["Visual realization bridge<br/>Visual updates, drawing content and resource lifetime"]
        Host["Content-root host<br/>HWND integration and lifecycle"]
        Controls -->|"Inherit and use primitives"| Runtime
        Runtime --> Realization
        Runtime --> Host
    end

    subgraph SDK["Windows App SDK: reused services"]
        Island["Microsoft.UI.Content.ContentIsland"]
        Input["Microsoft.UI.Input<br/>Pointer and keyboard sources"]
        LiftedQueue["Microsoft.UI.Dispatching.DispatcherQueue"]
        Island -->|"Input sources"| Input
        LiftedQueue -.-> Island
    end

    subgraph Visual["Visual layer: Windows"]
        WUC["Windows.UI.Composition<br/>System composition engine"]
    end

    SystemQueue["Windows.System.DispatcherQueue"]
    Graphics["Graphics and presentation<br/>DirectX, Windows and GPU driver"]

    App --> Controls
    App --> Runtime
    Realization -->|"Update display visuals"| WUC
    Host -->|"Host and attach"| Island
    Island -.->|"System Visual connection: runtime validation pending"| WUC
    Input -->|"Input notifications"| Runtime
    SystemQueue -.-> WUC
    WUC --> Graphics
```

The WUC/lifted-Content connection is implemented through `ContentIsland.CreateForSystemVisual` in the native backend. Runtime availability, feature gates, HWND hosting, coordinate mapping, and lifecycle still need execution evidence. See [Framework foundations](framework-foundations.md); successful compilation alone does not validate this connection.

The runtime's custom scheduler owns framework work ordering and invalidation coalescing. It does not automatically replace the platform queues shown above. IME, text editing, accessibility, and optional Windows App SDK features need their own integration contracts and are omitted from these overview diagrams.

## What changes

| Concern | Existing lifted-compositor path | Proposed architecture |
| --- | --- | --- |
| Framework objects | Core and DXaml peer objects, with state and lifetime responsibilities crossing the boundary. | One authoritative runtime implementation per framework object, including its WinRT interfaces. |
| Concrete controls | Older control responsibilities cross Core/DXaml; MUX controls build on the framework. | Concrete controls belong under `src/controls`; runtime primitives remain shared foundations. |
| Display realization | HW render walk and Composition realization infrastructure. | A focused bridge from settled runtime state to WUC visuals and drawing resources. |
| Composition | Lifted MUC engine in the path shown here. | Windows-provided WUC engine. |
| Input | Lifted platform input feeds existing framework input processing. | Lifted platform input feeds the unified runtime's element routing and focus policy. |
| Platform integration | Includes existing FrameworkUdk-dependent features. | Retain only required dependencies; decide FrameworkUdk use per feature. |

## Two trees still have different jobs

- The **element tree** owns framework semantics: parentage, properties, layout, event routes, and logical focus.
- The **Composition Visual tree** owns display state: transforms, clips, opacity, drawing content, and composition animations.

Removing Core/DXaml peers does not remove the Visual tree. A visual realization is a display resource, not another framework object with duplicated dependency-property storage or lifetime semantics. Do not require one Visual per UIElement; choose realization granularity according to rendering needs.

Input, ContentIsland, queues, and the HWND host support the layers from the side. They are not additional layers between Framework, Visual, and Graphics. Direct2D and DirectWrite may also be called by our drawing-content code; the graphics box does not imply that all graphics work passes only through the compositor.

## References

- [Core/DXaml and peer objects in the upstream design notes](/D:/source/mux-main/docs/design-notes/dxamlvscore.md)
- [WUC hosting in a Win32 application](https://learn.microsoft.com/en-us/windows/uwp/composition/using-the-visual-layer-with-win32)
- [ContentIsland.CreateForSystemVisual](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.content.contentisland.createforsystemvisual?view=windows-app-sdk-2.0)
