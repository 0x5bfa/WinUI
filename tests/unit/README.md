# Runtime unit tests

From a Visual Studio 2026 developer shell:

```powershell
msbuild Microsoft.UI.Xaml.slnx /restore /m /p:Configuration=Debug /p:Platform=x64
msbuild tests/unit/Microsoft.UI.Xaml.Runtime.Tests.vcxproj /t:Test /p:Configuration=Debug /p:Platform=x64
```

Repeat with `Configuration=Release` for the release build.

The tests cover native/WinRT identity, strong/weak references, thread access, ABI guards, value properties (including Control padding), ordered multi-child tree ownership, StackPanel layout, layout invalidation, and Button input/Click state. Reentrancy cases include changing the tree during notification or layout and releasing the sender's last external owner inside a callback. Tests import both `Xaml.Runtime` and `Xaml.Controls` and link both native static libraries.

They do not establish public WinUI ABI compatibility, managed reference tracking, external WinRT composition, UI-affine destruction, actual rendering, or OS input routing.
