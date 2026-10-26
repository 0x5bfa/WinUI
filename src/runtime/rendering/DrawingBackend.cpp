#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Foundation.Collections.h>
#include "DrawingBackend.h"
#include <algorithm>
#include <cmath>
#include <limits>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace
{
    namespace composition = winrt::Windows::UI::Composition;
    using namespace xaml::drawing;

    void ValidateFormat(TextFormat const& format)
    {
        if (format.family.empty() || format.locale.empty() || !std::isfinite(format.size) ||
            format.size <= 0 || format.size > 16384 || format.weight < 1 || format.weight > 999)
        {
            throw winrt::hresult_invalid_argument();
        }
    }
    float Constraint(double value)
    {
        if (std::isnan(value) || value < 0) { throw winrt::hresult_invalid_argument(); }
        return static_cast<float>(std::min(value, 1'000'000.0));
    }
    winrt::com_ptr<IDWriteFactory> WriteFactory()
    {
        thread_local auto factory = []
        {
            winrt::com_ptr<IDWriteFactory> result;
            winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(result.put())));
            return result;
        }();
        return factory;
    }
    winrt::com_ptr<IDWriteTextLayout> Layout(std::wstring const& text, TextFormat const& format, double width, double height)
    {
        ValidateFormat(format);
        if (text.size() > std::numeric_limits<UINT32>::max()) { throw winrt::hresult_out_of_bounds(); }
        auto factory = WriteFactory();
        winrt::com_ptr<IDWriteTextFormat> textFormat;
        winrt::check_hresult(factory->CreateTextFormat(format.family.c_str(), nullptr,
            static_cast<DWRITE_FONT_WEIGHT>(format.weight), format.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(format.size), format.locale.c_str(), textFormat.put()));
        winrt::check_hresult(textFormat->SetReadingDirection(format.rightToLeft ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT));
        winrt::check_hresult(textFormat->SetWordWrapping(format.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP));
        winrt::com_ptr<IDWriteTextLayout> layout;
        winrt::check_hresult(factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), textFormat.get(),
            Constraint(width), Constraint(height), layout.put()));
        return layout;
    }
    winrt::Windows::UI::Color CompositionColor(Color value) { return { value.a, value.r, value.g, value.b }; }
    D2D1_COLOR_F D2DColor(Color value) { return { value.r / 255.0f, value.g / 255.0f, value.b / 255.0f, value.a / 255.0f }; }
    winrt::com_ptr<IWICBitmapFrameDecode> DecodeFrame(std::wstring const& filePath)
    {
        if (filePath.empty()) { throw winrt::hresult_invalid_argument(); }
        winrt::com_ptr<IWICImagingFactory> factory;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())));
        winrt::com_ptr<IWICBitmapDecoder> decoder;
        winrt::check_hresult(factory->CreateDecoderFromFilename(filePath.c_str(),
            nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.put()));
        winrt::com_ptr<IWICBitmapFrameDecode> frame;
        winrt::check_hresult(decoder->GetFrame(0, frame.put()));
        return frame;
    }
    void ValidateBounds(double x, double y, double width, double height)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
            width < 0 || height < 0 || std::abs(x) > 1e6 || std::abs(y) > 1e6 || width > 1e6 || height > 1e6)
        {
            throw winrt::hresult_invalid_argument();
        }
    }
}

namespace xaml::drawing
{
    void Context::Rectangle(double x, double y, double width, double height, Color fill,
        double radius, Color stroke, double strokeWidth)
    {
        ValidateBounds(x, y, width, height);
        if (!std::isfinite(radius) || radius < 0 || !std::isfinite(strokeWidth) || strokeWidth < 0 || strokeWidth > 1e6)
        {
            throw winrt::hresult_invalid_argument();
        }
        m_commands.push_back({ .kind = CommandKind::Rectangle, .x = x, .y = y, .width = width, .height = height,
            .radius = radius, .strokeWidth = strokeWidth, .fill = fill, .stroke = stroke });
    }
    void Context::Text(double x, double y, double width, double height, std::wstring text, TextFormat format, Color foreground)
    {
        ValidateBounds(x, y, width, height);
        ValidateFormat(format);
        m_commands.push_back({ .kind = CommandKind::Text, .x = x, .y = y, .width = width, .height = height,
            .fill = foreground, .text = std::move(text), .format = std::move(format) });
    }
    void Context::Image(double x, double y, double width, double height,
        std::wstring filePath, ImageStretch stretch)
    {
        ValidateBounds(x, y, width, height);
        if (filePath.empty() || stretch < ImageStretch::None || stretch > ImageStretch::UniformToFill)
        { throw winrt::hresult_invalid_argument(); }
        m_commands.push_back({ .kind = CommandKind::Image, .x = x, .y = y,
            .width = width, .height = height, .imagePath = std::move(filePath), .imageStretch = stretch });
    }
}

xaml::drawing::TextMetrics XamlMeasureText(std::wstring const& text, xaml::drawing::TextFormat const& format,
    double width, double height)
{
    if (text.empty()) { ValidateFormat(format); (void)Constraint(width); (void)Constraint(height); return {}; }
    auto layout = Layout(text, format, width, height);
    DWRITE_TEXT_METRICS metrics{};
    winrt::check_hresult(layout->GetMetrics(&metrics));
    return { metrics.widthIncludingTrailingWhitespace, metrics.height };
}

xaml::drawing::ImageMetrics XamlMeasureImage(std::wstring const& filePath)
{
    auto frame = DecodeFrame(filePath);
    UINT width{}, height{};
    winrt::check_hresult(frame->GetSize(&width, &height));
    return { static_cast<double>(width), static_cast<double>(height) };
}

namespace xaml::runtime::platform
{
    struct DrawingBackend::Impl
    {
        composition::Compositor compositor;
        winrt::com_ptr<ID3D11Device> d3d;
        winrt::com_ptr<ID2D1Device> d2d;
        composition::CompositionGraphicsDevice graphics{ nullptr };
        std::uint64_t generation{ 1 };

        void EnsureDevice()
        {
            if (graphics) { return; }
            auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION, d3d.put(), nullptr, nullptr);
            if (FAILED(hr))
            {
                d3d = nullptr;
                winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                    nullptr, 0, D3D11_SDK_VERSION, d3d.put(), nullptr, nullptr));
            }
            auto dxgi = d3d.as<IDXGIDevice>();
            winrt::check_hresult(D2D1CreateDevice(dxgi.get(), nullptr, d2d.put()));
            auto interop = compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>();
            winrt::check_hresult(interop->CreateGraphicsDevice(d2d.get(),
                reinterpret_cast<ABI::Windows::UI::Composition::ICompositionGraphicsDevice**>(winrt::put_abi(graphics))));
        }
        composition::Visual TextVisual(drawing::Command const& command, float scale, bool rightToLeft)
        {
            EnsureDevice();
            double const pixelWidth = std::ceil(command.width * scale);
            double const pixelHeight = std::ceil(command.height * scale);
            if (pixelWidth > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || pixelHeight > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            {
                throw winrt::hresult_out_of_bounds();
            }
            auto surface = graphics.CreateDrawingSurface({ static_cast<float>(pixelWidth), static_cast<float>(pixelHeight) },
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
            auto interop = surface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
            winrt::com_ptr<ID2D1DeviceContext> context;
            POINT offset{};
            winrt::check_hresult(interop->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), context.put_void(), &offset));
            try
            {
                context->SetDpi(96 * scale, 96 * scale);
                context->SetTransform(D2D1::Matrix3x2F::Translation(offset.x / scale, offset.y / scale));
                context->Clear(D2D1::ColorF(0, 0));
                context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
                winrt::com_ptr<ID2D1SolidColorBrush> brush;
                winrt::check_hresult(context->CreateSolidColorBrush(D2DColor(command.fill), brush.put()));
                auto layout = Layout(command.text, command.format, command.width, command.height);
                context->DrawTextLayout({ 0, 0 }, layout.get(), brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            catch (...)
            {
                (void)interop->EndDraw();
                throw;
            }
            winrt::check_hresult(interop->EndDraw());
            auto visual = compositor.CreateSpriteVisual();
            visual.Size({ static_cast<float>(command.width), static_cast<float>(command.height) });
            auto brush = compositor.CreateSurfaceBrush(surface);
            brush.Stretch(composition::CompositionStretch::Fill);
            visual.Brush(brush);
            visual.Scale({ rightToLeft ? -1.0f : 1.0f, 1, 1 });
            visual.Offset({ static_cast<float>(rightToLeft ? command.x + command.width : command.x), static_cast<float>(command.y), 0 });
            return visual;
        }
        composition::Visual ImageVisual(drawing::Command const& command, float scale)
        {
            EnsureDevice();
            auto const pixelWidth = std::ceil(command.width * scale);
            auto const pixelHeight = std::ceil(command.height * scale);
            if (pixelWidth > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
                pixelHeight > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            {
                throw winrt::hresult_out_of_bounds();
            }

            winrt::com_ptr<IWICImagingFactory> factory;
            winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())));
            auto frame = DecodeFrame(command.imagePath);
            winrt::com_ptr<IWICFormatConverter> converter;
            winrt::check_hresult(factory->CreateFormatConverter(converter.put()));
            winrt::check_hresult(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));

            auto surface = graphics.CreateDrawingSurface({ static_cast<float>(pixelWidth), static_cast<float>(pixelHeight) },
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
            auto interop = surface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
            winrt::com_ptr<ID2D1DeviceContext> context;
            POINT offset{};
            winrt::check_hresult(interop->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), context.put_void(), &offset));
            try
            {
                context->SetDpi(96 * scale, 96 * scale);
                context->SetTransform(D2D1::Matrix3x2F::Translation(offset.x / scale, offset.y / scale));
                context->Clear(D2D1::ColorF(0, 0));
                winrt::com_ptr<ID2D1Bitmap1> bitmap;
                winrt::check_hresult(context->CreateBitmapFromWicBitmap(converter.get(), nullptr, bitmap.put()));
                auto const natural = bitmap->GetSize();
                float const width = static_cast<float>(command.width);
                float const height = static_cast<float>(command.height);
                float drawWidth = width, drawHeight = height;
                if (command.imageStretch != drawing::ImageStretch::Fill && natural.width > 0 && natural.height > 0)
                {
                    float const factor = command.imageStretch == drawing::ImageStretch::None ? 1.0f :
                        command.imageStretch == drawing::ImageStretch::Uniform ?
                        std::min(width / natural.width, height / natural.height) :
                        std::max(width / natural.width, height / natural.height);
                    drawWidth = natural.width * factor;
                    drawHeight = natural.height * factor;
                }
                auto const left = (width - drawWidth) / 2;
                auto const top = (height - drawHeight) / 2;
                context->DrawBitmap(bitmap.get(), D2D1::RectF(left, top, left + drawWidth, top + drawHeight));
            }
            catch (...)
            {
                (void)interop->EndDraw();
                throw;
            }
            winrt::check_hresult(interop->EndDraw());
            auto visual = compositor.CreateSpriteVisual();
            visual.Size({ static_cast<float>(command.width), static_cast<float>(command.height) });
            visual.Offset({ static_cast<float>(command.x), static_cast<float>(command.y), 0 });
            auto brush = compositor.CreateSurfaceBrush(surface);
            brush.Stretch(composition::CompositionStretch::Fill);
            visual.Brush(brush);
            return visual;
        }
    };

    DrawingBackend::DrawingBackend(composition::Compositor const& compositor) : m_impl(std::make_unique<Impl>(compositor)) {}
    DrawingBackend::~DrawingBackend() = default;
    std::uint64_t DrawingBackend::Generation() const noexcept { return m_impl->generation; }
    void DrawingBackend::CheckDevice()
    {
        if (m_impl->d3d && FAILED(m_impl->d3d->GetDeviceRemovedReason())) { ResetDevice(); }
    }
    void DrawingBackend::ResetDevice()
    {
        m_impl->graphics = nullptr; m_impl->d2d = nullptr; m_impl->d3d = nullptr; ++m_impl->generation;
    }
    void DrawingBackend::Draw(composition::ContainerVisual const& target, std::vector<drawing::Command> const& commands,
        float scale, bool rightToLeft, float)
    {
        // Prepare every visual before replacing the last completed content.
        std::vector<composition::Visual> visuals;
        for (auto const& command : commands)
        {
            if (command.width <= 0 || command.height <= 0) { continue; }
            if (command.kind == drawing::CommandKind::Text)
            {
                if (!command.text.empty()) { visuals.push_back(m_impl->TextVisual(command, scale, rightToLeft)); }
                continue;
            }
            if (command.kind == drawing::CommandKind::Image)
            {
                visuals.push_back(m_impl->ImageVisual(command, scale));
                continue;
            }
            auto geometry = m_impl->compositor.CreateRoundedRectangleGeometry();
            float const strokeWidth = static_cast<float>(std::min({ command.strokeWidth, command.width, command.height }));
            float const inset = strokeWidth / 2;
            geometry.Offset({ inset, inset });
            geometry.Size({ static_cast<float>(command.width) - 2 * inset, static_cast<float>(command.height) - 2 * inset });
            float const radius = static_cast<float>(std::min({ command.radius, command.width / 2, command.height / 2 }));
            geometry.CornerRadius({ std::max(0.0f, radius - inset), std::max(0.0f, radius - inset) });
            auto shape = m_impl->compositor.CreateSpriteShape(geometry);
            shape.FillBrush(m_impl->compositor.CreateColorBrush(CompositionColor(command.fill)));
            shape.StrokeBrush(m_impl->compositor.CreateColorBrush(CompositionColor(command.stroke)));
            shape.StrokeThickness(strokeWidth);
            auto visual = m_impl->compositor.CreateShapeVisual();
            visual.Size({ static_cast<float>(command.width), static_cast<float>(command.height) });
            visual.Offset({ static_cast<float>(command.x), static_cast<float>(command.y), 0 });
            visual.Shapes().Append(shape);
            visuals.push_back(visual);
        }
        target.Children().RemoveAll();
        for (auto const& visual : visuals) { target.Children().InsertAtTop(visual); }
    }
}
