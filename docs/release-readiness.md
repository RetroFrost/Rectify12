# Rectify12 release readiness

Rectify12 must not be labelled final solely because the foundation projects compile. A final release requires all gates below to be satisfied with evidence from disposable test machines.

## Implemented foundation gates

- Windows 11 22H2, 23H2, 24H2, and 25H2 are explicitly recognised; unknown builds remain blocked.
- Rectify11 v4 Alpha is required and excluded from conflicting-patch detection.
- The JepriCreations cursor archive is bundled, checksum-pinned, installed visibly as part of setup, and backed up for uninstall rollback.
- Windows Update is used for applicable driver updates and failures stop installation.
- Windhawk install/uninstall ownership has been removed from the installer path.
- Installer failures stop the stage instead of silently advancing.

## Blocking final-release gates

- Supply the redistributable AME Wizard binary through an approved release channel. The installer now recognises the pinned AtlasOS v0.5.0 hotfix Playbook, verifies its official SHA-256, runs an available wizard visibly, and visibly skips Atlas when compatible inputs are unavailable.
- Wire the versioned setup state machine into the installer entry point and prove reboot continuation, retry, and cleanup behavior.
- Replace the remaining prototype modules with a version-aware direct system-patch engine. Every modified system component needs a pre-change backup, ownership record, validation, and tested rollback.
- Remove or rename inherited Rectify11 v3 payload paths and UI text that can misidentify Rectify12.
- Sign the installer, control panel, Settings package, and release metadata with the project release certificate.
- Run install, reboot/resume, repair, upgrade, uninstall, interrupted-install, and rollback tests on clean 22H2, 23H2, 24H2, and 25H2 virtual machines. The older targets remain experimental until this matrix passes.
- Verify Rectify11 v4 remains installed and functional after Rectify12 uninstall.
- Obtain and preserve redistribution attribution/terms for every bundled third-party asset.

Licence-bypass activation is not a release feature and must not be introduced. Optional Office installation, if added, must use official installation media and lawful Microsoft activation.
