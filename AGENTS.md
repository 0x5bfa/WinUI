# Repository Guidelines

## Architecture

- Use Visual Studio 2026, MSVC, and MSBuild. The root solution is `Microsoft.UI.Xaml.slnx`.
- Use `/std:c++latest` and C++ Modules for new C++ code.
- Align modules and projects with stable subsystem boundaries. Avoid one module or project per class; modules are not DLL boundaries.
- Keep one unified runtime object model. Do not reintroduce the legacy Core/DXaml peer model.
- Use WUC (`Windows.UI.Composition`) for composition and lifted input (`Microsoft.UI.Input`) for input delivery. Keep element hit testing, event routing, and focus policy in the runtime; retain the platform queues required by their hosting APIs.
- Keep generated files in build output. Edit their source definitions instead.

## Language and Documentation

- Write repository documentation, code comments, and commit messages in English.
- Keep documentation simple and concise. Avoid repeating implementation details.
- Add ADRs only for major architectural decisions with long-term impact. Do not use ADRs for routine implementation choices.

## Goal

- Deliver a unified WinUI implementation as NuGet packages that external applications can reference to build, run, and deploy.
- Enable selected existing WinUI applications to migrate through package-reference changes and documented source changes for retired APIs. Preserve the supported WinRT ABI and required behavior without restoring Core/DXaml peers.
- First deliver a minimal external C++ application using the packaged runtime for window hosting, layout, drawing, input, and shutdown. Add public WinRT contracts, XAML, C#, controls, existing-application migration, and deployment in explicit stages.
- Integrate the replacement WinUI component with the required Windows App SDK components. Rebuilding the entire Windows App SDK is not a prerequisite. See [Project goals](docs/project-goals.md) for scope and completion criteria.
