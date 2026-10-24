using Microsoft.UI.Xaml.Compiler.Syntax;

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

static void Require(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}
