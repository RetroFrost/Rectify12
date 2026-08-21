#include "pch.h"
#include "MainWindow.xaml.h"
#include "FeatureCatalog.h"
#include "RuntimePolicy.h"
#include "SystemActions.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <filesystem>
#include <string>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {
    std::filesystem::path DefaultBackupPath() {
        wchar_t profile[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, ARRAYSIZE(profile)) == 0) {
            return std::filesystem::path(L"Rectify12-settings.r12cfg");
        }

        const std::filesystem::path directory = std::filesystem::path(profile) / L"Documents" / L"Rectify12";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        return directory / L"settings.r12cfg";
    }

    std::vector<std::wstring> ParseSemicolonList(std::wstring_view text) {
        std::vector<std::wstring> result;
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto end = text.find(L';', start);
            std::wstring item(text.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start));

            const auto first = item.find_first_not_of(L" \t\r\n");
            const auto last = item.find_last_not_of(L" \t\r\n");
            if (first != std::wstring::npos) {
                item = item.substr(first, last - first + 1);
                if (!item.empty()) result.push_back(std::move(item));
            }

            if (end == std::wstring_view::npos) break;
            start = end + 1;
        }
        return result;
    }
}

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

    void MainWindow::SetActionResult(bool success, std::wstring_view message)
    {
        ActionResult().Severity(success ? InfoBarSeverity::Success : InfoBarSeverity::Error);
        ActionResult().Message(hstring{ message });
        ActionResult().IsOpen(true);
    }

    void MainWindow::LoadExclusionsIntoEditor()
    {
        const auto exclusions = Rectify12::SystemActions::LoadCompatibilityExclusions();
        std::wstring joined;
        for (std::size_t i = 0; i < exclusions.size(); ++i) {
            if (i) joined += L"; ";
            joined += exclusions[i];
        }
        ExclusionsEditor().Text(hstring{ joined });
    }

    void MainWindow::UpdateActionVisibility(Rectify12::Settings::Page page)
    {
        using Rectify12::Settings::Page;
        ExplorerActions().Visibility(page == Page::Explorer ? Visibility::Visible : Visibility::Collapsed);
        CompatibilityActions().Visibility(page == Page::Compatibility ? Visibility::Visible : Visibility::Collapsed);
        RecoveryActions().Visibility(page == Page::Recovery ? Visibility::Visible : Visibility::Collapsed);
        OpenWindowsSettingsButton().Visibility((page == Page::Home || page == Page::Legacy) ? Visibility::Visible : Visibility::Collapsed);

        if (page == Page::Compatibility) {
            LoadExclusionsIntoEditor();
        }
    }

    void MainWindow::NavigateTo(Rectify12::Settings::Page page)
    {
        using Rectify12::Settings::Page;
        ActionResult().IsOpen(false);
        UpdateActionVisibility(page);

        switch (page)
        {
        case Page::Appearance:
            SetPageText(L"Appearance", L"Themes, resources, icons, fonts, cursors, sounds and the Rectify12 visual style.");
            break;
        case Page::Effects:
            SetPageText(L"Effects", L"Acrylic is the Rectify12 default backdrop. Mica and Mica Alt remain available as compatibility choices.");
            break;
        case Page::Explorer:
            SetPageText(L"File Manager & Explorer", L"Explorer patching, full third-party context menus, shell extensions and File Manager behaviour.");
            break;
        case Page::Legacy:
            SetPageText(L"Windows settings", L"WinUI 3 replacements and routes for Windows settings that still live in legacy Control Panel surfaces.");
            break;
        case Page::Compatibility:
            SetPageText(L"Compatibility", L"Per-process exclusions, build-specific fallbacks and protection from incompatible hooks.");
            break;
        case Page::Recovery:
            SetPageText(L"Recovery", L"Global UI restore, restore snapshots, settings backup/import, health checks, module rollback and repair tools.");
            break;
        case Page::About:
            SetPageText(
                L"About Rectify12",
                L"Rectify12 has a fixed 100-feature product catalogue. The old update-channel selector is not part of the design; feature 99 is the automatic restore snapshot.");
            break;
        case Page::Home:
        default:
            SetPageText(L"Rectify12", L"Windows visual consistency, File Manager, Acrylic effects, WinUI 3 rewrites and recovery.");
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

    void MainWindow::RestartExplorer_Click(
        Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        const auto result = Rectify12::SystemActions::RestartExplorer();
        SetActionResult(result.success, result.message);
    }

    void MainWindow::SaveExclusions_Click(
        Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        const auto exclusions = ParseSemicolonList(ExclusionsEditor().Text().c_str());
        const auto result = Rectify12::SystemActions::SaveCompatibilityExclusions(exclusions);
        SetActionResult(result.success, result.message);
        if (result.success) LoadExclusionsIntoEditor();
    }

    void MainWindow::RestoreOriginalWindowsUi_Click(
        Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        if (!Rectify12::RuntimePolicy::SetEnabled(false)) {
            SetActionResult(false, L"Could not disable the Rectify12 runtime policy.");
            return;
        }

        const auto explorer = Rectify12::SystemActions::RestartExplorer();
        if (explorer.success) {
            SetActionResult(true, L"Rectify12 hooks are disabled. Explorer was restarted; reopen other affected apps to restore their original Windows UI.");
        }
        else {
            SetActionResult(true, L"Rectify12 hooks are disabled. Reopen affected apps; Explorer could not be restarted automatically.");
        }
    }

    void MainWindow::EnableRectify12Ui_Click(
        Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        if (!Rectify12::RuntimePolicy::SetEnabled(true)) {
            SetActionResult(false, L"Could not enable the Rectify12 runtime policy.");
            return;
        }

        const auto explorer = Rectify12::SystemActions::RestartExplorer();
        if (explorer.success) {
            SetActionResult(true, L"Rectify12 hooks are enabled. Explorer was restarted; reopen other supported apps to apply Rectify12.");
        }
        else {
            SetActionResult(true, L"Rectify12 hooks are enabled. Reopen supported apps to apply Rectify12.");
        }
    }

    void MainWindow::CreateRestoreSnapshot_Click(
        Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        const auto result = Rectify12::SystemActions::CreateRestoreSnapshot();
        SetActionResult(result.success, result.message);
    }

    void MainWindow::ExportSettings_Click(
        Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        const auto path = DefaultBackupPath();
        auto result = Rectify12::SystemActions::ExportSettings(path);
        if (result.success) {
            result.message += L" Saved to ";
            result.message += path.wstring();
        }
        SetActionResult(result.success, result.message);
    }

    void MainWindow::ImportSettings_Click(
        Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        const auto path = DefaultBackupPath();
        auto result = Rectify12::SystemActions::ImportSettings(path);
        if (result.success) {
            result.message += L" Loaded from ";
            result.message += path.wstring();
            LoadExclusionsIntoEditor();
        }
        SetActionResult(result.success, result.message);
    }

    void MainWindow::RunHealthCheck_Click(
        Windows::Foundation::IInspectable const&,
        RoutedEventArgs const&)
    {
        const auto items = Rectify12::SystemActions::RunHealthCheck();
        std::wstring output;
        bool allHealthy = true;
        for (const auto& item : items) {
            allHealthy = allHealthy && item.healthy;
            output += item.healthy ? L"[OK] " : L"[!] ";
            output += item.name;
            output += L" — ";
            output += item.detail;
            output += L"\n";
        }
        HealthCheckOutput().Text(hstring{ output });
        SetActionResult(allHealthy, allHealthy ? L"Rectify Windows health check passed." : L"Rectify Windows found one or more items that need attention.");
    }
}
