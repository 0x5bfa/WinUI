using Microsoft.UI.Xaml.Compiler.Syntax;
using Microsoft.UI.Xaml.Compiler.Binary;
using Microsoft.UI.Xaml.Compiler.Generation;
using Microsoft.UI.Xaml.Compiler.Semantics;

const string xaml = """
    <Page xmlns="using:Microsoft.UI.Xaml.Controls"
          xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
          x:Class="Sample.MainPage">
      <Grid Grid.Row="0">
        <TextBlock Text="{x:Bind ViewModel.Title}" />
        <TextBlock xml:space="preserve">  text  </TextBlock>
      </Grid>
    </Page>
    """;

var result = XamlSyntaxParser.Parse(xaml, "MainPage.xaml");
Require(result.Success, "valid XAML should parse");
var root = result.Document!.Root;
Require(root.Name == new XamlName("using:Microsoft.UI.Xaml.Controls", "Page"), "root namespace");
Require(root.NamespaceDeclarations.Any(n => n.Prefix == "x" && n.NamespaceUri.EndsWith("/xaml", StringComparison.Ordinal)), "x namespace");
Require(root.Attributes.Single(a => a.Name.LocalName == "Class").Value == "Sample.MainPage", "x:Class");
Require(root.Location.Line == 1 && root.Location.Column == 2, "root source location");

var grid = root.Children.OfType<XamlElementSyntax>().Single();
Require(grid.Attributes.Single().Name.LocalName == "Grid.Row", "attached member syntax");
var textBlocks = grid.Children.OfType<XamlElementSyntax>().ToArray();
Require(textBlocks[0].Attributes.Single().Value == "{x:Bind ViewModel.Title}", "binding markup preserved");
Require(textBlocks[0].Attributes.Single().Location.Line == 5, "attribute source line");
Require(textBlocks[1].Children.OfType<XamlTextSyntax>().Single().Value == "  text  ", "xml:space content preserved");

var cdata = XamlSyntaxParser.Parse("<Page><![CDATA[{Binding Path=Title}]]></Page>", "Cdata.xaml");
Require(cdata.Document?.Root.Children.Single() is XamlTextSyntax { IsCData: true, Value: "{Binding Path=Title}" }, "CDATA preserved");

var malformed = XamlSyntaxParser.Parse("<Page>\n<Grid></Page>", "Malformed.xaml");
Require(malformed.Document is null && malformed.Diagnostics.Single().Code == "XMC0001", "malformed XML diagnostic");
Require(malformed.Diagnostics.Single().Location.FilePath == "Malformed.xaml" && malformed.Diagnostics.Single().Location.Line == 2,
    "diagnostic location");

var dtd = XamlSyntaxParser.Parse("<!DOCTYPE Page [<!ENTITY payload SYSTEM 'file:///c:/secret.txt'>]><Page>&payload;</Page>", "Dtd.xaml");
Require(dtd.Document is null && dtd.Diagnostics.Single().Code == "XMC0001", "external entity blocked");

var empty = XamlSyntaxParser.Parse("", "Empty.xaml");
Require(!empty.Success && empty.Diagnostics.Length == 1, "empty document diagnostic");

Console.WriteLine("XAML syntax smoke checks passed.");

var catalog = new XamlTypeCatalog();
catalog.Add(new("Microsoft.UI.Xaml.Controls.Page", "Microsoft.UI.Xaml.FrameworkElement",
    new HashSet<string>(StringComparer.Ordinal)));
catalog.Add(new("Microsoft.UI.Xaml.FrameworkElement", null,
    new HashSet<string>(["Unloaded"], StringComparer.Ordinal), null,
    new HashSet<string>(["Unloaded"], StringComparer.Ordinal)));
catalog.Add(new("Microsoft.UI.Xaml.Controls.Grid", null,
    new HashSet<string>(["Children", "SetRow"], StringComparer.Ordinal)));
catalog.Add(new("Microsoft.UI.Xaml.Controls.TextBlock", null,
    new HashSet<string>(["Text"], StringComparer.Ordinal)));
catalog.Add(new("Microsoft.UI.Xaml.Controls.Button", null,
    new HashSet<string>(["Click"], StringComparer.Ordinal), null,
    new HashSet<string>(["Click"], StringComparer.Ordinal)));

const string semanticXaml = """
    <Page xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
          xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
          x:Class="Demo.MainPage">
      <Grid Grid.Row="0">
        <Grid.Children>
          <TextBlock x:Name="TitleBlock" Text="{x:Bind ViewModel.Title, Mode=OneWay}" />
        </Grid.Children>
        <Button x:Name="SaveButton" Click="SaveClicked" />
      </Grid>
    </Page>
    """;
var semanticSyntax = XamlSyntaxParser.Parse(semanticXaml, "Views/MainPage.xaml");
Require(semanticSyntax.Success, "semantic fixture parses");
var bound = XamlSemanticBinder.Bind(semanticSyntax.Document!, catalog);
Require(bound.Success, string.Join("\n", bound.Diagnostics.Select(d => d.Message)));
Require(bound.Root.ConnectionId == 1 && bound.Root.Children.Single().ConnectionId == 2 &&
    bound.Root.Children.Single().Properties.Single().Values.Single().ConnectionId == 3 &&
    bound.Root.Children.Single().Children.Single().ConnectionId == 4,
    "connection IDs follow depth-first document order");
using (var serialized = new MemoryStream())
{
    CompiledMarkup.Write(bound, serialized);
    var bytes = serialized.ToArray();
    serialized.Position = 0;
    var restored = CompiledMarkup.Read(serialized);
    Require(restored.ClassName == bound.ClassName &&
        restored.Root.Children.Single().Properties.Single().Values.Single().Name == "TitleBlock",
        "compiled markup round-trips nested properties and names");
    using var second = new MemoryStream();
    CompiledMarkup.Write(restored, second);
    Require(bytes.AsSpan().SequenceEqual(second.ToArray()), "compiled markup serializes deterministically");
    bytes[0] = 0;
    using var invalid = new MemoryStream(bytes);
    try
    {
        _ = CompiledMarkup.Read(invalid);
        throw new Exception("Invalid compiled markup signature was accepted.");
    }
    catch (InvalidDataException) { }
}
Require(bound.Root.TypeName == "Microsoft.UI.Xaml.Controls.Page", "presentation namespace maps to metadata");
var boundGrid = bound.Root.Children.Single();
Require(boundGrid.Members.Single().OwnerType == "Microsoft.UI.Xaml.Controls.Grid", "attached owner resolved");
Require(boundGrid.Properties.Single().Name == "Children", "property element retains owner and property");
Require(boundGrid.Children.Single().TypeName == "Microsoft.UI.Xaml.Controls.Button", "property values not flattened");
Require(boundGrid.Properties.Single().Values.Single().Members.Single(m => m.Name == "Text").Binding?.Mode
    == XamlBindingMode.OneWay, "typed OneWay binding in property element");

var generated = XamlSourceGenerator.Generate(bound, catalog, XamlTargetLanguage.CSharp, "Views/MainPage.xaml");
Require(generated.Success && generated.Files.Single().Text.Contains("TitleBlock = __xc3", StringComparison.Ordinal),
    "bindings inside property elements get normal connector fields");

const string simpleXaml = """
    <Page xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
          xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
          x:Class="Demo.MainPage">
      <Grid>
        <TextBlock x:Name="TitleBlock" Text="{x:Bind ViewModel.Title, Mode=OneWay}" />
        <Button x:Name="SaveButton" Click="SaveClicked" />
      </Grid>
    </Page>
    """;
var simpleBound = XamlSemanticBinder.Bind(XamlSyntaxParser.Parse(simpleXaml, "Views/MainPage.xaml").Document!, catalog);
Require(simpleBound.Success, "simple semantic binding");
var csSource = XamlSourceGenerator.Generate(simpleBound, catalog, XamlTargetLanguage.CSharp, "Views/MainPage.xaml");
Require(csSource.Success && csSource.Files.Single().Text.Contains("__UpdateBindings()", StringComparison.Ordinal),
    "C# binding code generation");
Require(csSource.Files.Single().Text.Contains("SaveClicked", StringComparison.Ordinal), "event hookup code generation");
Require(csSource.Files.Single().Text.Contains("ms-appx:///Views/MainPage.xaml", StringComparison.Ordinal),
    "app-relative resource URI");

var cppSource = XamlSourceGenerator.Generate(simpleBound, catalog, XamlTargetLanguage.CppWinRT, "Views/MainPage.xaml");
Require(cppSource.Success && cppSource.Files.Single().Text.Contains("_bindingSubscriptions", StringComparison.Ordinal),
    "C++/WinRT OneWay binding subscription generation");
var twoWayXaml = simpleXaml.Replace("Mode=OneWay", "Mode=TwoWay", StringComparison.Ordinal);
var twoWayBound = XamlSemanticBinder.Bind(XamlSyntaxParser.Parse(twoWayXaml, "Views/MainPage.xaml").Document!, catalog);
cppSource = XamlSourceGenerator.Generate(twoWayBound, catalog, XamlTargetLanguage.CppWinRT, "Views/MainPage.xaml");
Require(cppSource.Success && cppSource.Files.Single().Text.Contains("RegisterPropertyChangedCallback", StringComparison.Ordinal),
    "C++/WinRT TwoWay callback generation");
var oneTimeXaml = simpleXaml.Replace("Mode=OneWay", "Mode=OneTime", StringComparison.Ordinal);
var oneTimeBound = XamlSemanticBinder.Bind(XamlSyntaxParser.Parse(oneTimeXaml, "Views/MainPage.xaml").Document!, catalog);
cppSource = XamlSourceGenerator.Generate(oneTimeBound, catalog, XamlTargetLanguage.CppWinRT, "Views/MainPage.xaml");
Require(cppSource.Success && cppSource.Files.Single().Text.Contains("IComponentConnector", StringComparison.Ordinal),
    "C++/WinRT generated connector");

var duplicateNames = XamlSyntaxParser.Parse("""
    <Page xmlns="using:Microsoft.UI.Xaml.Controls" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
      <Grid><Button x:Name="Duplicate"/><TextBlock x:Name="Duplicate"/></Grid>
    </Page>
    """, "Bad.xaml");
Require(XamlSemanticBinder.Bind(duplicateNames.Document!, catalog).Diagnostics.Any(d => d.Code == "XMC1004"),
    "duplicate name diagnostic");

var winmd = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
    ".nuget", "packages", "microsoft.windowsappsdk.winui", "2.3.9", "metadata", "Microsoft.UI.Xaml.winmd");
if (File.Exists(winmd))
{
    var realCatalog = new XamlTypeCatalog();
    realCatalog.AddMetadataFile(winmd);
    Require(realCatalog.Find("Microsoft.UI.Xaml.Controls.Grid") is not null, "WinMD type enumeration");
    Require(realCatalog.HasMember("Microsoft.UI.Xaml.Controls.Grid", "Row"), "WinMD attached property");
}

Console.WriteLine("XAML semantic and source generation smoke checks passed.");

static void Require(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}
