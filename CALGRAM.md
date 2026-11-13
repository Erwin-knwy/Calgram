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
| Calgram icon (a white chat bubble with green recycle arrows on a blue circle; our own design, not Telegram's logo) | `Resources/art/icon256.ico`, `icon256.png`, `logo_256*.png`, `icon_round512@2x.png` |
| Wallet hidden from the main menu | `window/window_main_menu.cpp` |
| `tg://calgram_post?channel=1890609315&reply=<msg>&text=<text>`: opens the office group with the text typed as a reply to that message; the agent presses Send (for the CRM's "Post in Calgram") | `calgram/calgram_compose.*`, `core/local_url_handlers.cpp` |
| Signs in to the CRM automatically with the bot web app's Telegram-signed data (no login) and calls its API | `calgram/calgram_crm.*` |
| "Lead: Hanna" / "Lead: You" / "Lead: Pool" next to the sender on every office-group message, plus the lead's status, follow-up date and the `#email` request's status once the CRM sends them (`POST /api/calgram/owners`, refreshed every 5 minutes) | `calgram/calgram_leads.*`, `history/view/history_view_message.cpp` |
| Click on "Lead: ..." opens that lead in the CRM (an agent's own, a pool lead, or any lead for an admin); someone else's lead shows whose it is | `calgram/calgram_leads.*`, `history/view/history_view_message.cpp` |
| Long office-group posts (emails, lead posts) show their first 3 lines; the rest (300+ characters) is a collapsed quote that opens with a click | `calgram/calgram_text.*`, `history/view/history_view_element.cpp` |
| Before a post with a phone number goes out in the office group, asks "(818) 555-1234 is Hanna's lead. Send anyway?" if the CRM says so; waits at most 2 seconds, never blocks | `calgram/calgram_compose.*`, `history/history_widget.cpp`, `history/view/history_view_chat_section.cpp` |
| Typing `#` in the office group suggests the script tags (`#general`, own `#name_script`, `#email`, `#text`, `#DNC_list`...) | `calgram/calgram_compose.*`, `chat_helpers/field_autocomplete.cpp` |
| Main menu "My open items": unanswered mentions and wrong emails; a click jumps to the message to clear it | `calgram/calgram_menu.*`, `window/window_main_menu.cpp` |
| Right-click the bot's "Wrong Email Address" post: "Reply \"Edited\"" types the reply; the agent presses Send | `calgram/calgram_menu.*`, `history/history_inner_widget.cpp`, `history/view/history_view_context_menu.cpp` |
| Right-click a message with phone numbers: "Search (818) 555-1234 in CRM" and "Copy 8185551234" (first two numbers) | same as above |
| "View Thread" on any reply in a group (right-click), opening the whole discussion at that reply | `calgram/calgram_menu.*`, `history/history_inner_widget.cpp` |
| Right-click in the office group: "Lead details" (a card from `GET /api/leads/<id>`), "Claim this lead" on pool leads (`POST /api/leads/pool/<id>/claim`, same rules as the Leads Pool), "Email request" (types `#email #general`, the address and a greeting as a reply; the agent picks the script and sends), "Remind me..." (`POST /api/agent/followups`; the bot DMs the message's link then) | `calgram/calgram_menu.*` |
| Main menu: "My open items (3)" shows the count; "Only my leads" fades office posts that aren't yours, don't mention you, don't reply to you and aren't about your leads (remembered between launches) | `calgram/calgram_menu.*`, `calgram/calgram_leads.*`, `window/window_main_menu.cpp`, `history/history_inner_widget.cpp` |
| Auto-update and crash reports off (build settings) | `.github/workflows/calgram-windows.yml` |

## CRM endpoints Calgram uses

All in the CRM repo (`mini_search/app.py`, rules in `core/calgram_owners.py`),
live since CRM commit 5d6538c (2026-11-13):

- `POST /api/calgram/owners` - "Lead: ..." labels, with the lead's status,
  next follow-up and the `#email` request's status.
- `POST /api/calgram/numbers` - the "Send anyway?" question: another
  agent's number, a Do Not Call number, another agent's lead at the
  post's address. Managers get DNC warnings only.
- `GET /api/calgram/script-tags` - `#email`, approved private and `#text`
  script tags for the `#` suggestions.
- `/crm/app/search-lead?q=<number>` - Search Lead with the number filled
  in (`?number=` works too, for builds before 2026-11-13).
- Existing agent endpoints: `/api/agent/unanswered-mentions`,
  `/api/agent/wrong-emails`, `/api/leads/<id>`,
  `/api/leads/pool/<id>/claim`, `/api/agent/followups`.

## Branches

- `dev` - Telegram's code, untouched. Only ever updated from Telegram.
- `calgram` - our app: `dev` plus the changes above. The default branch.

## Building

GitHub builds Calgram for free: **Actions -> Calgram Windows -> Run
workflow** - or just push: a push to `calgram` builds **Release** (what
agents get), a push to a `feature/...` branch builds Debug. The result is `Calgram.exe` under
the run's *Artifacts*. The first build compiles all of Telegram's
libraries and can take longer than GitHub's 6-hour limit; just re-run it,
and it continues where it stopped.

GitHub's free Windows machines need four workarounds, all in the
workflow - keep them: MSYS2's `pacman-key` reads a `CONFIG` variable (ours
is `CALGRAM_CONFIG`); Release installs ATL for the v14.44 toolset
(breakpad); the libraries stay at Telegram's C: path (the cache records
it) while Calgram's own `out` folder is a junction to D: (C: has ~28 GB);
and a 16-32 GB page file on D: (the Release compile runs out of memory
otherwise). A Release build takes about 2.5 hours once libraries are
cached, Debug about 70 minutes.

Before giving a build to agents:
1. Set the repo secrets `CALGRAM_API_ID` and `CALGRAM_API_HASH` (from
   my.telegram.org -> API development tools). Without them the build uses
   Telegram's test keys, and logins may fail.
2. Build with **Release**.

## Proving a download

Every build is signed by GitHub with *build provenance*: a record that
this exact `Calgram.exe` was built by this repo's workflow from one
commit of the public code. Nobody's computer is involved, ours included.
To check a copy:
1. In PowerShell: `Get-FileHash Calgram.exe` gives its SHA256
   fingerprint (the download also has it in `Calgram.exe.sha256.txt`).
2. On GitHub: **Actions -> Attestations** lists every signed build with
   its fingerprint and commit. The two fingerprints must match.
3. Or, with the GitHub CLI: `gh attestation verify Calgram.exe -R
   Erwin-knwy/Calgram`.

Builds from before 2026-11-10 (733cb75 and older) have no attestation.

## Taking a Telegram update

1. Update `dev` from Telegram (GitHub's "Sync fork" on the `dev` branch).
2. Merge `dev` into `calgram`; fix any conflict in the files listed above.
3. Push `calgram`; GitHub builds it.

Telegram's own workflows (`win.yml`, `mac.yml`...) are disabled in this
repo's Actions settings. Keep them disabled.
