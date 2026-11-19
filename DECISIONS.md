# Design decisions

| # | Topic | Decision | Why |
|---|-------|----------|-----|
| 1 | Origin | Forked from Genesix by DSK (same GPLv3 codebase, namespace `gx` kept) | Reuse engine, tooling, CI, installer. Genesix itself is never modified. |
| 2 | Plugin codes | Manufacturer `Dskm`, plugin `Infm`, bundle `com.dskmusic.infinitum` | Frozen forever (DAW session compatibility). |
| 3 | Repo visibility | Private until first release | Author's choice. CI is manual-only (`workflow_dispatch`) and Linux is disabled because private-repo Actions minutes are metered. No public binaries while private (GPLv3: binaries must ship with source). |
| 4 | Commit e-mail | Repo-local GitHub noreply address from the first commit | The history becomes public when the repo is flipped. |
| 5 | Installer | New Inno Setup AppId | Must not upgrade/uninstall Genesix. |
| 6 | Export formats | WAV, FLAC, OGG (JUCE built-in); no MP3 | No extra encoder code/dependency. |
| 7 | Playback | Play/Stop button + follows DAW transport; evolution driven by seed + timeline position | DAW bounces match what was heard. |
