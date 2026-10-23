# Repository Structure

This repository contains a new Windows XAML runtime. Public WinRT and XAML compatibility is defined at the boundary; the runtime uses one internal object model.

The layout below is a broad guide, not a fixed directory or project structure. Create only the parts needed by each implementation step, and adjust boundaries as the code develops.

## Layout

```text
/
├─ Microsoft.UI.Xaml.slnx
├─ Directory.Build.props
├─ Directory.Build.targets
├─ eng/
│  ├─ build/
│  └─ toolchain/
├─ docs/
│  ├─ repo-structure.md
│  └─ architecture/
│     ├─ README.md
│     └─ decisions/
├─ src/
│  ├─ contracts/                 # Public API and type/property definitions
│  ├─ tools/
│  │  ├─ XamlGen/                # Generate ABI and runtime metadata
│  │  └─ XamlCompiler/           # Compile XAML markup
│  ├─ platform/
│  │  ├─ contracts/              # Runtime-facing platform interfaces
│  │  └─ windows/                # Windowing, Composition, input, text, UIA
│  ├─ runtime/
│  │  ├─ foundation/
│  │  ├─ object-model/
│  │  ├─ visual-tree/
│  │  ├─ scheduling/
│  │  ├─ layout/
│  │  ├─ resources/
│  │  ├─ input/
│  │  ├─ text/
│  │  ├─ automation/
│  │  └─ rendering/
│  ├─ controls/
│  │  ├─ primitives/
│  │  ├─ panels/
│  │  ├─ text/
│  │  ├─ collections/
│  │  └─ navigation/
│  ├─ markup/
│  │  └─ runtime/                # Load and execute compiled markup
│  └─ product/
│     └─ Microsoft.UI.Xaml.vcxproj
├─ tests/
│  ├─ unit/
│  ├─ compatibility/
│  ├─ integration/
│  └─ performance/
└─ samples/
   ├─ SmokeApp/
   └─ Gallery/
```

## Boundaries

- `contracts/` is the source of truth for public APIs and type metadata. `XamlGen` produces generated files in build output; do not check generated files into the source tree.
- `runtime/` owns the unified object model and runtime services. Organize C++ Modules around stable subsystems, not individual classes. Module boundaries are not DLL boundaries.
- `platform/contracts/` defines services used by the runtime. `platform/windows/` implements them with Windows APIs.
- `controls/` and `markup/runtime/` build on the runtime. `product/` assembles the final `Microsoft.UI.Xaml.dll`.
- Keep project boundaries coarser than source directories. Split projects only when a stable build or deployment boundary requires it.
- The initial object model builds as `src/runtime/Microsoft.UI.Xaml.Runtime.vcxproj`, a static library for the future product and native tests. A directory does not require its own project or module.

The current slice provides `xaml::runtime::Object` in `Xaml.Runtime`: C++/WinRT identity, strong/weak references, and creating-thread access checks. It holds no UI resources. Property storage, managed reference tracking, external composition, session lifetime, and UI-affine reclamation are later slices.

## Build and Documentation

- Use Visual Studio 2026, MSVC, `/std:c++latest`, and MSBuild. Keep `Microsoft.UI.Xaml.slnx` as the root solution and pin the tested toolset in `eng/toolchain/`.
- Keep generated and intermediate files in build output directories.
- Keep documentation concise. Add ADRs under `docs/architecture/decisions/` only for major decisions with long-term architectural impact.
