/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#include "calgram/calgram_compose.h"

#include "base/call_delayed.h"
#include "base/qthelp_url.h"
#include "calgram/calgram_crm.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "history/history.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

namespace Calgram {
namespace {

constexpr auto kCheckWait = crl::time(2000);
constexpr auto kUnsupportedFor = crl::time(3600 * 1000);
constexpr auto kTagsRefresh = crl::time(3600 * 1000);

// Same order as the CRM's core/phone_extractor.py.
[[nodiscard]] const std::vector<QRegularExpression> &Patterns() {
	static const auto result = std::vector<QRegularExpression>{
		QRegularExpression(
			u"(?<!\\d)\\((\\d{3})\\)\\s*(\\d{3})[-.\\s]?(\\d{4})(?!\\d)"_q),
		QRegularExpression(
			u"(?<!\\d)(\\d{3})\\)\\s*(\\d{3})[-.\\s]?(\\d{4})(?!\\d)"_q),
		QRegularExpression(
			u"(?<!\\d)\\+?1[-.\\s]?(\\d{3})[-.\\s]?(\\d{3})[-.\\s]?(\\d{4})(?!\\d)"_q),
		QRegularExpression(
			u"(?<!\\d)(\\d{3})[-.\\s](\\d{3})[-.\\s](\\d{4})(?!\\d)"_q),
		QRegularExpression(u"(?<!\\d)(\\d{3})(\\d{3})(\\d{4})(?!\\d)"_q),
	};
	return result;
}

[[nodiscard]] QString FormatNumber(const QString &digits) {
	return u"(%1) %2-%3"_q.arg(
		digits.mid(0, 3),
		digits.mid(3, 3),
		digits.mid(6));
}

struct NumberCheck {
	QString confirmedText;
	QString checkingText;
	crl::time unsupportedAt = 0;
};

[[nodiscard]] NumberCheck &NumberCheckFor(not_null<Main::Session*> session) {
	static auto All = base::flat_map<not_null<Main::Session*>, NumberCheck>();
	auto i = All.find(session);
	if (i == end(All)) {
		i = All.emplace(session, NumberCheck()).first;
		session->lifetime().add([=] { All.remove(session); });
	}
	return i->second;
}

struct ScriptTags {
	std::vector<QString> list;
	crl::time loadedAt = 0;
	bool loading = false;
};

[[nodiscard]] ScriptTags &ScriptTagsFor(not_null<Main::Session*> session) {
	static auto All = base::flat_map<not_null<Main::Session*>, ScriptTags>();
	auto i = All.find(session);
	if (i == end(All)) {
		i = All.emplace(session, ScriptTags()).first;
		session->lifetime().add([=] { All.remove(session); });
	}
	return i->second;
}

// The group's own tags, always offered.
[[nodiscard]] std::vector<QString> FixedTags() {
	return {
		u"email"_q,
		u"text"_q,
		u"DNC_list"_q,
		u"ReceivedCall"_q,
		u"MissedCall"_q,
	};
}

void AddTags(
		std::vector<QString> &to,
		const QJsonDocument &json,
		const QString &field) {
	static const auto word = QRegularExpression(u"^\\w+$"_q);
	for (const auto &value : json.object().value(u"scripts"_q).toArray()) {
		auto tag = value.toObject().value(field).toString().trimmed();
		if (tag.startsWith('#')) {
			tag = tag.mid(1);
		}
		const auto known = ranges::any_of(to, [&](const QString &other) {
			return !other.compare(tag, Qt::CaseInsensitive);
		});
		if (word.match(tag).hasMatch() && !known) {
			to.push_back(tag);
		}
	}
}

void LoadScriptTags(not_null<Main::Session*> session) {
	auto &tags = ScriptTagsFor(session);
	if (tags.loading
		|| (tags.loadedAt && crl::now() - tags.loadedAt < kTagsRefresh)) {
		return;
	}
	tags.loading = true;
	const auto weak = base::make_weak(session);
	const auto finish = [=](std::vector<QString> list) {
		if (const auto strong = weak.get()) {
			auto &loaded = ScriptTagsFor(strong);
			loaded.list = std::move(list);
			loaded.loadedAt = crl::now();
			loaded.loading = false;
		}
	};
	// One list with every #email and #text script, when the CRM has it.
	CrmRequest(session, u"/api/calgram/script-tags"_q, {}, [=](
			int status,
			QJsonDocument json) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		} else if (status == 200) {
			auto list = FixedTags();
			AddTags(list, json, u"tag"_q);
			finish(std::move(list));
			return;
		}
		// Until then, the email scripts agents can already see.
		CrmRequest(strong, u"/api/agent/scripts"_q, {}, [=](
				int,
				QJsonDocument company) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			CrmRequest(
				strong,
				u"/api/agent/private-scripts?status=approved"_q,
				{},
				[=](int, QJsonDocument own) {
					auto list = FixedTags();
					AddTags(list, company, u"name"_q);
					AddTags(list, own, u"tag"_q);
					finish(std::move(list));
				});
		});
	});
}

} // namespace

std::vector<QString> FindPhoneNumbers(const QString &text) {
	static const auto url = QRegularExpression(u"https?://\\S+"_q);
	auto cleaned = text;
	cleaned.replace(url, u" "_q);

	auto result = std::vector<QString>();
	auto claimed = std::vector<std::pair<int, int>>();
	for (const auto &pattern : Patterns()) {
		auto i = pattern.globalMatch(cleaned);
		while (i.hasNext()) {
			const auto match = i.next();
			const auto start = int(match.capturedStart());
			const auto end = int(match.capturedEnd());
			const auto overlaps = ranges::any_of(claimed, [&](auto span) {
				return (start < span.second) && (span.first < end);
			});
			if (overlaps) {
				continue;
			}
			const auto number = match.captured(1)
				+ match.captured(2)
				+ match.captured(3);
			if (number.size() != 10) {
				continue;
			}
			claimed.emplace_back(start, end);
			// The office group's id, which t.me links put in the text.
			if (number != u"1890609315"_q
				&& !ranges::contains(result, number)) {
				result.push_back(number);
			}
		}
	}
	return result;
}

void PrefillReply(
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		MsgId replyTo,
		const QString &text) {
	const auto peerId = history->peer->id;
	auto cursor = MessageCursor();
	cursor.position = cursor.anchor = int(text.size());
	history->setLocalDraft(std::make_unique<Data::Draft>(
		TextWithTags{ text, {} },
		FullReplyTo{
			.messageId = (IsServerMsgId(replyTo)
				? FullMsgId(peerId, replyTo)
				: FullMsgId()),
		},
		SuggestOptions(),
		cursor,
		Data::WebPageDraft()));
	history->clearLocalEditDraft(MsgId(), PeerId());
	history->session().changes().entryUpdated(
		history,
		Data::EntryUpdate::Flag::LocalDraftSet);
	controller->showPeerHistory(
		history,
		Window::SectionShow::Way::ClearStack,
		ShowAtUnreadMsgId);
	controller->window().activate();
}

bool HandlePostLink(
		not_null<Window::SessionController*> controller,
		const QString &query) {
	const auto params = qthelp::url_parse_params(
		query,
		qthelp::UrlParamNameTransform::ToLower);
	const auto peerId = peerFromChannel(
		ChannelId(params.value(u"channel"_q).toULongLong()));
	const auto replyTo = MsgId(params.value(u"reply"_q).toLongLong());
	const auto text = params.value(u"text"_q);
	if (peerId != OfficePeerId() || text.isEmpty()) {
		return false;
	}
	const auto peer = controller->session().data().peerLoaded(peerId);
	if (!peer) {
		controller->showToast(
			u"Open the office group in Calgram first, then try again."_q);
		return true;
	}
	PrefillReply(controller, peer->owner().history(peer), replyTo, text);
	return true;
}

bool HoldForNumberCheck(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const QString &text,
		Fn<void()> send) {
	if (!IsOfficePeer(peer)) {
		return false;
	}
	const auto session = &peer->session();
	auto &check = NumberCheckFor(session);
	if (!check.confirmedText.isEmpty() && check.confirmedText == text) {
		check.confirmedText = QString();
		return false;
	} else if (check.unsupportedAt
		&& crl::now() - check.unsupportedAt < kUnsupportedFor) {
		return false;
	} else if (!check.checkingText.isEmpty() && check.checkingText == text) {
		return true; // Send pressed again while we ask.
	}
	const auto numbers = FindPhoneNumbers(text);
	if (numbers.empty()) {
		return false;
	}
	check.checkingText = text;

	const auto answered = std::make_shared<bool>(false);
	const auto weak = base::make_weak(controller);
	const auto go = [=] {
		if (const auto strong = weak.get()) {
			auto &state = NumberCheckFor(&strong->session());
			state.checkingText = QString();
			state.confirmedText = text;
			send();
		}
	};
	base::call_delayed(kCheckWait, controller.get(), [=] {
		if (!*answered) {
			*answered = true;
			go();
		}
	});

	auto array = QJsonArray();
	for (const auto &number : numbers) {
		array.push_back(number);
	}
	const auto body = QJsonDocument(
		QJsonObject{ { u"numbers"_q, array } }
	).toJson(QJsonDocument::Compact);
	CrmRequest(session, u"/api/calgram/numbers"_q, body, [=](
			int status,
			QJsonDocument json) {
		if (*answered) {
			return;
		}
		*answered = true;
		const auto strong = weak.get();
		if (!strong) {
			return;
		} else if (status == 404) {
			NumberCheckFor(&strong->session()).unsupportedAt = crl::now();
		}
		auto lines = QStringList();
		const auto warnings = json.object().value(u"warnings"_q).toArray();
		for (const auto &value : warnings) {
			const auto warning = value.toObject();
			lines.push_back(u"%1 is %2's lead."_q.arg(
				FormatNumber(warning.value(u"number"_q).toString()),
				warning.value(u"owner"_q).toString()));
		}
		if (lines.isEmpty()) {
			go();
			return;
		}
		NumberCheckFor(&strong->session()).checkingText = QString();
		strong->show(Ui::MakeConfirmBox({
			.text = lines.join('\n') + u"\n\nSend anyway?"_q,
			.confirmed = [=](Fn<void()> close) {
				close();
				go();
			},
			.confirmText = u"Send anyway"_q,
		}));
	});
	return true;
}

void AppendScriptTags(
		ChannelData *channel,
		const QString &filter,
		std::vector<QString> &rows) {
	if (!channel || !IsOfficePeer(channel)) {
		return;
	}
	const auto session = &channel->session();
	LoadScriptTags(session);
	for (const auto &tag : ScriptTagsFor(session).list) {
		if (!filter.isEmpty()
			&& (tag.size() == filter.size()
				|| !tag.startsWith(filter, Qt::CaseInsensitive))) {
			continue;
		} else if (!ranges::contains(rows, tag)) {
			rows.push_back(tag);
		}
	}
}

} // namespace Calgram
