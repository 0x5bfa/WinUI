# Native desktop sample

A small unpackaged C++ consumer of the native Framework layer. It uses WUC and lifted input through `DesktopWindowHost`, without XAML or concrete controls.

Build from the repository root with Visual Studio 2026 MSBuild:

```powershell
msbuild samples/NativeDesktop/NativeDesktop.vcxproj /restore /m /p:Configuration=Debug /p:Platform=x64
```

The executable is `artifacts/bin/x64/Debug/NativeDesktop.exe`. Use `Configuration=Release` for the Release output. The framework-dependent sample requires the matching Windows App SDK 2.5 runtime; its Foundation, IXP, and Runtime NuGet components are explicitly pinned in the project. Availability of the system-visual Content APIs and any runtime feature gates must be established on the target machine.

When running the sample, click and release the blue rectangle to change its opacity while pressed. After focusing it by clicking, Space toggles its opacity and Escape closes the window. The window forwards client resize and per-monitor DPI changes to the host.

This is an integration application, not evidence of completed runtime validation. It uses a ProjectReference rather than the final NuGet-only consumption path. The blue rectangle is diagnostic rendering content; brushes, text, images, control templates, IME, and full keyboard navigation remain pending.
