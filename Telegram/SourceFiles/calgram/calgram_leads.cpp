/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#include "calgram/calgram_leads.h"

#include "base/timer.h"
#include "calgram/calgram_crm.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "ui/click_handler.h"
#include "window/window_session_controller.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>

namespace Calgram {
namespace {

constexpr auto kBatchDelay = crl::time(300);
constexpr auto kRetryDelay = crl::time(45 * 1000);
constexpr auto kRefreshAfter = crl::time(5 * 60 * 1000);
constexpr auto kMaxBatch = 300;
constexpr auto kDimmedOpacity = 0.35;

struct Lead {
	int64 id = 0; // 0 if the viewer can't open it.
	QString owner;
	QString status;
	QString followUp;
	bool mine = false;
	bool pool = false;
};

struct Info {
	QString label;
	Lead lead;
	crl::time at = 0;
	bool mine = false;
};

// The CRM's POST /api/calgram/owners answers
// { "owners": { "<msg id>": [ owner, ... ] }, "unknown": [ <msg id> ],
//   "email": { "<msg id>": "Sent" } }, an owner being
// { "name", "mine", "pool", "lead_id", "status", "follow_up" }.
// "email", "status" and "follow_up" are optional.
class Leads final : public base::has_weak_ptr {
public:
	explicit Leads(not_null<Main::Session*> session)
	: _session(session)
	, _batchTimer([=] { send(); })
	, _retryTimer([=] { retry(); }) {
	}

	QString label(MsgId id) {
		if (!IsServerMsgId(id)) {
			return {};
		}
		const auto i = _info.find(id);
		const auto stale = (i == end(_info))
			|| (crl::now() - i->second.at > kRefreshAfter);
		if (stale && !_inflight.contains(id) && !_failed.contains(id)) {
			_pending.emplace(id);
			if (!_batchTimer.isActive()) {
				_batchTimer.callOnce(kBatchDelay);
			}
		}
		return (i != end(_info)) ? i->second.label : QString();
	}

	std::optional<LeadRef> lead(MsgId id) const {
		const auto i = _info.find(id);
		if (i == end(_info) || i->second.lead.owner.isEmpty()) {
			return std::nullopt;
		}
		const auto &lead = i->second.lead;
		return LeadRef{
			.id = lead.id,
			.owner = lead.owner,
			.mine = lead.mine,
			.pool = lead.pool,
		};
	}

	void refresh(MsgId id) {
		if (const auto i = _info.find(id); i != end(_info)) {
			i->second.at = 0;
		}
		if (!_inflight.contains(id)) {
			_failed.remove(id);
			_pending.emplace(id);
			if (!_batchTimer.isActive()) {
				_batchTimer.callOnce(kBatchDelay);
			}
		}
	}

	// std::nullopt until the CRM has answered for this message.
	std::optional<bool> mine(MsgId id) const {
		const auto i = _info.find(id);
		return (i != end(_info))
			? std::make_optional(i->second.mine)
			: std::nullopt;
	}

	ClickHandlerPtr link(MsgId id) {
		const auto i = _info.find(id);
		if (i == end(_info) || i->second.lead.owner.isEmpty()) {
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
		const auto i = _info.find(id);
		if (i == end(_info)) {
			return;
		}
		const auto &lead = i->second.lead;
		if (lead.id) {
			OpenCrmPage(u"/app/lead/%1"_q.arg(lead.id));
		} else if (const auto controller
				= HistoryView::ExtractController(context)) {
			controller->showToast(
				lead.owner + u"'s lead - only they can open it."_q);
		}
	}

	void send() {
		if (_pending.empty()) {
			return;
		}
		auto ids = std::vector<MsgId>();
		auto array = QJsonArray();
		for (const auto id : _pending) {
			ids.push_back(id);
			array.push_back(QJsonValue(qint64(id.bare)));
			if (int(ids.size()) >= kMaxBatch) {
				break;
			}
		}
		for (const auto id : ids) {
			_pending.remove(id);
			_inflight.emplace(id);
		}
		if (!_pending.empty()) {
			_batchTimer.callOnce(kBatchDelay);
		}
		const auto body = QJsonDocument(
			QJsonObject{ { u"message_ids"_q, array } }
		).toJson(QJsonDocument::Compact);
		CrmRequest(
			_session,
			u"/api/calgram/owners"_q,
			body,
			crl::guard(this, [=](int status, QJsonDocument json) {
				for (const auto id : ids) {
					_inflight.remove(id);
				}
				if (status != 200) {
					failed(ids);
				} else {
					apply(json.object(), ids);
				}
			}));
	}

	void failed(const std::vector<MsgId> &ids) {
		for (const auto id : ids) {
			_failed.emplace(id);
		}
		if (!_retryTimer.isActive()) {
			_retryTimer.callOnce(kRetryDelay);
		}
	}

	void retry() {
		for (const auto id : base::take(_failed)) {
			_pending.emplace(id);
		}
		send();
	}

	void apply(const QJsonObject &json, const std::vector<MsgId> &asked) {
		const auto owners = json.value(u"owners"_q).toObject();
		const auto emails = json.value(u"email"_q).toObject();
		auto unknown = std::vector<MsgId>();
		for (const auto &value : json.value(u"unknown"_q).toArray()) {
			unknown.push_back(MsgId(qint64(value.toDouble())));
		}
		const auto now = crl::now();
		for (const auto id : asked) {
			if (ranges::contains(unknown, id)) {
				continue;
			}
			const auto key = QString::number(id.bare);
			auto names = QStringList();
			auto lead = Lead();
			auto anyMine = false;
			for (const auto &value : owners.value(key).toArray()) {
				const auto owner = value.toObject();
				const auto mine = owner.value(u"mine"_q).toBool();
				anyMine |= mine;
				const auto name = owner.value(u"name"_q).toString();
				names.push_back(mine ? u"You"_q : name);
				const auto leadId = int64(
					owner.value(u"lead_id"_q).toDouble());
				if (names.size() == 1 || (mine && leadId)) {
					lead = {
						.id = leadId,
						.owner = name,
						.status = owner.value(u"status"_q).toString(),
						.followUp = owner.value(u"follow_up"_q).toString(),
						.mine = mine,
						.pool = owner.value(u"pool"_q).toBool(),
					};
				}
			}
			auto parts = QStringList();
			if (!names.isEmpty()) {
				parts.push_back(u"Lead: "_q + ((names.size() > 2)
					? (names.mid(0, 2).join(u", "_q)
						+ u" +%1"_q.arg(names.size() - 2))
					: names.join(u", "_q)));
				if (!lead.status.isEmpty()) {
					parts.push_back(lead.status);
				}
				if (!lead.followUp.isEmpty()) {
					parts.push_back(u"Follow-up "_q + lead.followUp);
				}
			}
			if (const auto email = emails.value(key).toString()
				; !email.isEmpty()) {
				parts.push_back(u"Email: "_q + email);
			}
			auto &info = _info[id];
			const auto label = parts.join(u" · "_q);
			const auto changed = (info.label != label);
			info = {
				.label = label,
				.lead = lead,
				.at = now,
				.mine = anyMine,
			};
			if (changed) {
				if (const auto item = _session->data().message(
						OfficePeerId(),
						id)) {
					_session->data().requestItemResize(item);
				}
			}
		}
		if (!unknown.empty()) {
			failed(unknown);
		}
	}

	const not_null<Main::Session*> _session;
	base::Timer _batchTimer;
	base::Timer _retryTimer;
	base::flat_map<MsgId, Info> _info;
	base::flat_map<MsgId, ClickHandlerPtr> _links;
	base::flat_set<MsgId> _pending;
	base::flat_set<MsgId> _inflight;
	base::flat_set<MsgId> _failed;

};

[[nodiscard]] Leads *LeadsFor(not_null<const HistoryItem*> item) {
	static auto All = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<Leads>>();
	if (!IsOfficeChat(item)) {
		return nullptr;
	}
	const auto session = &item->history()->session();
	auto i = All.find(session);
	if (i == end(All)) {
		i = All.emplace(session, std::make_unique<Leads>(session)).first;
		session->lifetime().add([=] { All.remove(session); });
	}
	return i->second.get();
}

[[nodiscard]] QSettings &Settings() {
	static QSettings result(u"California Recycles"_q, u"Calgram"_q);
	return result;
}

[[nodiscard]] bool &OnlyMyLeadsValue() {
	static auto result = Settings().value(u"only_my_leads"_q).toBool();
	return result;
}

[[nodiscard]] bool RepliesToMe(not_null<const HistoryItem*> item) {
	const auto reply = item->Get<HistoryMessageReply>();
	const auto to = reply ? reply->resolvedMessage.get() : nullptr;
	return to && to->out();
}

[[nodiscard]] bool Dimmed(not_null<const HistoryItem*> item) {
	if (!OnlyMyLeadsValue() || !item->isRegular() || item->out()) {
		return false;
	} else if (item->mentionsMe() || RepliesToMe(item)) {
		return false;
	}
	const auto leads = LeadsFor(item);
	const auto mine = leads ? leads->mine(item->id) : std::nullopt;
	return leads && mine.has_value() && !*mine;
}

} // namespace

bool OnlyMyLeads() {
	return OnlyMyLeadsValue();
}

void SetOnlyMyLeads(bool enabled) {
	OnlyMyLeadsValue() = enabled;
	Settings().setValue(u"only_my_leads"_q, enabled);
}

OnlyMyLeadsDimmer::OnlyMyLeadsDimmer(
	QPainter &p,
	not_null<const HistoryItem*> item)
: _p(p)
, _opacity(p.opacity()) {
	if (Dimmed(item)) {
		_p.setOpacity(_opacity * kDimmedOpacity);
	}
}

OnlyMyLeadsDimmer::~OnlyMyLeadsDimmer() {
	_p.setOpacity(_opacity);
}

std::optional<LeadRef> LeadFor(not_null<const HistoryItem*> item) {
	const auto leads = LeadsFor(item);
	return leads ? leads->lead(item->id) : std::nullopt;
}

void RefreshLead(not_null<const HistoryItem*> item) {
	if (const auto leads = LeadsFor(item)) {
		leads->refresh(item->id);
	}
}

QString LeadOwnerLabel(not_null<const HistoryItem*> item) {
	const auto leads = LeadsFor(item);
	return leads ? leads->label(item->id) : QString();
}

ClickHandlerPtr LeadLink(not_null<const HistoryItem*> item) {
	const auto leads = LeadsFor(item);
	return leads ? leads->link(item->id) : nullptr;
}

} // namespace Calgram
