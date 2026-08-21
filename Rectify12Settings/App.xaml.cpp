#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "SettingsRoutes.h"
#if __has_include("App.g.cpp")
#include "App.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::Windows::AppLifecycle;

namespace winrt::Rectify12Settings::implementation
{
    App::App()
    {
#if defined(_DEBUG) && !defined(DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION)
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& args)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = args.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        m_window = make<MainWindow>();
        ApplyActivationRoute();
        m_window.Activate();
    }

    void App::ApplyActivationRoute()
    {
        const auto activation = AppInstance::GetCurrent().GetActivatedEventArgs();
        if (!activation || activation.Kind() != ExtendedActivationKind::Protocol)
        {
            return;
        }

        const auto protocolArgs = activation.Data().try_as<Windows::ApplicationModel::Activation::ProtocolActivatedEventArgs>();
        if (!protocolArgs)
        {
            return;
        }

        const auto route = Rectify12::Settings::PageFromUri(protocolArgs.Uri().RawUri());
        const auto mainWindow = m_window.try_as<Rectify12Settings::MainWindow>();
        if (mainWindow)
        {
            get_self<implementation::MainWindow>(mainWindow)->NavigateTo(route);
        }
    }
}
