# Runtime unit tests

From a Visual Studio 2026 developer shell:

```powershell
msbuild Microsoft.UI.Xaml.slnx /restore /m /p:Configuration=Debug /p:Platform=x64
msbuild tests/unit/Microsoft.UI.Xaml.Runtime.Tests.vcxproj /t:Test /p:Configuration=Debug /p:Platform=x64
```

Repeat with `Configuration=Release` for the release build.

The initial tests cover native/WinRT identity and shared state, implementation inheritance, strong/weak references, thread access, ABI guards, and resource-neutral final release. They do not establish managed reference tracking, external WinRT composition, or UI-affine destruction.
