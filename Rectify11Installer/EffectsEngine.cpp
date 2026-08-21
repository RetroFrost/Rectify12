#include "EffectsEngine.h"

namespace Rectify12::Effects {
    namespace {
        DWM_SYSTEMBACKDROP_TYPE ToDwmBackdrop(BackdropKind backdrop) {
            switch (backdrop) {
            case BackdropKind::Mica:
                return DWMSBT_MAINWINDOW;
            case BackdropKind::Acrylic:
                return DWMSBT_TRANSIENTWINDOW;
            case BackdropKind::MicaAlt:
                return DWMSBT_TABBEDWINDOW;
            case BackdropKind::None:
            default:
                return DWMSBT_NONE;
            }
        }

        void CaptureFirstFailure(HRESULT candidate, HRESULT& result) {
            if (FAILED(candidate) && SUCCEEDED(result)) {
                result = candidate;
            }
        }
    }

    HRESULT ApplyWindowEffects(HWND hwnd, const WindowEffectOptions& options) {
        if (!hwnd || !IsWindow(hwnd)) {
            return E_INVALIDARG;
        }

        HRESULT result = S_OK;
        BOOL darkMode = options.immersiveDark ? TRUE : FALSE;

        CaptureFirstFailure(
            DwmSetWindowAttribute(
                hwnd,
                DWMWA_USE_IMMERSIVE_DARK_MODE,
                &darkMode,
                sizeof(darkMode)),
            result);

        if (options.extendFrame) {
            const MARGINS margins = { -1, -1, -1, -1 };
            CaptureFirstFailure(DwmExtendFrameIntoClientArea(hwnd, &margins), result);
        }

        const DWM_SYSTEMBACKDROP_TYPE backdrop = ToDwmBackdrop(options.backdrop);
        CaptureFirstFailure(
            DwmSetWindowAttribute(
                hwnd,
                DWMWA_SYSTEMBACKDROP_TYPE,
                &backdrop,
                sizeof(backdrop)),
            result);

        return result;
    }
}
