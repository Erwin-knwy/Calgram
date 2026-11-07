# Calgram

Calgram is California Recycles' own build of Telegram Desktop for the
office's Windows laptops: Telegram as it is, plus a few small changes for
our work and our CRM. It is an unofficial app that uses the Telegram API.
It is not made or endorsed by Telegram.

## Rules we keep

- **Change as little as possible.** Every change makes Telegram updates
  harder. Prefer hiding a feature to deleting its code. Mark every change
  with a `// Calgram:` comment so `git grep "Calgram:"` lists them all.
- **Telegram's API terms**: the name must not contain "Telegram", no
  Telegram logo, no ghost mode (hiding online/read/typing), and nothing
  sent for an agent without them doing it.
- **Never put secrets in the code.** The repo is public. The api_id and
  api_hash live in the repo's Actions secrets only.

## What we changed

| Change | Files |
|---|---|
| Own name, installer id, data folder (`%APPDATA%\Calgram`) and startup shortcut, so it runs next to official Telegram | `core/version.h` |
| Own taskbar group and notifications | `platform/win/windows_app_user_model_id.cpp` |
| Windows file details (company, product name) | `Resources/winrc/Telegram.rc` |
| Calgram icon (the office recycle icon) | `Resources/art/icon256.ico`, `icon256.png`, `logo_256*.png`, `icon_round512@2x.png` |
| "View Thread" on every reply in a group, not only on the first message | `history/history_inner_widget.cpp` |
| Wallet hidden from the main menu | `window/window_main_menu.cpp` |
| Auto-update and crash reports off (build settings) | `.github/workflows/calgram-windows.yml` |

## Branches

- `dev` - Telegram's code, untouched. Only ever updated from Telegram.
- `calgram` - our app: `dev` plus the changes above. The default branch.

## Building

GitHub builds Calgram for free: **Actions -> Calgram Windows -> Run
workflow** (or any push to `calgram`). The result is `Calgram.exe` under
the run's *Artifacts*. The first build compiles all of Telegram's
libraries and can take longer than GitHub's 6-hour limit; just re-run it,
and it continues where it stopped.

Before giving a build to agents:
1. Set the repo secrets `CALGRAM_API_ID` and `CALGRAM_API_HASH` (from
   my.telegram.org -> API development tools). Without them the build uses
   Telegram's test keys, and logins may fail.
2. Build with **Release**.

## Taking a Telegram update

1. Update `dev` from Telegram (GitHub's "Sync fork" on the `dev` branch).
2. Merge `dev` into `calgram`; fix any conflict in the files listed above.
3. Push `calgram`; GitHub builds it.

Telegram's own workflows (`win.yml`, `mac.yml`...) are disabled in this
repo's Actions settings. Keep them disabled.
