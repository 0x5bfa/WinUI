# Unified.WinUI.Native

A native C++ source package for the unified runtime and controls. Consumers compile the module sources with their own MSVC toolset, avoiding toolset-specific prebuilt IFC distribution. This preview does not replace the public Microsoft.UI.Xaml WinRT DLL or provide managed projections.

Requirements: Visual Studio 2026/v145, Windows SDK 10.0.26100.0, x64, C++/WinRT 3.0.260818.1, and IXP 2.1.9. Add those last two as direct PackageReferences so their native MSBuild imports run. Windows App SDK initialization and deployment belong to the host application; the package contains no official WinUI implementation.

```xml
<PackageReference Include="Unified.WinUI.Native" Version="0.1.0-preview.3" />
<PackageReference Include="Microsoft.Windows.CppWinRT" Version="3.0.260818.1" />
<PackageReference Include="Microsoft.WindowsAppSDK.InteractiveExperiences" Version="2.1.9" />
```

Use `import Xaml.Runtime;` and `import Xaml.Controls;`. The package compiles the runtime/control implementations into the consumer, generates platform projections in the consumer's intermediate directory, and links system graphics/TSF libraries. Compile it once per final native module; do not embed separate copies into multiple DLLs that exchange runtime objects or property identities.

Build the package from the repository root:

```powershell
dotnet pack eng/packaging/Unified.WinUI.Native.csproj -c Release
```

`samples/PackagedDesktop` is a NuGet-only consumer. Copy that folder outside the repository and add `artifacts/packages` as a local NuGet source. It has no ProjectReference or source path back into the repository. Its Foundation/Runtime dependencies initialize Windows App SDK and its message loop forwards TSF keystrokes before dispatch. No package is published by this workflow.

Public WinRT activation, composability, reference tracking, C# integration, PRI/XAML deployment, and full behavioral compatibility are subsequent gates. Building this source package establishes only the native C++ consumption path.
