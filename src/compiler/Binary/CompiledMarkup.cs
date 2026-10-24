using System.Collections.Immutable;
using System.Text;
using Microsoft.UI.Xaml.Compiler.Semantics;
using Microsoft.UI.Xaml.Compiler.Syntax;

namespace Microsoft.UI.Xaml.Compiler.Binary;

/// <summary>
/// An independently versioned, deterministic interchange format for bound markup.
/// This is not the Windows XBF2 wire format and must not be deployed as an .xbf.
/// </summary>
public static class CompiledMarkup
{
    private const uint Magic = 0x4642584D; // MXBF, little endian
    private const ushort Version = 1;
    private const int MaxEntries = 1_000_000;

    public static void Write(BoundMarkup markup, Stream stream)
    {
        ArgumentNullException.ThrowIfNull(markup);
        ArgumentNullException.ThrowIfNull(stream);
        if (!markup.Success)
            throw new ArgumentException("Markup must be successfully bound before serialization.", nameof(markup));
        using var writer = new BinaryWriter(stream, new UTF8Encoding(false, true), leaveOpen: true);
        writer.Write(Magic);
        writer.Write(Version);
        WriteOptional(writer, markup.ClassName);
        WriteElement(writer, markup.Root, depth: 0);
    }

    public static BoundMarkup Read(Stream stream)
    {
        ArgumentNullException.ThrowIfNull(stream);
        using var reader = new BinaryReader(stream, new UTF8Encoding(false, true), leaveOpen: true);
        if (reader.ReadUInt32() != Magic)
            throw new InvalidDataException("Invalid compiled markup signature.");
        if (reader.ReadUInt16() != Version)
            throw new InvalidDataException("Unsupported compiled markup version.");
        var className = ReadOptional(reader);
        var root = ReadElement(reader, depth: 0);
        if (stream.CanSeek && stream.Position != stream.Length)
            throw new InvalidDataException("Trailing data in compiled markup stream.");
        return new(className, root, []);
    }

    private static void WriteElement(BinaryWriter writer, BoundElement element, int depth)
    {
        CheckDepth(depth);
        writer.Write(element.TypeName);
        WriteOptional(writer, element.Name);
        writer.Write(element.ConnectionId);
        WriteLocation(writer, element.Location);
        writer.Write(element.Members.Length);
        foreach (var member in element.Members)
        {
            writer.Write(member.OwnerType);
            writer.Write(member.Name);
            writer.Write(member.Value);
            WriteLocation(writer, member.Location);
            writer.Write(member.Binding is not null);
            if (member.Binding is { } binding)
            {
                writer.Write(binding.Path);
                writer.Write((byte)binding.Mode);
                WriteLocation(writer, binding.Location);
            }
        }
        writer.Write(element.Children.Length);
        foreach (var child in element.Children)
            WriteElement(writer, child, depth + 1);
        writer.Write(element.Properties.Length);
        foreach (var property in element.Properties)
        {
            writer.Write(property.OwnerType);
            writer.Write(property.Name);
            WriteOptional(writer, property.Text);
            WriteLocation(writer, property.Location);
            writer.Write(property.Values.Length);
            foreach (var value in property.Values)
                WriteElement(writer, value, depth + 1);
        }
    }

    private static BoundElement ReadElement(BinaryReader reader, int depth)
    {
        CheckDepth(depth);
        var typeName = reader.ReadString();
        var name = ReadOptional(reader);
        var id = reader.ReadInt32();
        var location = ReadLocation(reader);
        var members = ImmutableArray.CreateBuilder<BoundMember>();
        var memberCount = ReadCount(reader);
        for (var i = 0; i < memberCount; ++i)
        {
            var owner = reader.ReadString();
            var member = reader.ReadString();
            var value = reader.ReadString();
            var memberLocation = ReadLocation(reader);
            BoundBinding? binding = null;
            if (reader.ReadBoolean())
            {
                var path = reader.ReadString();
                var mode = reader.ReadByte();
                if (mode > (byte)XamlBindingMode.TwoWay)
                    throw new InvalidDataException("Invalid binding mode.");
                binding = new(path, (XamlBindingMode)mode, ReadLocation(reader));
            }
            members.Add(new(owner, member, value, binding, memberLocation));
        }
        var children = ImmutableArray.CreateBuilder<BoundElement>();
        var childCount = ReadCount(reader);
        for (var i = 0; i < childCount; ++i)
            children.Add(ReadElement(reader, depth + 1));
        var properties = ImmutableArray.CreateBuilder<BoundPropertyElement>();
        var propertyCount = ReadCount(reader);
        for (var i = 0; i < propertyCount; ++i)
        {
            var owner = reader.ReadString();
            var property = reader.ReadString();
            var text = ReadOptional(reader);
            var propertyLocation = ReadLocation(reader);
            var values = ImmutableArray.CreateBuilder<BoundElement>();
            var count = ReadCount(reader);
            for (var j = 0; j < count; ++j)
                values.Add(ReadElement(reader, depth + 1));
            properties.Add(new(owner, property, values.ToImmutable(), text, propertyLocation));
        }
        return new(typeName, name, id, members.ToImmutable(), children.ToImmutable(),
            properties.ToImmutable(), location);
    }

    private static void WriteOptional(BinaryWriter writer, string? value)
    {
        writer.Write(value is not null);
        if (value is not null) writer.Write(value);
    }

    private static string? ReadOptional(BinaryReader reader) => reader.ReadBoolean() ? reader.ReadString() : null;

    private static void WriteLocation(BinaryWriter writer, XamlSourceLocation location)
    {
        writer.Write(location.FilePath);
        writer.Write(location.Line);
        writer.Write(location.Column);
    }

    private static XamlSourceLocation ReadLocation(BinaryReader reader) =>
        new(reader.ReadString(), reader.ReadInt32(), reader.ReadInt32());

    private static int ReadCount(BinaryReader reader)
    {
        var count = reader.ReadInt32();
        if (count is < 0 or > MaxEntries)
            throw new InvalidDataException("Invalid compiled markup item count.");
        return count;
    }

    private static void CheckDepth(int depth)
    {
        if (depth > 256)
            throw new InvalidDataException("Compiled markup nesting limit exceeded.");
    }
}
