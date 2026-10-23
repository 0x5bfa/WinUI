# Repository Guidelines

## Architecture

- Use Visual Studio 2026, MSVC, and MSBuild. The root solution is `Microsoft.UI.Xaml.slnx`.
- Use `/std:c++latest` and C++ Modules for new C++ code.
- Align modules and projects with stable subsystem boundaries. Avoid one module or project per class; modules are not DLL boundaries.
- Keep one unified runtime object model. Do not reintroduce the legacy Core/DXaml peer model.
- Keep generated files in build output. Edit their source definitions instead.

## Language and Documentation

- Write repository documentation, code comments, and commit messages in English.
- Keep documentation simple and concise. Avoid repeating implementation details.
- Add ADRs only for major architectural decisions with long-term impact. Do not use ADRs for routine implementation choices.
