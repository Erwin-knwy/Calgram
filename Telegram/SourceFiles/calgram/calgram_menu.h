/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#pragma once

class HistoryItem;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Calgram {

// The agent's unanswered mentions and wrong emails; a click jumps there.
void ShowOpenItems(not_null<Window::SessionController*> controller);

// Right-click on an office group message: reply "Edited" to the bot's
// Wrong Email Address post, search or copy the numbers in it.
void FillMessageMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

} // namespace Calgram
