/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#include "calgram/calgram_crm.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QDesktopServices>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Calgram {
namespace {

constexpr auto kOfficeChannelId = uint64(1890609315);
constexpr auto kSignInMaxAge = crl::time(20 * 3600 * 1000); // CRM: 24 h
constexpr auto kTimeout = 15 * 1000;

struct Pending {
	QString path;
	QByteArray body;
	Fn<void(int, QJsonDocument)> done;
	bool retried = false;
};

// Signs in with the bot web app's initData ("tma" header).
class Client final : public base::has_weak_ptr {
public:
	explicit Client(not_null<Main::Session*> session)
	: _session(session) {
	}

	void request(Pending pending) {
		if (_initData.isEmpty()
			|| crl::now() - _initDataAt > kSignInMaxAge) {
			_waiting.push_back(std::move(pending));
			if (!_signingIn) {
				signIn();
			}
			return;
		}
		send(std::move(pending));
	}

private:
	void send(Pending pending) {
		auto http = QNetworkRequest(
			QUrl(QString::fromLatin1(kCrmUrl) + pending.path));
		http.setRawHeader("User-Agent", "Calgram");
		http.setRawHeader("Authorization", "tma " + _initData.toUtf8());
		http.setTransferTimeout(kTimeout);
		const auto get = pending.body.isEmpty();
		if (!get) {
			http.setHeader(
				QNetworkRequest::ContentTypeHeader,
				u"application/json"_q);
		}
		const auto reply = get
			? _manager.get(http)
			: _manager.post(http, pending.body);
		QObject::connect(reply, &QNetworkReply::finished, &_manager, [=,
				pending = std::move(pending)]() mutable {
			reply->deleteLater();
			const auto status = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (status == 401 && !pending.retried) {
				_initData = QString();
				pending.retried = true;
				request(std::move(pending));
				return;
			} else if (status != 200) {
				LOG(("Calgram: CRM %1 answered %2."
					).arg(pending.path
					).arg(status));
			}
			pending.done(
				status,
				(status == 200)
					? QJsonDocument::fromJson(reply->readAll())
					: QJsonDocument());
		});
	}

	void signedIn(const QString &initData) {
		_signingIn = false;
		_initData = initData;
		_initDataAt = crl::now();
		for (auto &pending : base::take(_waiting)) {
			send(std::move(pending));
		}
	}

	void signInFailed() {
		_signingIn = false;
		for (auto &pending : base::take(_waiting)) {
			pending.done(0, QJsonDocument());
		}
	}

	void signIn() {
		_signingIn = true;
		const auto username = u"CalRecycles_Bot"_q;
		if (const auto peer = _session->data().peerByUsername(username)) {
			if (const auto bot = peer->asUser()) {
				requestWebAppData(bot);
				return;
			}
		}
		_session->api().request(MTPcontacts_ResolveUsername(
			MTP_flags(MTPcontacts_ResolveUsername::Flag(0)),
			MTP_string(username),
			MTP_string()
		)).done(crl::guard(this, [=](const MTPcontacts_ResolvedPeer &result) {
			result.match([&](const MTPDcontacts_resolvedPeer &data) {
				_session->data().processUsers(data.vusers());
				_session->data().processChats(data.vchats());
			});
			const auto peer = _session->data().peerByUsername(username);
			if (const auto bot = peer ? peer->asUser() : nullptr) {
				requestWebAppData(bot);
			} else {
				signInFailed();
			}
		})).fail(crl::guard(this, [=](const MTP::Error &error) {
			LOG(("Calgram: bot resolve failed: %1").arg(error.type()));
			signInFailed();
		})).send();
	}

	void requestWebAppData(not_null<UserData*> bot) {
		_session->api().request(MTPmessages_RequestAppWebView(
			MTP_flags(MTPmessages_RequestAppWebView::Flag(0)),
			bot->input(),
			MTP_inputBotAppShortName(
				bot->inputUser(),
				MTP_string("CalRecycles_MiniApp")),
			MTP_string(),
			MTP_dataJSON(MTP_bytes(QByteArray())),
			MTP_string("tdesktop")
		)).done(crl::guard(this, [=](const MTPWebViewResult &result) {
			const auto url = QUrl(qs(result.data().vurl()));
			const auto data = QUrlQuery(
				url.fragment(QUrl::FullyEncoded)
			).queryItemValue(u"tgWebAppData"_q, QUrl::FullyDecoded);
			if (data.isEmpty()) {
				LOG(("Calgram: no tgWebAppData in the web app url."));
				signInFailed();
			} else {
				signedIn(data);
			}
		})).fail(crl::guard(this, [=](const MTP::Error &error) {
			LOG(("Calgram: web app sign-in failed: %1").arg(error.type()));
			signInFailed();
		})).send();
	}

	const not_null<Main::Session*> _session;
	QNetworkAccessManager _manager;
	std::vector<Pending> _waiting;
	QString _initData;
	crl::time _initDataAt = 0;
	bool _signingIn = false;

};

[[nodiscard]] Client &ClientFor(not_null<Main::Session*> session) {
	static auto All = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<Client>>();
	auto i = All.find(session);
	if (i == end(All)) {
		i = All.emplace(session, std::make_unique<Client>(session)).first;
		session->lifetime().add([=] { All.remove(session); });
	}
	return *i->second;
}

} // namespace

PeerId OfficePeerId() {
	return peerFromChannel(ChannelId(kOfficeChannelId));
}

bool IsOfficePeer(not_null<const PeerData*> peer) {
	return (peer->id == OfficePeerId());
}

bool IsOfficeChat(not_null<const HistoryItem*> item) {
	return IsOfficePeer(item->history()->peer);
}

void CrmRequest(
		not_null<Main::Session*> session,
		const QString &path,
		QByteArray body,
		Fn<void(int status, QJsonDocument json)> done) {
	ClientFor(session).request({
		.path = path,
		.body = std::move(body),
		.done = std::move(done),
	});
}

void OpenCrmPage(const QString &path) {
	QDesktopServices::openUrl(QUrl(QString::fromLatin1(kCrmUrl) + path));
}

} // namespace Calgram
