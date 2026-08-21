#pragma once

#include "framework.h"

namespace Rectify12::Effects {
    enum class BackdropKind {
        None,
        Mica,
        Acrylic,
        MicaAlt
    };

    struct WindowEffectOptions {
        bool immersiveDark = true;
        bool extendFrame = true;
        BackdropKind backdrop = BackdropKind::Mica;
    };

    HRESULT ApplyWindowEffects(HWND hwnd, const WindowEffectOptions& options);
}
