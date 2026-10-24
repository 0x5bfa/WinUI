# Native desktop sample

A small unpackaged C++ consumer of the native Framework and Controls layers. It uses WUC and lifted input through `DesktopWindowHost`, without XAML or the official WinUI implementation.

Build from the repository root with Visual Studio 2026 MSBuild:

```powershell
msbuild samples/NativeDesktop/NativeDesktop.vcxproj /restore /m /p:Configuration=Debug /p:Platform=x64
```

The executable is `artifacts/bin/x64/Debug/NativeDesktop.exe`. Use `Configuration=Release` for the Release output. The framework-dependent sample requires the matching Windows App SDK 2.5 runtime; its Foundation, IXP, and Runtime NuGet components are explicitly pinned in the project. Availability of the system-visual Content APIs and any runtime feature gates must be established on the target machine.

The application displays two templated Buttons and a text status line. Click, Space, or Enter activates a button; Tab and Shift+Tab move focus; Escape closes the window. The first button updates the status text through a weak callback. The window forwards client resize and per-monitor DPI changes to the host, and the message loop calls `PreTranslateMessage` before dispatch for TSF keystroke handling.

This is an integration application, not evidence of completed runtime validation. It references Controls and Runtime projects. [PackagedDesktop](../PackagedDesktop/README.md) provides a separate NuGet-only native consumption path. Public WinRT ABI, managed controls, automation, rich text editing, images, and full keyboard/host navigation remain pending. The sample does not contain a text editor and therefore does not exercise IME composition.
