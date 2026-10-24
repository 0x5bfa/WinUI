using System.Collections.Immutable;
using Microsoft.UI.Xaml.Compiler.Syntax;

namespace Microsoft.UI.Xaml.Compiler.Semantics;

public enum XamlBindingMode { OneTime, OneWay, TwoWay }

public sealed record BoundBinding(string Path, XamlBindingMode Mode, XamlSourceLocation Location);

public sealed record BoundMember(string OwnerType, string Name, string Value, BoundBinding? Binding,
    XamlSourceLocation Location);

public sealed record BoundPropertyElement(string OwnerType, string Name, ImmutableArray<BoundElement> Values,
    string? Text, XamlSourceLocation Location);

public sealed record BoundElement(string TypeName, string? Name, int ConnectionId,
    ImmutableArray<BoundMember> Members, ImmutableArray<BoundElement> Children,
    ImmutableArray<BoundPropertyElement> Properties, XamlSourceLocation Location);

public sealed record BoundMarkup(string? ClassName, BoundElement Root, ImmutableArray<XamlDiagnostic> Diagnostics)
{
    public bool Success => Diagnostics.IsEmpty;
}
