module;
#include <string>
#include "Views/MainPage.xaml.g.h"

export module NativeXamlFixture;

namespace winrt::Fixture::implementation
{
    struct DemoModel
    {
        std::shared_ptr<std::wstring> title = std::make_shared<std::wstring>(L"Native binding");
        ::winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged notifier;
        std::wstring Title() const { return *title; }
        void Title(std::wstring const& value) { *title = value; notifier.RaiseChanged(); }
        template <typename T> T try_as() const { return static_cast<T>(notifier); }
    };

    struct MainPage : MainPageT<MainPage>
    {
        DemoModel Model;
        int saves{};
        DemoModel ViewModel() const { return Model; }
        void SaveClicked() { ++saves; }

        void CompileGeneratedMembers()
        {
            InitializeComponent();
            Connect(3, {});
            UpdateBindings();
            (void)TitleBlock();
            (void)SaveButton();
        }
    };
}

extern "C" int ValidateNativeGeneratedXaml()
{
    winrt::Fixture::implementation::MainPage page;
    page.InitializeComponent();
    if (page.TitleBlock().Text() != L"Native binding") return 1;
    if (page.Model.notifier.SubscriptionCount() != 1 || page.TitleBlock().CallbackCount() != 1) return 9;
    page.Model.Title(L"Updated");
    if (page.TitleBlock().Text() != L"Updated") return 2;
    page.TitleBlock().Text(L"From UI");
    if (page.Model.Title() != L"From UI") return 3;
    page.RaiseUnloaded();
    if (page.Model.notifier.SubscriptionCount() != 0 || page.TitleBlock().CallbackCount() != 0) return 10;
    page.Model.Title(L"While unloaded");
    if (page.TitleBlock().Text() != L"From UI") return 4;
    page.TitleBlock().Text(L"UI while unloaded");
    if (page.Model.Title() != L"While unloaded") return 5;
    page.RaiseLoaded();
    page.RaiseLoaded();
    if (page.Model.notifier.SubscriptionCount() != 1 || page.TitleBlock().CallbackCount() != 1) return 11;
    if (page.TitleBlock().Text() != L"While unloaded") return 6;
    page.TitleBlock().Text(L"Reconnected");
    if (page.Model.Title() != L"Reconnected") return 7;
    page.SaveButton().RaiseClick();
    if (page.saves != 1) return 8;
    return 0;
}
