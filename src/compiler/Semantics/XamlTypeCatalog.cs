using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;

namespace Microsoft.UI.Xaml.Compiler.Semantics;

public sealed class XamlTypeCatalog
{
    private readonly Dictionary<string, XamlTypeDescription> _types = new(StringComparer.Ordinal);

    public IReadOnlyCollection<XamlTypeDescription> Types => _types.Values;

    public void Add(XamlTypeDescription type) => _types[type.FullName] = type;

    public XamlTypeDescription? Find(string fullName) =>
        _types.GetValueOrDefault(fullName);

    public bool HasMember(string typeName, string memberName)
    {
        var visited = new HashSet<string>(StringComparer.Ordinal);
        while (visited.Add(typeName) && Find(typeName) is { } type)
        {
            if (type.Members.Contains(memberName))
            {
                return true;
            }

            typeName = type.BaseType ?? string.Empty;
        }

        return false;
    }

    public void AddMetadataFile(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        using var stream = File.OpenRead(path);
        using var portable = new PEReader(stream);
        var reader = portable.GetMetadataReader();
        foreach (var handle in reader.TypeDefinitions)
        {
            var definition = reader.GetTypeDefinition(handle);
            var fullName = FullName(reader.GetString(definition.Namespace), reader.GetString(definition.Name));
            if (fullName.StartsWith('<') || fullName == "<Module>")
            {
                continue;
            }

            var members = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in definition.GetProperties())
            {
                members.Add(reader.GetString(reader.GetPropertyDefinition(property).Name));
            }

            foreach (var evt in definition.GetEvents())
            {
                members.Add(reader.GetString(reader.GetEventDefinition(evt).Name));
            }

            foreach (var method in definition.GetMethods())
            {
                var methodName = reader.GetString(reader.GetMethodDefinition(method).Name);
                if (methodName.StartsWith("get_", StringComparison.Ordinal) ||
                    methodName.StartsWith("set_", StringComparison.Ordinal) ||
                    methodName.StartsWith("add_", StringComparison.Ordinal))
                {
                    members.Add(methodName[4..]);
                }
                else if (!methodName.StartsWith('.', StringComparison.Ordinal))
                {
                    members.Add(methodName);
                }
            }

            foreach (var field in definition.GetFields())
            {
                members.Add(reader.GetString(reader.GetFieldDefinition(field).Name));
            }

            var baseType = ResolveName(reader, definition.BaseType);
            Add(new XamlTypeDescription(fullName, baseType, members));
        }
    }

    private static string? ResolveName(MetadataReader reader, System.Reflection.Metadata.EntityHandle handle) =>
        handle.Kind switch
        {
            HandleKind.TypeReference => GetReferenceName(reader, (TypeReferenceHandle)handle),
            HandleKind.TypeDefinition => GetDefinitionName(reader, (TypeDefinitionHandle)handle),
            _ => null,
        };

    private static string GetReferenceName(MetadataReader reader, TypeReferenceHandle handle)
    {
        var type = reader.GetTypeReference(handle);
        return FullName(reader.GetString(type.Namespace), reader.GetString(type.Name));
    }

    private static string GetDefinitionName(MetadataReader reader, TypeDefinitionHandle handle)
    {
        var type = reader.GetTypeDefinition(handle);
        return FullName(reader.GetString(type.Namespace), reader.GetString(type.Name));
    }

    private static string FullName(string ns, string name) =>
        ns.Length == 0 ? name : $"{ns}.{name}";
}

public sealed record XamlTypeDescription(string FullName, string? BaseType, IReadOnlySet<string> Members);
