# Third-party components

| Component | Where | License |
| --- | --- | --- |
| [scrcpy](https://github.com/Genymobile/scrcpy) server v4.1 | `assets/scrcpy-server` (unmodified binary) | Apache-2.0 |
| [tvhp](https://github.com/ps5-payload-dev/tvhp) RmlUi/SDL backend | `src/RmlUi/` | GPL-3.0 (derived from RmlUi's MIT backends) |
| [websrv](https://github.com/ps5-payload-dev/websrv) homebrew loader | `third_party/websrv/` | GPL-3.0-or-later |
| [Lato](https://www.latofonts.com/) | `fonts/` | SIL OFL 1.1 |

The home screen shortcut installer (in `src/launcher.c`) follows the technique used by
[ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) and
[ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager).
