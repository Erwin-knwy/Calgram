/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA

// used in Updater.cpp and Setup.iss for Windows
// Calgram: its own identity, so it installs next to official Telegram
// without sharing its data folder (%APPDATA%\<AppName>), installer id
// or startup shortcut (<AppFile>.lnk).
constexpr auto AppId = "{CFCE92AB-B46B-4AC3-9706-3845304A2728}"_cs;
constexpr auto AppNameOld = "Calgram (Unofficial)"_cs;
constexpr auto AppName = "Calgram"_cs;
constexpr auto AppFile = "Calgram"_cs;
constexpr auto AppVersion = 7004001;
constexpr auto AppVersionStr = "7.4.1";
constexpr auto AppBetaVersion = false;
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;
