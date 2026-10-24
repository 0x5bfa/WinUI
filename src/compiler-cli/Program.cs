using System.Text;
using Microsoft.UI.Xaml.Compiler.Binary;
using Microsoft.UI.Xaml.Compiler.Generation;
using Microsoft.UI.Xaml.Compiler.Semantics;
using Microsoft.UI.Xaml.Compiler.Syntax;

try
{
    var options = ParseOptions(args);
    var inputPath = Required(options, "--source");
    var outputPath = Required(options, "--output");
    var resourcePath = Required(options, "--resource");
    var language = Required(options, "--language") switch
    {
        "csharp" => XamlTargetLanguage.CSharp,
        "cppwinrt" => XamlTargetLanguage.CppWinRT,
        var value => throw new ArgumentException($"Unknown language '{value}'."),
    };

    var parse = XamlSyntaxParser.ParseFile(inputPath);
    if (parse.Document is null)
    {
        WriteDiagnostics(parse.Diagnostics);
        return 1;
    }

    var catalog = new XamlTypeCatalog();
    foreach (var reference in options.GetValueOrDefault("--reference") ?? [])
        catalog.AddMetadataFile(reference);
    var bound = XamlSemanticBinder.Bind(parse.Document, catalog);
    var generation = XamlSourceGenerator.Generate(bound, catalog, language, resourcePath);
    WriteDiagnostics(generation.Diagnostics);
    if (!generation.Success) return 1;

    if (generation.Files.Length != 1)
        throw new InvalidOperationException("Expected one code-behind artifact per input document.");
    var source = generation.Files[0].Text.Replace("\r\n", "\n", StringComparison.Ordinal)
        .Replace("\n", "\r\n", StringComparison.Ordinal);
    Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outputPath))!);
    if (!File.Exists(outputPath) || File.ReadAllText(outputPath) != source)
        File.WriteAllText(outputPath, source, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
    Console.WriteLine(outputPath);
    if (options.GetValueOrDefault("--binary-output")?.LastOrDefault() is { } binaryPath)
    {
        using var serialized = new MemoryStream();
        CompiledMarkup.Write(bound, serialized);
        var bytes = serialized.ToArray();
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(binaryPath))!);
        if (!File.Exists(binaryPath) || !File.ReadAllBytes(binaryPath).AsSpan().SequenceEqual(bytes))
            File.WriteAllBytes(binaryPath, bytes);
        Console.WriteLine(binaryPath);
    }
    return 0;
}
catch (Exception exception) when (exception is ArgumentException or IOException or UnauthorizedAccessException
    or BadImageFormatException)
{
    Console.Error.WriteLine($"XMC9999: {exception.Message}");
    return 1;
}

static Dictionary<string, List<string>> ParseOptions(string[] args)
{
    var options = new Dictionary<string, List<string>>(StringComparer.Ordinal);
    for (var index = 0; index < args.Length; index += 2)
    {
        if (index + 1 >= args.Length || !args[index].StartsWith("--", StringComparison.Ordinal))
            throw new ArgumentException("Expected --option value pairs.");
        if (!options.TryGetValue(args[index], out var values))
            options[args[index]] = values = [];
        values.Add(args[index + 1]);
    }
    return options;
}

static string Required(Dictionary<string, List<string>> options, string name) =>
    options.GetValueOrDefault(name)?.LastOrDefault() ?? throw new ArgumentException($"Missing {name}.");

static void WriteDiagnostics(IEnumerable<XamlDiagnostic> diagnostics)
{
    foreach (var diagnostic in diagnostics)
        Console.Error.WriteLine($"{diagnostic.Location.FilePath}({diagnostic.Location.Line},{diagnostic.Location.Column}): error {diagnostic.Code}: {diagnostic.Message}");
}
