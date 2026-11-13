/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#include "calgram/calgram_menu.h"

#include "calgram/calgram_compose.h"
#include "calgram/calgram_crm.h"
#include "calgram/calgram_leads.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Calgram {
namespace {

constexpr auto kMaxNumbersInMenu = 2;

struct OpenItem {
	QString text;
	MsgId jumpTo;
};

struct OpenItems {
	std::vector<OpenItem> mentions;
	std::vector<OpenItem> wrong;
	int answers = 0;
	bool failed = false;
};

// GET /api/agent/unanswered-mentions and /api/agent/wrong-emails answer
// { "items": [ { "msg_id", "verdict_id"?, "date", "mentioned_by"?,
// "excerpt"?, "detail"? } ] }, newest first.
[[nodiscard]] std::vector<OpenItem> ParseItems(
		const QJsonDocument &json,
		bool mentions) {
	auto result = std::vector<OpenItem>();
	for (const auto &value : json.object().value(u"items"_q).toArray()) {
		const auto item = value.toObject();
		const auto date = item.value(u"date"_q).toString();
		const auto verdict = int64(item.value(u"verdict_id"_q).toDouble());
		const auto message = int64(item.value(u"msg_id"_q).toDouble());
		const auto text = mentions
			? (item.value(u"mentioned_by"_q).toString()
				+ u" mentioned you, "_q
				+ date
				+ u": "_q
				+ item.value(u"excerpt"_q).toString())
			: (u"Wrong email, "_q
				+ date
				+ u": "_q
				+ item.value(u"detail"_q).toString());
		result.push_back({
			.text = text,
			.jumpTo = MsgId(verdict ? verdict : message),
		});
	}
	return result;
}

void OpenItemsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const OpenItems &items) {
	box->setTitle(u"My open items"_q);
	box->setWidth(st::boxWideWidth);
	const auto addText = [&](const QString &text) {
		box->addRow(object_ptr<Ui::FlatLabel>(box, text, st::boxLabel));
	};
	const auto addList = [&](
			const QString &title,
			const std::vector<OpenItem> &list) {
		if (list.empty()) {
			return;
		}
		addText(title);
		for (const auto &item : list) {
			const auto button = box->verticalLayout()->add(
				object_ptr<Ui::SettingsButton>(
					box,
					rpl::single(item.text)));
			const auto jumpTo = item.jumpTo;
			button->setClickedCallback([=] {
				box->closeBox();
				controller->showPeerHistory(
					OfficePeerId(),
					Window::SectionShow::Way::ClearStack,
					jumpTo);
			});
		}
	};
	if (items.failed) {
		addText(u"Couldn't reach the CRM. Try again in a minute."_q);
	} else if (items.mentions.empty() && items.wrong.empty()) {
		addText(u"Nothing open."_q);
	} else {
		addList(u"Unanswered mentions"_q, items.mentions);
		addList(u"Wrong emails"_q, items.wrong);
		addText(u"Clear each one in the group: reply, or react."_q);
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

[[nodiscard]] bool IsWrongEmailPost(not_null<HistoryItem*> item) {
	const auto user = item->from()->asUser();
	return user
		&& user->isBot()
		&& item->originalText().text.contains(u"Wrong Email Address"_q);
}

void LoadOpenItems(
		not_null<Main::Session*> session,
		Fn<void(const OpenItems&)> done) {
	const auto state = std::make_shared<OpenItems>();
	const auto check = [=] {
		if (++state->answers == 2) {
			done(*state);
		}
	};
	CrmRequest(session, u"/api/agent/unanswered-mentions"_q, {}, [=](
			int status,
			QJsonDocument json) {
		state->failed |= (status != 200);
		state->mentions = ParseItems(json, true);
		check();
	});
	CrmRequest(session, u"/api/agent/wrong-emails"_q, {}, [=](
			int status,
			QJsonDocument json) {
		state->failed |= (status != 200);
		state->wrong = ParseItems(json, false);
		check();
	});
}


[[nodiscard]] QString FormatPhone(const QString &digits) {
	return (digits.size() == 10)
		? u"(%1) %2-%3"_q.arg(digits.mid(0, 3), digits.mid(3, 3), digits.mid(6))
		: digits;
}

[[nodiscard]] QString MessageLink(not_null<HistoryItem*> item) {
	return u"https://t.me/c/%1/%2"_q.arg(
		peerToChannel(item->history()->peer->id).bare
	).arg(item->id.bare);
}

[[nodiscard]] QString Pretty(QString key) {
	key.replace('_', ' ');
	return key.isEmpty() ? key : (key.left(1).toUpper() + key.mid(1));
}

// GET /api/leads/<id>: the lead page's own data.
void LeadCardBox(
		not_null<Ui::GenericBox*> box,
		int64 leadId,
		QJsonObject lead) {
	box->setTitle(lead.value(u"business_name"_q).toString());
	box->setWidth(st::boxWideWidth);
	auto phones = QStringList();
	for (const auto &value : lead.value(u"phones"_q).toArray()) {
		const auto number = value.isObject()
			? value.toObject().value(u"number"_q).toString()
			: value.toString();
		if (!number.isEmpty()) {
			phones.push_back(FormatPhone(number));
		}
	}
	const auto place = QStringList{
		lead.value(u"address"_q).toString(),
		lead.value(u"city"_q).toString(),
		lead.value(u"zip"_q).toString(),
	}.filter(QRegularExpression(u"\\S"_q)).join(u", "_q);
	const auto rows = std::vector<std::pair<QString, QString>>{
		{ u"Contact"_q, lead.value(u"contact_name"_q).toString() },
		{ u"Phones"_q, phones.join(u", "_q) },
		{ u"Email"_q, lead.value(u"email"_q).toString() },
		{ u"Address"_q, place },
		{ u"Industry"_q, lead.value(u"industry"_q).toString() },
		{ u"Territory"_q, lead.value(u"territory_code"_q).toString() },
		{ u"Status"_q, Pretty(lead.value(u"status"_q).toString()) },
		{ u"Last call"_q, Pretty(lead.value(u"last_call_result"_q).toString()) },
		{ u"Notes"_q, lead.value(u"notes"_q).toString().left(300) },
	};
	for (const auto &[label, value] : rows) {
		if (value.trimmed().isEmpty()) {
			continue;
		}
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			label + u": "_q + value,
			st::boxLabel));
	}
	box->addButton(rpl::single(u"Open in CRM"_q), [=] {
		OpenCrmPage(u"/app/lead/%1"_q.arg(leadId));
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

void ShowLeadCard(
		not_null<Window::SessionController*> controller,
		int64 leadId) {
	const auto weak = base::make_weak(controller);
	CrmRequest(
		&controller->session(),
		u"/api/leads/%1"_q.arg(leadId),
		{},
		[=](int status, QJsonDocument json) {
			const auto strong = weak.get();
			if (!strong) {
				return;
			} else if (status != 200) {
				strong->showToast(CrmError(status, json));
				return;
			}
			strong->show(Box(LeadCardBox, leadId, json.object()));
		});
}

void ClaimLead(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		int64 leadId) {
	const auto weak = base::make_weak(controller);
	const auto history = item->history();
	const auto id = item->id;
	controller->show(Ui::MakeConfirmBox({
		.text = u"Claim this lead from the Leads Pool? First click wins."_q,
		.confirmed = [=](Fn<void()> close) {
			close();
			CrmRequest(
				&history->session(),
				u"/api/leads/pool/%1/claim"_q.arg(leadId),
				QByteArray("{}"),
				[=](int status, QJsonDocument json) {
					const auto strong = weak.get();
					if (!strong) {
						return;
					}
					strong->showToast((status == 200)
						? u"Claimed. It's in your leads now."_q
						: CrmError(status, json));
					if (const auto item = history->owner().message(
							history->peer->id,
							id)) {
						RefreshLead(item);
					}
				});
		},
		.confirmText = u"Claim"_q,
	}));
}

void PrefillEmailRequest(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		const QString &email,
		const QString &contact) {
	const auto first = contact.trimmed().split(' ').value(0);
	const auto greeting = first.isEmpty()
		? u"Hello,"_q
		: (u"Dear "_q + first + ',');
	PrefillReply(
		controller,
		item->history(),
		item->id,
		u"#email #general\n\n"_q + email + '\n' + greeting);
}

void StartEmailRequest(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		int64 leadId) {
	static const auto regex = QRegularExpression(
		u"[A-Za-z0-9._%+\\-]+@[A-Za-z0-9.\\-]+\\.[A-Za-z]{2,}"_q);
	const auto match = regex.match(item->originalText().text);
	if (match.hasMatch()) {
		PrefillEmailRequest(controller, item, match.captured(0), QString());
		return;
	} else if (!leadId) {
		controller->showToast(u"No email address in this post."_q);
		return;
	}
	const auto weak = base::make_weak(controller);
	const auto history = item->history();
	const auto id = item->id;
	CrmRequest(
		&history->session(),
		u"/api/leads/%1"_q.arg(leadId),
		{},
		[=](int status, QJsonDocument json) {
			const auto strong = weak.get();
			const auto item = history->owner().message(history->peer->id, id);
			if (!strong || !item) {
				return;
			}
			const auto lead = json.object();
			const auto email = lead.value(u"email"_q).toString().trimmed();
			if (status != 200) {
				strong->showToast(CrmError(status, json));
			} else if (email.isEmpty()) {
				strong->showToast(u"This lead has no email address."_q);
			} else {
				PrefillEmailRequest(
					strong,
					item,
					email,
					lead.value(u"contact_name"_q).toString());
			}
		});
}

// POST /api/agent/followups: the same parser as the bot and the CRM.
void RemindBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		QString note) {
	box->setTitle(u"Remind me"_q);
	box->setWidth(st::boxWideWidth);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		u"When? The bot will DM you this message's link then."_q,
		st::boxLabel));
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(u"tomorrow at 10, in 3 days, next Tuesday at 2"_q)));
	const auto error = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		QString(),
		st::boxLabel));
	error->hide();
	box->setFocusCallback([=] { field->setFocusFast(); });
	const auto weak = base::make_weak(controller);
	const auto submit = [=] {
		const auto when = field->getLastText().trimmed();
		if (when.isEmpty()) {
			field->showError();
			return;
		}
		const auto body = QJsonDocument(QJsonObject{
			{ u"text"_q, when + ' ' + note },
		}).toJson(QJsonDocument::Compact);
		CrmRequest(
			&controller->session(),
			u"/api/agent/followups"_q,
			body,
			crl::guard(box.get(), [=](int status, QJsonDocument json) {
				if (status == 200) {
					if (const auto strong = weak.get()) {
						strong->showToast(u"Reminder set: "_q
							+ json.object().value(
								u"date_label"_q).toString());
					}
					box->closeBox();
				} else {
					error->setText(CrmError(status, json));
					error->show();
					field->showError();
				}
			}));
	};
	field->submits(
	) | rpl::on_next([=](Qt::KeyboardModifiers) {
		submit();
	}, field->lifetime());
	box->addButton(rpl::single(u"Remind me"_q), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

// The CRM's Looks, made by tools/calgram_themes.py; the CRM's own first
// mode for each comes first.
struct LookTheme {
	const char *key;
	const char *name;
	bool darkFirst = false;
};
constexpr auto kLooks = std::array<LookTheme, 7>{ {
	{ "modern", "Modern", false },
	{ "classic", "Classic", false },
	{ "cyberpunk", "Cyberpunk", true },
	{ "gtavi", "GTA VI", true },
	{ "kurosawa", "Kurosawa", false },
	{ "ca", "CA", false },
	{ "lumiere", "Lumiere", true },
} };

void LooksBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(u"Look"_q);
	box->setWidth(st::boxWideWidth);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		u"The CRM's Looks. Telegram's own: Settings > Chat Settings."_q,
		st::boxLabel));
	for (const auto &look : kLooks) {
		for (const auto dark : { look.darkFirst, !look.darkFirst }) {
			const auto mode = dark ? u"dark"_q : u"light"_q;
			const auto path = u":/calgram/%1-%2.tdesktop-theme"_q.arg(
				QString::fromLatin1(look.key),
				mode);
			const auto button = box->verticalLayout()->add(
				object_ptr<Ui::SettingsButton>(
					box,
					rpl::single(QString::fromLatin1(look.name)
						+ u", "_q
						+ mode)));
			button->setClickedCallback([=] {
				box->closeBox();
				Window::Theme::Apply(path);
			});
		}
	}
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void ShowOpenItems(not_null<Window::SessionController*> controller) {
	const auto weak = base::make_weak(controller);
	LoadOpenItems(&controller->session(), [=](const OpenItems &items) {
		if (const auto strong = weak.get()) {
			strong->show(Box(OpenItemsBox, strong, items));
		}
	});
}

void AddMainMenuItems(
		not_null<Ui::VerticalLayout*> menu,
		not_null<Window::SessionController*> controller) {
	const auto label = menu->lifetime().make_state<rpl::variable<QString>>(
		u"My open items"_q);
	const auto open = Settings::AddButtonWithIcon(
		menu,
		label->value(),
		st::mainMenuButton,
		{ &st::menuIconNotifications });
	open->setClickedCallback([=] {
		ShowOpenItems(controller);
	});
	LoadOpenItems(&controller->session(), crl::guard(open.get(), [=](
			const OpenItems &items) {
		const auto count = int(items.mentions.size() + items.wrong.size());
		if (!items.failed && count > 0) {
			*label = u"My open items (%1)"_q.arg(count);
		}
	}));

	Settings::AddButtonWithIcon(
		menu,
		rpl::single(u"Look"_q),
		st::mainMenuButton,
		{ &st::menuIconPalette }
	)->setClickedCallback([=] {
		controller->show(Box(LooksBox));
	});

	const auto mine = Settings::AddButtonWithIcon(
		menu,
		rpl::single(u"Only my leads"_q),
		st::mainMenuButton,
		{ &st::menuIconProfile });
	mine->toggleOn(rpl::single(OnlyMyLeads()));
	mine->toggledChanges(
	) | rpl::on_next([=](bool enabled) {
		SetOnlyMyLeads(enabled);
		controller->widget()->update();
	}, mine->lifetime());
}

void FillMessageMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	if (!item->isRegular()) {
		return;
	}
	const auto history = item->history();
	if (history->peer->isMegagroup() && !history->isForum()) {
		const auto top = item->replyToTop();
		const auto root = top ? top : item->replyToId();
		LOG(("Calgram: menu for %1, reply %2, top %3."
			).arg(item->id.bare
			).arg(item->replyToId().bare
			).arg(top.bare));
		if (root && root != item->id) {
			const auto id = item->id;
			menu->addAction(u"View Thread"_q, [=] {
				controller->showRepliesForMessage(history, root, id);
			}, &st::menuIconViewReplies);
		}
	}
	if (!IsOfficeChat(item)) {
		return;
	}
	if (IsWrongEmailPost(item)) {
		const auto id = item->id;
		menu->addAction(u"Reply \"Edited\""_q, [=] {
			PrefillReply(controller, history, id, u"Edited"_q);
		}, &st::menuIconReply);
	}
	const auto itemId = item->fullId();
	if (const auto lead = LeadFor(item)) {
		const auto leadId = lead->id;
		if (leadId) {
			menu->addAction(u"Lead details"_q, [=] {
				ShowLeadCard(controller, leadId);
			}, &st::menuIconInfo);
		}
		if (lead->pool && leadId) {
			menu->addAction(u"Claim this lead"_q, [=] {
				if (const auto item = history->owner().message(itemId)) {
					ClaimLead(controller, item, leadId);
				}
			}, &st::menuIconAddToFolder);
		}
	}
	{
		const auto lead = LeadFor(item);
		const auto leadId = lead ? lead->id : int64(0);
		menu->addAction(u"Email request"_q, [=] {
			if (const auto item = history->owner().message(itemId)) {
				StartEmailRequest(controller, item, leadId);
			}
		}, &st::menuIconEdit);
		const auto excerpt = item->originalText().text.simplified().left(80);
		const auto note = excerpt + ' ' + MessageLink(item);
		menu->addAction(u"Remind me..."_q, [=] {
			controller->show(Box(RemindBox, controller, note));
		}, &st::menuIconSchedule);
	}
	const auto numbers = FindPhoneNumbers(item->originalText().text);
	const auto count = std::min(int(numbers.size()), kMaxNumbersInMenu);
	for (auto i = 0; i != count; ++i) {
		const auto number = numbers[i];
		const auto shown = FormatPhone(number);
		menu->addAction(u"Search %1 in CRM"_q.arg(shown), [=] {
			OpenCrmPage(u"/app/search-lead?q="_q + number);
		}, &st::menuIconSearch);
		menu->addAction(u"Copy %1"_q.arg(number), [=] {
			QGuiApplication::clipboard()->setText(number);
			controller->showToast(u"Copied "_q + number);
		}, &st::menuIconCopy);
	}
}

} // namespace Calgram
