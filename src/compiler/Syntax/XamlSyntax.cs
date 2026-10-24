using System.Collections.Immutable;

namespace Microsoft.UI.Xaml.Compiler.Syntax;

public sealed record XamlName(string NamespaceUri, string LocalName);

public sealed record XamlSourceLocation(string FilePath, int Line, int Column);

public sealed record XamlDiagnostic(string Code, string Message, XamlSourceLocation Location);

public sealed record XamlSyntaxDocument(string FilePath, XamlElementSyntax Root);

public sealed record XamlParseResult(XamlSyntaxDocument? Document, ImmutableArray<XamlDiagnostic> Diagnostics)
{
    public bool Success => Document is not null && Diagnostics.IsEmpty;
}

public abstract record XamlNodeSyntax(XamlSourceLocation Location);

public sealed record XamlElementSyntax(
    XamlName Name,
    ImmutableArray<XamlAttributeSyntax> Attributes,
    ImmutableArray<XamlNamespaceDeclarationSyntax> NamespaceDeclarations,
    ImmutableArray<XamlNodeSyntax> Children,
    XamlSourceLocation Location) : XamlNodeSyntax(Location);

public sealed record XamlAttributeSyntax(XamlName Name, string Value, XamlSourceLocation Location);

public sealed record XamlNamespaceDeclarationSyntax(string Prefix, string NamespaceUri, XamlSourceLocation Location);

public sealed record XamlTextSyntax(string Value, bool IsCData, XamlSourceLocation Location) : XamlNodeSyntax(Location);
