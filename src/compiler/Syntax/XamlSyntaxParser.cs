using System.Xml;
using System.Xml.Linq;

namespace Microsoft.UI.Xaml.Compiler.Syntax;

public static class XamlSyntaxParser
{
    public static XamlParseResult Parse(string source, string filePath)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(filePath);

        using var input = new StringReader(source);
        using var reader = XmlReader.Create(input, CreateSettings());
        return ParseCore(reader, filePath);
    }

    public static XamlParseResult ParseFile(string filePath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(filePath);

        using var input = File.OpenRead(filePath);
        using var reader = XmlReader.Create(input, CreateSettings());
        return ParseCore(reader, filePath);
    }

    private static XmlReaderSettings CreateSettings() => new()
    {
        DtdProcessing = DtdProcessing.Prohibit,
        XmlResolver = null,
        IgnoreComments = true,
        IgnoreProcessingInstructions = true,
    };

    private static XamlParseResult ParseCore(XmlReader reader, string filePath)
    {
        try
        {
            var document = XDocument.Load(reader, LoadOptions.SetLineInfo | LoadOptions.PreserveWhitespace);
            if (document.Root is null)
            {
                return new(null, [new("XMC0001", "XAML has no root element.", new(filePath, 0, 0))]);
            }

            return new(new(filePath, ReadElement(document.Root, filePath)), []);
        }
        catch (XmlException exception)
        {
            return new(null, [new("XMC0001", exception.Message,
                new(filePath, exception.LineNumber, exception.LinePosition))]);
        }
    }

    private static XamlElementSyntax ReadElement(XElement element, string filePath)
    {
        var attributes = new List<XamlAttributeSyntax>();
        var namespaces = new List<XamlNamespaceDeclarationSyntax>();

        foreach (var attribute in element.Attributes())
        {
            var location = GetLocation(attribute, filePath);
            if (attribute.IsNamespaceDeclaration)
            {
                var prefix = attribute.Name.LocalName == "xmlns" ? "" : attribute.Name.LocalName;
                namespaces.Add(new(prefix, attribute.Value, location));
            }
            else
            {
                attributes.Add(new(ToName(attribute.Name), attribute.Value, location));
            }
        }

        var children = new List<XamlNodeSyntax>();
        foreach (var child in element.Nodes())
        {
            switch (child)
            {
                case XElement childElement:
                    children.Add(ReadElement(childElement, filePath));
                    break;
                case XCData cdata:
                    children.Add(new XamlTextSyntax(cdata.Value, true, GetLocation(cdata, filePath)));
                    break;
                case XText text:
                    children.Add(new XamlTextSyntax(text.Value, false, GetLocation(text, filePath)));
                    break;
            }
        }

        return new(ToName(element.Name), [.. attributes], [.. namespaces], [.. children], GetLocation(element, filePath));
    }

    private static XamlName ToName(XName name) => new(name.NamespaceName, name.LocalName);

    private static XamlSourceLocation GetLocation(IXmlLineInfo info, string filePath) =>
        new(filePath, info.HasLineInfo() ? info.LineNumber : 0, info.HasLineInfo() ? info.LinePosition : 0);
}
