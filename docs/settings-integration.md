# Rectify12 Settings integration

Rectify12 treats Windows Settings as the primary configuration surface. The legacy Rectify12 Control Panel is transitional and will be removed after the WinUI 3 Settings package reaches feature parity and passes CI/PC testing.

## UI baseline

All Rectify12-owned user interfaces target C++/WinRT + WinUI 3 on the Windows App SDK. The current baseline is Windows App SDK 2.4.0, Microsoft.Windows.CppWinRT 3.0.260818.1, the Visual Studio 2026 v145 toolset, and `/std:c++latest`.

Owned UI includes:

- Rectify12 Settings
- Rectify12 installer/recovery UI (migration after Settings is stable)
- Rectify12 File Manager / Explorer-facing configuration UI
- Rectify12 diagnostics and rollback UI

Low-level shell hooks, Windhawk mods, resource redirectors and service/installer backends remain native C++/Win32 where WinUI is not an appropriate execution environment. They expose settings to the WinUI layer rather than implementing their own UI.

## Windows Settings integration

Windows exposes the restricted `windows.settingsApp` package extension. Rectify12 will register a packaged Settings application using `rescap:SettingsApp` so that Rectify12 appears through Windows Settings links/search and deep links.

The documented Settings extension model links from Windows Settings into a partner settings application; it does not provide a public contract for rendering arbitrary WinUI content directly inside `SystemSettings.exe`. Rectify12 must therefore keep the integration version-aware and non-destructive. We do not patch `SystemSettings.exe` binaries.

Initial category: `extras`.

Planned deep links:

- `rectify12-settings://home`
- `rectify12-settings://appearance`
- `rectify12-settings://effects`
- `rectify12-settings://explorer`
- `rectify12-settings://legacy`
- `rectify12-settings://compatibility`
- `rectify12-settings://recovery`
- `rectify12-settings://about`

## Control Panel migration

Rectify12's goal is to remove the need to open legacy Control Panel for Windows settings that can be represented safely through supported system APIs, documented `ms-settings:` routes, COM/WinRT configuration APIs, or well-understood registry policy/state.

Migration rules:

1. Prefer an existing Windows Settings page and deep-link to it when Microsoft already owns the setting.
2. If Windows Settings has no equivalent but the setting has a supported API, expose a native WinUI 3 Rectify12 page.
3. If the setting is backed only by a legacy CPL/COM surface, implement an adapter and keep the old CPL as a fallback until the adapter is verified.
4. Never remove a legacy path until the replacement can read the current state, write it, validate the result, and roll back.
5. Preserve third-party Control Panel items; Rectify12 only migrates Windows/Rectify-owned settings.

## Planned page groups

### System
- power and advanced power options
- system properties equivalents
- environment-variable entry points
- optional features and Windows features links
- recovery/system restore entry points

### Devices
- devices and printers migration links/adapters
- mouse and keyboard legacy options
- sound device advanced properties
- AutoPlay

### Network
- adapter properties and legacy network connections
- sharing/advanced network options
- proxy/VPN links where Windows Settings already owns the UI

### Personalisation
- Rectify12 themes
- icons/resources
- Mica/Acrylic/Mica Alt
- generic-dark replacement
- fonts/cursors/sounds

### Explorer
- Rectify12 Explorer patch
- full context menu policy
- third-party shell-extension handling
- File Manager options
- resource redirection

### Compatibility and recovery
- per-process exclusions
- module enable/disable
- safe mode
- restore defaults
- v3.2 migration state
- logs and diagnostics

## Compatibility

The Settings app must detect the Windows build and expose only routes/adapters validated for that build. 25H2 and 26H1 are the first primary targets. Unsupported or moved Windows pages must fail gracefully and fall back to the relevant native Windows surface rather than leaving a dead link.
