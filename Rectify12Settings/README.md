# Rectify12 Settings

This directory is the replacement for the temporary `Rectify12ControlPanel` project.

Target stack:

- C++/WinRT
- WinUI 3
- Windows App SDK 2.3.1
- Microsoft.Windows.CppWinRT 3.0.260715.1
- Visual Studio 2026 / v145
- `/std:c++latest`
- packaged deployment so Rectify12 can register the `windows.settingsApp` extension

The first milestone is a buildable Settings shell with protocol/deep-link routing and an MSIX manifest for Windows Settings integration. Once this reaches feature parity with the temporary Control Panel, the old project will be removed.

Do not patch or replace `SystemSettings.exe`. Rectify12 integrates through the Settings app extension/deep-link contract and keeps build-specific fallbacks for legacy Windows surfaces.
