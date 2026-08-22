# Rectify12 setup contract

This document defines the intended installation and uninstallation behaviour for the Rectify12 foundation.

## Supported Windows releases

- Rectify12 accepts Windows 11 22H2 (build 22621), 23H2 (build 22631), 24H2 (build 26100), and 25H2 (build 26200).
- 22H2 and 23H2 are foundation/experimental targets until their direct-patch and rollback paths have completed dedicated runtime testing; 24H2 and 25H2 remain the primary targets.
- Installation must stop before any system modification when the current base build is outside the explicit set above. Future/unknown builds are not accepted automatically.
- Uninstallation must remain available even if Windows has subsequently moved to another build, so users can recover/remove Rectify12 after an OS upgrade.

## Required base

- Rectify11 v4 Alpha must already be installed before Rectify12 can patch the system.
- Rectify11 is an allowed prerequisite and must not be treated as a conflicting patched system.
- The prerequisite check should use Rectify11's uninstall registration and require a v4-or-newer alpha-compatible installation.

## Existing system modifications

- Before applying AtlasOS or Rectify12 patches, setup must look for signs of another pre-patched/custom Windows environment.
- When a conflicting patched environment is detected, setup must stop and ask the user to remove it before running Rectify12.
- Rectify11 is the explicit exception.
- Detection must favour reliable product/registry markers over vague heuristics to avoid false positives.

## AtlasOS / AME Wizard stage

- AtlasOS is applied as an AME Wizard Playbook; it is not a separate operating system installation.
- Rectify12 launches the AME Wizard stage as part of setup and supplies the AtlasOS Playbook.
- Required/recommended Rectify12 Atlas options should already be selected when the user reaches the AME Wizard option pages.
- AME Wizard remains visible to the user; this is not intended to be a hidden/headless Atlas installation.
- Automation must not make this stage silent: AME Wizard stays visible, the selected options remain reviewable, and progress, warnings, failures, and restart decisions are shown to the user.
- Rectify12 may prepare inputs and guide the visible flow, but it must not simulate acceptance of safety warnings or hide third-party licence/consent screens.
- Rectify12 must persist its setup state before any reboot initiated by this stage.

## Resume after reboot

- Setup is multi-stage and must continue automatically after Windows starts again.
- The resume mechanism must store a versioned setup-state record, register a one-shot continuation entry, and remove that entry once the resumed stage starts successfully.
- A failed or interrupted stage must not silently advance the state machine.

## System patching

- Rectify12 patches Windows system components directly.
- Rectify12 itself must not depend on Windhawk for its system patches.
- Because these are system-level changes, setup must display a prominent backup/data-loss warning before the first destructive stage.
- System files/settings modified by Rectify12 should have sufficient ownership/rollback metadata to allow safe removal.

## Drivers

- Rectify12 updates device drivers to the latest applicable versions as part of the automated setup flow.
- Driver update failures must be reported rather than silently ignored.
- Driver updates should happen only after the user has passed the compatibility/backup gate.

## Cursors

The canonical cursor asset supplied for Rectify12 is:

- Bundled archive: `assets/cursors/Rectify12-cursors-jepricreations.zip`
- Original upload name: `windows_11_cursors_concept_by_jepricreations_densjkc.zip`
- SHA-256: `04c9a4797f02ab88fd5df15a9377a32b3f66497f05caf89460f3441968a7024c`
- Author/provider: JepriCreations
- Included schemes: `W11 Cursor Light Free by Jepri Creations` and `W11 Cursor Dark Free by Jepri Creations`

The foundation build verifies this checksum before compiling and includes the exact archive in its artifact. Any later installer extraction/application stage must verify the same checksum before using the pack. The cursor pack's included licence/usage note requires clear credit to JepriCreations and a link to the author's DeviantArt page when redistributed.

Rectify12 should apply the light or dark cursor scheme to match the selected/current Windows app theme and preserve the previous cursor scheme for rollback.

## Office option

- Setup may offer an optional Microsoft Office installation stage.
- Rectify12 must not embed or automate licence-bypass/cracking activation. Activation should use a legitimate Microsoft account/product licence or another lawful activation path.

## Uninstallation

- Uninstalling Rectify12 removes only Rectify12-owned changes and restores the state Rectify12 backed up where possible.
- Uninstalling Rectify12 does **not** uninstall Rectify11 v4 Alpha.
- If the user also wants Rectify11 removed, they must uninstall Rectify11 separately after Rectify12 has been removed.
- Shared components must not be removed blindly when ownership cannot be proven.
