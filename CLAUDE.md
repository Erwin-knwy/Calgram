# Calgram

This repo is **Calgram**, California Recycles' own build of Telegram
Desktop for the office's Windows laptops: Telegram as it is, plus a few
small changes for our work and our CRM. Read `CALGRAM.md` first - what we
changed, how it builds, how to take Telegram updates.

## Rules that must not be broken

- **Change as little of Telegram as possible.** Every change makes the
  next Telegram update harder. Hide rather than delete. New Calgram code
  goes in `Telegram/SourceFiles/calgram/`; in Telegram's own files, keep
  the change to a few lines marked `// Calgram:`. Hardcode English text
  instead of editing `lang.strings`. List every change in `CALGRAM.md`.
- **Telegram's API terms**: no "Telegram" in the name, no Telegram logo,
  no ghost mode (hiding online/read/typing), and nothing sent for an
  agent without them pressing Send.
- **The repo is public.** No secrets in code. The api_id/api_hash live
  only in the Actions secrets `CALGRAM_API_ID` / `CALGRAM_API_HASH`.
- **Windows x64 only**, built for free on GitHub Actions
  (`.github/workflows/calgram-windows.yml`). Nothing builds on Erwin's Mac
  (macOS 12) or his 8 GB Lenovo. A build takes about 70 minutes once the
  libraries are cached; check the result before stacking more changes.
  Telegram's own workflows stay disabled in the repo's Actions settings.
- **Branches**: `dev` is untouched Telegram, `calgram` is our app
  (default, every push builds). Work on `feature/...` branches and merge
  when ready. Never force-push.

## The CRM side

Calgram talks to the CRM (repo `Erwin-knwy/CalRecycles_Bot`, served at
`https://app.fruhlingswolke2.com/crm/`). It signs in with the Telegram-
signed initData of the bot's web app `CalRecycles_MiniApp` - no password -
through `Calgram::CrmRequest` (`calgram/calgram_crm.cpp`). The endpoints it
uses, and the CRM changes it is waiting for, are listed in `CALGRAM.md`.
CRM changes follow that repo's own CLAUDE.md and WORKFLOW.md: agents are
live 9 AM - 3 PM Pacific, restart only the changed service.

---

For Telegram Desktop's own code, read `AGENTS.md` and treat it as the
canonical repository-wide instructions.
