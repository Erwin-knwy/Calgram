/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#pragma once

#include "data/data_peer_id.h"

#include <QtCore/QJsonDocument>

class HistoryItem;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Calgram {

inline constexpr auto kCrmUrl = "https://app.fruhlingswolke2.com/crm";

[[nodiscard]] PeerId OfficePeerId();
[[nodiscard]] bool IsOfficePeer(not_null<const PeerData*> peer);
[[nodiscard]] bool IsOfficeChat(not_null<const HistoryItem*> item);

// Calls the CRM's API as the signed-in Telegram user. An empty body means
// GET. status is 0 when there was no answer; json is the answer's body,
// errors included ({"detail": "..."}).
void CrmRequest(
	not_null<Main::Session*> session,
	const QString &path,
	QByteArray body,
	Fn<void(int status, QJsonDocument json)> done);

// Opens a CRM page, such as "/app/lead/12", in the browser.
void OpenCrmPage(const QString &path);

// The CRM's error text from an answer, or a fallback.
[[nodiscard]] QString CrmError(int status, const QJsonDocument &json);

} // namespace Calgram
