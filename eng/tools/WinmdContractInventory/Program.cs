using System.Collections.Immutable;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;

if (args.Length < 2 || !File.Exists(args[0]))
{
    Console.Error.WriteLine("Usage: WinmdContractInventory <Microsoft.UI.Xaml.winmd> [--snapshot <path> | --verify <path>] <TypeName> [TypeName...]");
    return 1;
}

var manifestMode = args.Length > 2 && args[1] is "--snapshot" or "--verify";
if (manifestMode && args.Length < 4)
{
    Console.Error.WriteLine("Snapshot and verification require a file path and at least one type.");
    return 1;
}
var manifestPath = manifestMode ? args[2] : null;
var typeArguments = args.Skip(manifestMode ? 3 : 1).ToArray();
var savedOutput = Console.Out;
using var buffer = new StringWriter(System.Globalization.CultureInfo.InvariantCulture);
if (manifestMode) { Console.SetOut(buffer); }

using var stream = File.OpenRead(args[0]);
using var portable = new PEReader(stream);
var metadata = portable.GetMetadataReader();
var provider = new SignatureTextProvider();
var requested = typeArguments.ToHashSet(StringComparer.Ordinal);
var found = new HashSet<string>(StringComparer.Ordinal);

Console.WriteLine($"WinMD: {(manifestMode ? "Microsoft.UI.Xaml.winmd" : Path.GetFullPath(args[0]))}");
Console.WriteLine($"Assembly: {metadata.GetString(metadata.GetAssemblyDefinition().Name)}");
Console.WriteLine($"Assembly version: {metadata.GetAssemblyDefinition().Version}");

foreach (var handle in metadata.TypeDefinitions)
{
    var type = metadata.GetTypeDefinition(handle);
    var typeName = metadata.GetString(type.Name);
    var namespaceName = metadata.GetString(type.Namespace);
    var fullName = string.IsNullOrEmpty(namespaceName) ? typeName : $"{namespaceName}.{typeName}";
    // Runtime classes expose many of their members on versioned ABI interfaces.
    // Include all matching versioned interfaces, factories, and statics.
    var matchingName = requested.Any(name =>
    {
        var separator = name.LastIndexOf('.');
        var requestedNamespace = separator < 0 ? "Microsoft.UI.Xaml" : name[..separator];
        if (namespaceName != requestedNamespace) { return false; }
        if (separator >= 0) { name = name[(separator + 1)..]; }
        if (typeName == name) { return true; }
        if (!typeName.StartsWith($"I{name}", StringComparison.Ordinal)) { return false; }
        var suffix = typeName[(name.Length + 1)..];
        return suffix.Length == 0 || suffix.All(char.IsDigit) ||
            new[] { "Factory", "Statics", "Protected", "Overrides" }.Any(prefix =>
                suffix.StartsWith(prefix, StringComparison.Ordinal) &&
                (suffix.Length == prefix.Length || suffix[prefix.Length..].All(char.IsDigit)));
    });
    if (!matchingName) { continue; }
    found.Add(fullName);
    Console.WriteLine();
    Console.WriteLine($"TYPE {fullName} [{type.Attributes}]");
    PrintAttributes(type.GetCustomAttributes(), "  ATTRIBUTE");

    foreach (var implHandle in type.GetInterfaceImplementations())
    {
        var implemented = metadata.GetInterfaceImplementation(implHandle).Interface;
        Console.WriteLine($"  IMPLEMENTS {provider.Name(metadata, implemented)}");
    }
    foreach (var methodHandle in type.GetMethods())
    {
        var method = metadata.GetMethodDefinition(methodHandle);
        var signature = method.DecodeSignature(provider, null);
        var modifiers = (method.Attributes & MethodAttributes.MemberAccessMask).ToString();
        var parameters = string.Join(", ", signature.ParameterTypes);
        Console.WriteLine($"  METHOD [{modifiers}] {signature.ReturnType} {metadata.GetString(method.Name)}({parameters})");
        PrintAttributes(method.GetCustomAttributes(), $"    METHOD_ATTRIBUTE {metadata.GetString(method.Name)}");
    }
    foreach (var propertyHandle in type.GetProperties())
    {
        var property = metadata.GetPropertyDefinition(propertyHandle);
        var signature = property.DecodeSignature(provider, null);
        Console.WriteLine($"  PROPERTY {signature.ReturnType} {metadata.GetString(property.Name)}");
        PrintAttributes(property.GetCustomAttributes(), $"    PROPERTY_ATTRIBUTE {metadata.GetString(property.Name)}");
    }
    foreach (var eventHandle in type.GetEvents())
    {
        var evt = metadata.GetEventDefinition(eventHandle);
        Console.WriteLine($"  EVENT {provider.Name(metadata, evt.Type)} {metadata.GetString(evt.Name)}");
    }
}

foreach (var name in requested)
{
    if (!found.Contains(name.Contains('.') ? name : $"Microsoft.UI.Xaml.{name}"))
    {
        Console.Error.WriteLine($"Missing runtime class: {name}");
        return 2;
    }
}

if (manifestMode)
{
    Console.SetOut(savedOutput);
    var manifest = buffer.ToString().Replace("\r\n", "\n", StringComparison.Ordinal);
    if (args[1] == "--snapshot")
    {
        File.WriteAllText(manifestPath!, manifest.Replace("\n", "\r\n", StringComparison.Ordinal));
        Console.WriteLine($"Wrote WinMD contract snapshot: {manifestPath}");
    }
    else
    {
        if (!File.Exists(manifestPath))
        {
            Console.Error.WriteLine($"Missing WinMD contract snapshot: {manifestPath}");
            return 3;
        }
        var expected = File.ReadAllText(manifestPath).Replace("\r\n", "\n", StringComparison.Ordinal);
        if (expected != manifest)
        {
            var actualLines = manifest.Split('\n');
            var expectedLines = expected.Split('\n');
            var mismatch = Enumerable.Range(0, Math.Min(actualLines.Length, expectedLines.Length))
                .FirstOrDefault(i => actualLines[i] != expectedLines[i], -1);
            if (mismatch < 0) { mismatch = Math.Min(actualLines.Length, expectedLines.Length); }
            Console.Error.WriteLine($"WinMD contract mismatch at line {mismatch + 1}: expected '{expectedLines.ElementAtOrDefault(mismatch)}', actual '{actualLines.ElementAtOrDefault(mismatch)}'.");
            return 4;
        }
        Console.WriteLine($"WinMD contract verified: {requested.Count} requested types, {found.Count} matching definitions.");
    }
}

return 0;

void PrintAttributes(CustomAttributeHandleCollection attributes, string prefix)
{
    foreach (var attributeHandle in attributes)
    {
        var attribute = metadata.GetCustomAttribute(attributeHandle);
        var ctor = attribute.Constructor;
        var attributeType = ctor.Kind switch
        {
            HandleKind.MemberReference => metadata.GetMemberReference((MemberReferenceHandle)ctor).Parent,
            HandleKind.MethodDefinition => metadata.GetMethodDefinition((MethodDefinitionHandle)ctor).GetDeclaringType(),
            _ => default(EntityHandle)
        };
        if (attributeType.IsNil) { continue; }
        var blob = metadata.GetBlobBytes(attribute.Value);
        Console.WriteLine($"{prefix} {provider.Name(metadata, attributeType)} {Convert.ToHexString(blob)}");
    }
}

sealed class SignatureTextProvider : ISignatureTypeProvider<string, object?>
{
    public string Name(MetadataReader reader, EntityHandle handle) => handle.Kind switch
    {
        HandleKind.TypeDefinition => GetTypeFromDefinition(reader, (TypeDefinitionHandle)handle, 0),
        HandleKind.TypeReference => GetTypeFromReference(reader, (TypeReferenceHandle)handle, 0),
        HandleKind.TypeSpecification => GetTypeFromSpecification(reader, null, (TypeSpecificationHandle)handle, 0),
        _ => $"{handle.Kind}"
    };

    public string GetTypeFromDefinition(MetadataReader reader, TypeDefinitionHandle handle, byte rawTypeKind)
    {
        var type = reader.GetTypeDefinition(handle);
        return Combine(reader.GetString(type.Namespace), reader.GetString(type.Name));
    }

    public string GetTypeFromReference(MetadataReader reader, TypeReferenceHandle handle, byte rawTypeKind)
    {
        var type = reader.GetTypeReference(handle);
        return Combine(reader.GetString(type.Namespace), reader.GetString(type.Name));
    }

    public string GetTypeFromSpecification(MetadataReader reader, object? genericContext,
        TypeSpecificationHandle handle, byte rawTypeKind) =>
        reader.GetTypeSpecification(handle).DecodeSignature(this, genericContext);

    public string GetPrimitiveType(PrimitiveTypeCode typeCode) => typeCode.ToString();
    public string GetArrayType(string elementType, ArrayShape shape) => $"{elementType}[{new string(',', shape.Rank - 1)}]";
    public string GetSZArrayType(string elementType) => $"{elementType}[]";
    public string GetByReferenceType(string elementType) => $"ref {elementType}";
    public string GetPointerType(string elementType) => $"{elementType}*";
    public string GetPinnedType(string elementType) => $"pinned {elementType}";
    public string GetGenericMethodParameter(object? genericContext, int index) => $"!!{index}";
    public string GetGenericTypeParameter(object? genericContext, int index) => $"!{index}";
    public string GetGenericInstantiation(string genericType, ImmutableArray<string> arguments) =>
        $"{genericType}<{string.Join(", ", arguments)}>";
    public string GetModifiedType(string modifier, string unmodifiedType, bool isRequired) =>
        $"{unmodifiedType} {(isRequired ? "modreq" : "modopt")}({modifier})";
    public string GetFunctionPointerType(MethodSignature<string> signature) =>
        $"fnptr({string.Join(", ", signature.ParameterTypes)}) -> {signature.ReturnType}";

    private static string Combine(string ns, string name) => string.IsNullOrEmpty(ns) ? name : $"{ns}.{name}";
}
