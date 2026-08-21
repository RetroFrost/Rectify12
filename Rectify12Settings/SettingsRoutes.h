#pragma once

#include <string_view>

namespace Rectify12::Settings {
    enum class Page {
        Home,
        Appearance,
        Effects,
        Explorer,
        Legacy,
        Compatibility,
        Recovery,
        About,
    };

    inline constexpr std::wstring_view Protocol = L"rectify12-settings";

    inline Page PageFromUri(std::wstring_view uri) noexcept {
        if (uri.find(L"appearance") != std::wstring_view::npos) return Page::Appearance;
        if (uri.find(L"effects") != std::wstring_view::npos) return Page::Effects;
        if (uri.find(L"explorer") != std::wstring_view::npos) return Page::Explorer;
        if (uri.find(L"legacy") != std::wstring_view::npos) return Page::Legacy;
        if (uri.find(L"compatibility") != std::wstring_view::npos) return Page::Compatibility;
        if (uri.find(L"recovery") != std::wstring_view::npos) return Page::Recovery;
        if (uri.find(L"about") != std::wstring_view::npos) return Page::About;
        return Page::Home;
    }
}
