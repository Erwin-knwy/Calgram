/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#include "calgram/calgram_crm.h"

#include "apiwrap.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "ui/click_handler.h"
#include "window/window_session_controller.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QDesktopServices>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Calgram {
namespace {

constexpr auto kOfficeChannelId = uint64(1890609315);
constexpr auto kBatchDelay = crl::time(300);
constexpr auto kRetryDelay = crl::time(45 * 1000);
constexpr auto kSignInMaxAge = crl::time(20 * 3600 * 1000); // CRM: 24 h
constexpr auto kLeadUrl = "https://app.fruhlingswolke2.com/crm/app/lead/%1";
constexpr auto kMaxBatch = 300;

[[nodiscard]] PeerId OfficePeerId() {
	return peerFromChannel(ChannelId(kOfficeChannelId));
}

struct Lead {
	int64 id = 0; // 0 if the viewer can't open it.
	QString owner;
};

// Signs in with the bot web app's initData ("tma" header).
class Crm final : public base::has_weak_ptr {
public:
	explicit Crm(not_null<Main::Session*> session)
	: _session(session)
	, _batchTimer([=] { send(); })
	, _retryTimer([=] { retry(); }) {
	}

	QString label(not_null<const HistoryItem*> item) {
		const auto id = item->id;
		if (!IsServerMsgId(id)) {
			return {};
		}
		const auto i = _labels.find(id);
		if (i != end(_labels)) {
			return i->second;
		}
		if (!_inflight.contains(id) && !_retry.contains(id)) {
			_pending.emplace(id);
			if (!_batchTimer.isActive()) {
				_batchTimer.callOnce(kBatchDelay);
			}
		}
		return {};
	}

	ClickHandlerPtr link(MsgId id) {
		const auto i = _labels.find(id);
		if (i == end(_labels) || i->second.isEmpty()) {
			return nullptr;
		}
		auto &result = _links[id];
		if (!result) {
			const auto weak = base::make_weak(this);
			result = std::make_shared<LambdaClickHandler>([=](
					ClickContext context) {
				if (const auto strong = weak.get()) {
					strong->open(id, context);
				}
			});
		}
		return result;
	}

private:
	void open(MsgId id, const ClickContext &context) {
		const auto i = _leads.find(id);
		if (i == end(_leads)) {
			return;
		} else if (i->second.id) {
			QDesktopServices::openUrl(
				QUrl(QString(kLeadUrl).arg(i->second.id)));
		} else if (const auto controller
				= HistoryView::ExtractController(context)) {
			controller->showToast(
				i->second.owner + u"'s lead - only they can open it."_q);
		}
	}

	void send() {
		if (_pending.empty() || _signingIn) {
			return;
		}
		if (_initData.isEmpty() || crl::now() - _initDataAt > kSignInMaxAge) {
			signIn();
			return;
		}
		auto ids = std::vector<MsgId>();
		for (const auto id : _pending) {
			ids.push_back(id);
			if (int(ids.size()) >= kMaxBatch) {
				break;
			}
		}
		for (const auto id : ids) {
			_pending.remove(id);
			_inflight.emplace(id);
		}
		requestOwners(std::move(ids));
		if (!_pending.empty()) {
			_batchTimer.callOnce(kBatchDelay);
		}
	}

	void retry() {
		for (const auto id : _retry) {
			_pending.emplace(id);
		}
		_retry.clear();
		send();
	}

	void failLater() {
		for (const auto id : _pending) {
			_retry.emplace(id);
		}
		_pending.clear();
		if (!_retryTimer.isActive()) {
			_retryTimer.callOnce(kRetryDelay);
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
				_signingIn = false;
				failLater();
			}
		})).fail(crl::guard(this, [=](const MTP::Error &error) {
			LOG(("Calgram: bot resolve failed: %1").arg(error.type()));
			_signingIn = false;
			failLater();
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
			_signingIn = false;
			const auto url = QUrl(qs(result.data().vurl()));
			const auto data = QUrlQuery(
				url.fragment(QUrl::FullyEncoded)
			).queryItemValue(u"tgWebAppData"_q, QUrl::FullyDecoded);
			if (data.isEmpty()) {
				LOG(("Calgram: no tgWebAppData in the web app url."));
				failLater();
				return;
			}
			_initData = data;
			_initDataAt = crl::now();
			send();
		})).fail(crl::guard(this, [=](const MTP::Error &error) {
			LOG(("Calgram: web app sign-in failed: %1").arg(error.type()));
			_signingIn = false;
			failLater();
		})).send();
	}

	void requestOwners(std::vector<MsgId> ids) {
		auto array = QJsonArray();
		for (const auto id : ids) {
			array.push_back(QJsonValue(qint64(id.bare)));
		}
		const auto body = QJsonDocument(
			QJsonObject{ { u"message_ids"_q, array } }
		).toJson(QJsonDocument::Compact);
		auto request = QNetworkRequest(QUrl(
			u"https://app.fruhlingswolke2.com/crm/api/calgram/owners"_q));
		request.setHeader(
			QNetworkRequest::ContentTypeHeader,
			u"application/json"_q);
		request.setRawHeader("User-Agent", "Calgram");
		request.setRawHeader("Authorization", "tma " + _initData.toUtf8());
		const auto reply = _manager.post(request, body);
		QObject::connect(reply, &QNetworkReply::finished, &_manager, [=] {
			reply->deleteLater();
			for (const auto id : ids) {
				_inflight.remove(id);
			}
			const auto status = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (status == 401) {
				_initData = QString(); // sign in again, but not in a loop
			}
			if (status != 200) {
				LOG(("Calgram: CRM owners request failed: %1").arg(status));
				for (const auto id : ids) {
					_retry.emplace(id);
				}
				if (!_retryTimer.isActive()) {
					_retryTimer.callOnce(kRetryDelay);
				}
				return;
			}
			applyOwners(QJsonDocument::fromJson(reply->readAll()).object(), ids);
		});
	}

	void applyOwners(const QJsonObject &json, const std::vector<MsgId> &asked) {
		const auto owners = json.value(u"owners"_q).toObject();
		for (const auto &value : json.value(u"unknown"_q).toArray()) {
			_retry.emplace(MsgId(qint64(value.toDouble())));
		}
		for (const auto id : asked) {
			const auto key = QString::number(id.bare);
			if (!owners.contains(key)) {
				continue;
			}
			auto names = QStringList();
			auto lead = Lead();
			for (const auto &value : owners.value(key).toArray()) {
				const auto owner = value.toObject();
				const auto mine = owner.value(u"mine"_q).toBool();
				const auto name = owner.value(u"name"_q).toString();
				const auto leadId = int64(
					owner.value(u"lead_id"_q).toDouble());
				names.push_back(mine ? u"You"_q : name);
				if (names.size() == 1 || (mine && leadId)) {
					lead = { leadId, name };
				}
			}
			_leads[id] = lead;
			const auto shown = (names.size() > 2)
				? (names.mid(0, 2).join(u", "_q)
					+ u" +%1"_q.arg(names.size() - 2))
				: names.join(u", "_q);
			const auto label = shown.isEmpty()
				? QString()
				: (u"Lead: "_q + shown);
			_labels[id] = label;
			if (!label.isEmpty()) {
				if (const auto item = _session->data().message(
						OfficePeerId(),
						id)) {
					_session->data().requestItemResize(item);
				}
			}
		}
		if (!_retry.empty() && !_retryTimer.isActive()) {
			_retryTimer.callOnce(kRetryDelay);
		}
	}

	const not_null<Main::Session*> _session;
	QNetworkAccessManager _manager;
	base::Timer _batchTimer;
	base::Timer _retryTimer;
	base::flat_map<MsgId, QString> _labels;
	base::flat_map<MsgId, Lead> _leads;
	base::flat_map<MsgId, ClickHandlerPtr> _links;
	base::flat_set<MsgId> _pending;
	base::flat_set<MsgId> _inflight;
	base::flat_set<MsgId> _retry;
	QString _initData;
	crl::time _initDataAt = 0;
	bool _signingIn = false;

};

[[nodiscard]] Crm *CrmFor(not_null<const HistoryItem*> item) {
	static auto All = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<Crm>>();
	if (!IsOfficeChat(item)) {
		return nullptr;
	}
	const auto session = &item->history()->session();
	auto i = All.find(session);
	if (i == end(All)) {
		i = All.emplace(session, std::make_unique<Crm>(session)).first;
		session->lifetime().add([=] { All.remove(session); });
	}
	return i->second.get();
}

} // namespace

bool IsOfficeChat(not_null<const HistoryItem*> item) {
	return (item->history()->peer->id == OfficePeerId());
}

QString LeadOwnerLabel(not_null<const HistoryItem*> item) {
	const auto crm = CrmFor(item);
	return crm ? crm->label(item) : QString();
}

ClickHandlerPtr LeadLink(not_null<const HistoryItem*> item) {
	const auto crm = CrmFor(item);
	return crm ? crm->link(item->id) : nullptr;
}

} // namespace Calgram
