#pragma once

#include "App.xaml.g.h"

namespace winrt::Rectify12Settings::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        Microsoft::UI::Xaml::Window m_window{ nullptr };
        void ApplyActivationRoute();
    };
}
