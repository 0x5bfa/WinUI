# NuGet-only native consumer

Copy this folder outside the repository after packing `eng/packaging/Unified.WinUI.Native.csproj`. Add the resulting local feed during restore:

```powershell
msbuild PackagedDesktop.vcxproj /restore /m /p:Configuration=Debug /p:Platform=x64 /p:RestoreAdditionalProjectSources=D:\source\microsoft-ui-xaml\artifacts\packages
```

This application uses only package references and its local Main.cpp. It exercises the same native controls as NativeDesktop; no repository ProjectReference, generated headers, or IFC files are reused. The framework-dependent Windows App SDK runtime must be installed. Build success does not establish runtime or ABI compatibility.
