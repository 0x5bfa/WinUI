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
        var pending = new Stack<string>();
        pending.Push(typeName);
        while (pending.Count > 0)
        {
            typeName = pending.Pop();
            if (!visited.Add(typeName) || Find(typeName) is not { } type)
                continue;

            if (type.Members.Contains(memberName) ||
                type.Members.Contains("Get" + memberName) ||
                type.Members.Contains("Set" + memberName))
            {
                return true;
            }

            if (type.BaseType is not null) pending.Push(type.BaseType);
            foreach (var iface in type.Interfaces ?? []) pending.Push(iface);
        }

        return false;
    }

    public bool HasEvent(string typeName, string eventName)
    {
        var pending = new Stack<string>();
        var visited = new HashSet<string>(StringComparer.Ordinal);
        pending.Push(typeName);
        while (pending.Count > 0)
        {
            var name = pending.Pop();
            if (!visited.Add(name) || Find(name) is not { } type) continue;
            if (type.Events?.Contains(eventName) == true) return true;
            if (type.BaseType is not null) pending.Push(type.BaseType);
            foreach (var iface in type.Interfaces ?? []) pending.Push(iface);
        }

        return false;
    }

    public string? ResolvePresentationType(string localName)
    {
        // XAML's presentation namespace spans multiple WinRT namespaces.
        // Prefer commonly declared visual types, and only use a unique fallback.
        string[] namespaces = ["Microsoft.UI.Xaml.Controls", "Microsoft.UI.Xaml.Controls.Primitives",
            "Microsoft.UI.Xaml", "Microsoft.UI.Xaml.Shapes", "Microsoft.UI.Xaml.Media",
            "Microsoft.UI.Xaml.Media.Animation", "Microsoft.UI.Xaml.Documents",
            "Microsoft.UI.Xaml.Data", "Microsoft.UI.Xaml.Input"];
        foreach (var ns in namespaces)
            if (_types.ContainsKey(ns + "." + localName)) return ns + "." + localName;

        const string prefix = "Microsoft.UI.Xaml.";
        var matches = _types.Keys.Where(name => name.StartsWith(prefix, StringComparison.Ordinal) &&
            name.EndsWith("." + localName, StringComparison.Ordinal)).Take(2).ToArray();
        return matches.Length == 1 ? matches[0] : null;
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
            var events = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in definition.GetProperties())
            {
                members.Add(reader.GetString(reader.GetPropertyDefinition(property).Name));
            }

            foreach (var evt in definition.GetEvents())
            {
                var eventName = reader.GetString(reader.GetEventDefinition(evt).Name);
                members.Add(eventName);
                events.Add(eventName);
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
                else if (!methodName.StartsWith('.'))
                {
                    members.Add(methodName);
                }
            }

            foreach (var field in definition.GetFields())
            {
                members.Add(reader.GetString(reader.GetFieldDefinition(field).Name));
            }

            var baseType = ResolveName(reader, definition.BaseType);
            var interfaces = definition.GetInterfaceImplementations()
                .Select(i => ResolveName(reader, reader.GetInterfaceImplementation(i).Interface))
                .OfType<string>().ToArray();
            Add(new XamlTypeDescription(fullName, baseType, members, interfaces, events));
        }
    }

    private static string? ResolveName(MetadataReader reader, System.Reflection.Metadata.EntityHandle handle)
    {
        if (handle.IsNil) return null;
        return handle.Kind switch
        {
            HandleKind.TypeReference => GetReferenceName(reader, (TypeReferenceHandle)handle),
            HandleKind.TypeDefinition => GetDefinitionName(reader, (TypeDefinitionHandle)handle),
            _ => null,
        };
    }

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

public sealed record XamlTypeDescription(string FullName, string? BaseType, IReadOnlySet<string> Members,
    IReadOnlyList<string>? Interfaces = null, IReadOnlySet<string>? Events = null);
