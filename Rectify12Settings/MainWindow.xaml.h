#pragma once

#include "MainWindow.g.h"
#include "SettingsRoutes.h"

namespace winrt::Rectify12Settings::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void NavigateTo(Rectify12::Settings::Page page);
        void NavView_SelectionChanged(
            Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
        void OpenWindowsSettings_Click(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void SetPageText(std::wstring_view title, std::wstring_view description);
    };
}

namespace winrt::Rectify12Settings::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
