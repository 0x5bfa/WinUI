using System.Collections.Immutable;
using System.Text.RegularExpressions;
using Microsoft.UI.Xaml.Compiler.Syntax;

namespace Microsoft.UI.Xaml.Compiler.Semantics;

public static partial class XamlSemanticBinder
{
    private const string XamlNamespace = "http://schemas.microsoft.com/winfx/2006/xaml";
    private const string XmlNamespace = "http://www.w3.org/XML/1998/namespace";
    private const string PresentationNamespace = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";

    [GeneratedRegex(@"^[A-Za-z_][A-Za-z_0-9]*(\.[A-Za-z_][A-Za-z_0-9]*)*$", RegexOptions.CultureInvariant)]
    private static partial Regex IdentifierPath();

    public static BoundMarkup Bind(XamlSyntaxDocument document, XamlTypeCatalog catalog)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(catalog);
        var context = new Context(catalog);
        var rootClass = document.Root.Attributes.FirstOrDefault(a => a.Name == new XamlName(XamlNamespace, "Class"));
        if (rootClass is not null && !IdentifierPath().IsMatch(rootClass.Value))
        {
            context.Report("XMC1001", "x:Class must be a qualified identifier.", rootClass.Location);
        }

        var root = context.BindElement(document.Root, root: true);
        return new(rootClass?.Value, root, [.. context.Diagnostics]);
    }

    private sealed class Context(XamlTypeCatalog catalog)
    {
        public List<XamlDiagnostic> Diagnostics { get; } = [];
        private readonly HashSet<string> _names = new(StringComparer.Ordinal);
        private int _nextConnectionId;

        public void Report(string code, string message, XamlSourceLocation location) =>
            Diagnostics.Add(new(code, message, location));

        public BoundElement BindElement(XamlElementSyntax element, bool root = false)
        {
            // Connection IDs follow document order, including property-element values.
            var connectionId = ++_nextConnectionId;
            var typeName = ResolveTypeName(element.Name);
            if (catalog.Find(typeName) is null)
            {
                Report("XMC1002", $"Unknown XAML type '{typeName}'.", element.Location);
            }

            string? name = null;
            var members = new List<BoundMember>();
            foreach (var attribute in element.Attributes)
            {
                if (attribute.Name.NamespaceUri == XmlNamespace) continue;
                if (attribute.Name.NamespaceUri == XamlNamespace)
                {
                    if (attribute.Name.LocalName is "Name")
                    {
                        name = attribute.Value;
                        if (!IdentifierPath().IsMatch(name) || name.Contains('.', StringComparison.Ordinal))
                        {
                            Report("XMC1003", "x:Name must be a simple identifier.", attribute.Location);
                        }
                        else if (!_names.Add(name))
                        {
                            Report("XMC1004", $"Duplicate x:Name '{name}'.", attribute.Location);
                        }
                    }
                    else if (attribute.Name.LocalName is "Class" && !root)
                    {
                        Report("XMC1005", "x:Class is valid only on the root element.", attribute.Location);
                    }
                    else if (attribute.Name.LocalName is not ("Class" or "Key" or "Uid" or "FieldModifier" or "DataType" or "DeferLoadStrategy" or "Load" or "Phase" or "DefaultBindMode"))
                    {
                        Report("XMC1011", $"Unsupported XAML directive '{attribute.Name.LocalName}'.", attribute.Location);
                    }

                    continue;
                }

                BindMember(typeName, element.Name.NamespaceUri, attribute.Name, attribute.Value,
                    attribute.Location, members);
            }

            var children = new List<BoundElement>();
            var properties = new List<BoundPropertyElement>();
            foreach (var child in element.Children.OfType<XamlElementSyntax>())
            {
                if (child.Name.LocalName.Contains('.', StringComparison.Ordinal))
                {
                    var dot = child.Name.LocalName.LastIndexOf('.');
                    var owner = ResolveTypeName(new(child.Name.NamespaceUri, child.Name.LocalName[..dot]));
                    var member = child.Name.LocalName[(dot + 1)..];
                    ValidateMember(owner, member, child.Location);
                    var values = child.Children.OfType<XamlElementSyntax>().Select(value => BindElement(value)).ToImmutableArray();
                    var propertyText = string.Concat(child.Children.OfType<XamlTextSyntax>().Select(t => t.Value));
                    properties.Add(new(owner, member, values,
                        string.IsNullOrWhiteSpace(propertyText) ? null : propertyText, child.Location));
                }
                else
                {
                    children.Add(BindElement(child));
                }
            }

            return new(typeName, name, connectionId, [.. members], [.. children], [.. properties], element.Location);
        }

        private void BindMember(string elementType, string elementNamespace, XamlName name, string value,
            XamlSourceLocation location,
            List<BoundMember> members)
        {
            var dot = name.LocalName.LastIndexOf('.');
            var owner = dot >= 0
                ? ResolveTypeName(new(name.NamespaceUri.Length == 0 ? elementNamespace : name.NamespaceUri,
                    name.LocalName[..dot]))
                : elementType;
            var member = dot >= 0 ? name.LocalName[(dot + 1)..] : name.LocalName;
            ValidateMember(owner, member, location);
            if (catalog.HasEvent(owner, member) && !IdentifierPath().IsMatch(value))
                Report("XMC1012", $"Event handler '{value}' must be a qualified identifier.", location);

            BoundBinding? binding = null;
            if (value.StartsWith("{x:Bind", StringComparison.Ordinal) && value.EndsWith('}'))
            {
                binding = ParseBinding(value, location);
            }

            members.Add(new(owner, member, value, binding, location));
        }

        private void ValidateMember(string owner, string member, XamlSourceLocation location)
        {
            if (catalog.Find(owner) is null) return;
            if (!catalog.HasMember(owner, member))
            {
                Report("XMC1006", $"'{owner}' does not define '{member}'.", location);
            }
        }

        private BoundBinding? ParseBinding(string value, XamlSourceLocation location)
        {
            var content = value[7..^1].Trim();
            var parts = content.Split(',', StringSplitOptions.TrimEntries);
            string? path = null;
            var mode = XamlBindingMode.OneTime;
            foreach (var part in parts)
            {
                if (part.Length == 0) continue;
                var equals = part.IndexOf('=');
                if (equals < 0 && path is null) path = part;
                else if (equals >= 0)
                {
                    var key = part[..equals].Trim();
                    var argument = part[(equals + 1)..].Trim();
                    if (key == "Path") path = argument;
                    else if (key == "Mode" && !Enum.TryParse(argument, ignoreCase: false, out mode))
                    {
                        Report("XMC1007", $"Unsupported x:Bind mode '{argument}'.", location);
                    }
                    else if (key != "Mode")
                    {
                        Report("XMC1008", $"Unsupported x:Bind argument '{key}'.", location);
                    }
                }
                else Report("XMC1009", $"Unexpected x:Bind argument '{part}'.", location);
            }

            if (path is null || !IdentifierPath().IsMatch(path))
            {
                Report("XMC1010", "x:Bind requires a simple property path.", location);
                return null;
            }

            return new(path, mode, location);
        }

        private string ResolveTypeName(XamlName name)
        {
            if (name.NamespaceUri.StartsWith("using:", StringComparison.Ordinal))
                return $"{name.NamespaceUri[6..]}.{name.LocalName}";
            if (name.NamespaceUri.StartsWith("clr-namespace:", StringComparison.Ordinal))
                return $"{name.NamespaceUri[14..].Split(';')[0]}.{name.LocalName}";
            if (name.NamespaceUri == PresentationNamespace)
                return catalog.ResolvePresentationType(name.LocalName) ?? $"Microsoft.UI.Xaml.{name.LocalName}";
            return $"{name.NamespaceUri}.{name.LocalName}";
        }
    }
}
