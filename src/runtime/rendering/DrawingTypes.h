#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xaml::drawing
{
    struct Color
    {
        std::uint8_t a{}, r{}, g{}, b{};
        bool operator==(Color const&) const = default;
        static constexpr Color FromARGB(std::uint32_t value) noexcept
        {
            return { static_cast<std::uint8_t>(value >> 24), static_cast<std::uint8_t>(value >> 16),
                static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value) };
        }
    };
    struct TextFormat
    {
        std::wstring family{ L"Segoe UI" };
        std::wstring locale{ L"en-US" };
        double size{ 14 };
        std::uint16_t weight{ 400 };
        bool italic{};
        bool wrap{};
        bool rightToLeft{};
        bool operator==(TextFormat const&) const = default;
    };
    struct TextMetrics { double width{}, height{}; };
    struct ImageMetrics { double width{}, height{}; };
    enum class ImageStretch { None, Fill, Uniform, UniformToFill };
    enum class CommandKind { Rectangle, Text, Image };
    struct Command
    {
        CommandKind kind{};
        double x{}, y{}, width{}, height{}, radius{}, strokeWidth{};
        Color fill{}, stroke{};
        std::wstring text;
        std::wstring imagePath;
        ImageStretch imageStretch{ ImageStretch::Uniform };
        TextFormat format;
    };
    class Context
    {
    public:
        void Rectangle(double x, double y, double width, double height, Color fill,
            double radius = 0, Color stroke = {}, double strokeWidth = 0);
        void Text(double x, double y, double width, double height, std::wstring text,
            TextFormat format, Color foreground);
        // Decodes an image on demand from a local file into a WUC drawing surface.
        void Image(double x, double y, double width, double height, std::wstring filePath,
            ImageStretch stretch = ImageStretch::Uniform);
        [[nodiscard]] std::vector<Command> const& Commands() const noexcept { return m_commands; }
    private:
        std::vector<Command> m_commands;
    };
}

xaml::drawing::TextMetrics XamlMeasureText(std::wstring const& text,
    xaml::drawing::TextFormat const& format, double width, double height);
xaml::drawing::ImageMetrics XamlMeasureImage(std::wstring const& filePath);
