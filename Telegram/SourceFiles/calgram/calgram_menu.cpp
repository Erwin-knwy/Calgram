/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#include "calgram/calgram_menu.h"

#include "calgram/calgram_compose.h"
#include "calgram/calgram_crm.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
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

} // namespace

void ShowOpenItems(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto state = std::make_shared<OpenItems>();
	const auto weak = base::make_weak(controller);
	const auto done = [=] {
		if (++state->answers < 2) {
			return;
		} else if (const auto strong = weak.get()) {
			strong->show(Box(OpenItemsBox, strong, *state));
		}
	};
	CrmRequest(session, u"/api/agent/unanswered-mentions"_q, {}, [=](
			int status,
			QJsonDocument json) {
		state->failed |= (status != 200);
		state->mentions = ParseItems(json, true);
		done();
	});
	CrmRequest(session, u"/api/agent/wrong-emails"_q, {}, [=](
			int status,
			QJsonDocument json) {
		state->failed |= (status != 200);
		state->wrong = ParseItems(json, false);
		done();
	});
}

void FillMessageMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	if (!IsOfficeChat(item) || !item->isRegular()) {
		return;
	}
	if (IsWrongEmailPost(item)) {
		const auto history = item->history();
		const auto id = item->id;
		menu->addAction(u"Reply \"Edited\""_q, [=] {
			PrefillReply(controller, history, id, u"Edited"_q);
		}, &st::menuIconReply);
	}
	const auto numbers = FindPhoneNumbers(item->originalText().text);
	const auto count = std::min(int(numbers.size()), kMaxNumbersInMenu);
	for (auto i = 0; i != count; ++i) {
		const auto number = numbers[i];
		const auto shown = u"(%1) %2-%3"_q.arg(
			number.mid(0, 3),
			number.mid(3, 3),
			number.mid(6));
		menu->addAction(u"Search %1 in CRM"_q.arg(shown), [=] {
			OpenCrmPage(u"/app/search-lead?number="_q + number);
		}, &st::menuIconSearch);
		menu->addAction(u"Copy %1"_q.arg(number), [=] {
			QGuiApplication::clipboard()->setText(number);
			controller->showToast(u"Copied "_q + number);
		}, &st::menuIconCopy);
	}
}

} // namespace Calgram
