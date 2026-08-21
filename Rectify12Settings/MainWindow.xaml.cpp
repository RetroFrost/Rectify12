#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Rectify12Settings::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        NavView().SelectedItem(NavView().MenuItems().GetAt(0));
        NavigateTo(Rectify12::Settings::Page::Home);
    }

    void MainWindow::SetPageText(std::wstring_view title, std::wstring_view description)
    {
        PageTitle().Text(hstring{ title });
        PageDescription().Text(hstring{ description });
    }

    void MainWindow::NavigateTo(Rectify12::Settings::Page page)
    {
        using Rectify12::Settings::Page;

        switch (page)
        {
        case Page::Appearance:
            SetPageText(L"Appearance", L"Themes, resources, icons, fonts, cursors, sounds and Rectify12 visual style.");
            break;
        case Page::Effects:
            SetPageText(L"Effects", L"Mica, Mica Alt, Acrylic and replacement of compatible generic dark surfaces.");
            break;
        case Page::Explorer:
            SetPageText(L"File Manager & Explorer", L"Explorer patching, full context menus, third-party shell extensions and File Manager behaviour.");
            break;
        case Page::Legacy:
            SetPageText(L"Windows settings", L"Modern replacements and routes for Windows settings that still live in legacy Control Panel surfaces.");
            break;
        case Page::Compatibility:
            SetPageText(L"Compatibility", L"Per-process exclusions, build-specific fallbacks and module compatibility controls.");
            break;
        case Page::Recovery:
            SetPageText(L"Recovery", L"Disable Rectify12 modules, restore Windows defaults, inspect migration state and recover from a broken visual component.");
            break;
        case Page::About:
            SetPageText(L"About Rectify12", L"Version, installed modules, update channel, diagnostics and project information.");
            break;
        case Page::Home:
        default:
            SetPageText(L"Rectify12", L"Windows visual consistency, File Manager, effects, modernised legacy settings and recovery.");
            break;
        }
    }

    void MainWindow::NavView_SelectionChanged(
        NavigationView const&,
        NavigationViewSelectionChangedEventArgs const& args)
    {
        const auto item = args.SelectedItem().try_as<NavigationViewItem>();
        if (!item) return;

        const auto tag = unbox_value_or<hstring>(item.Tag(), L"home");
        NavigateTo(Rectify12::Settings::PageFromUri(tag));
    }

    void MainWindow::OpenWindowsSettings_Click(
        Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        Windows::System::Launcher::LaunchUriAsync(Windows::Foundation::Uri{ L"ms-settings:" });
    }
}
