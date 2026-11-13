/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#pragma once

class HistoryItem;

namespace Ui {
class PopupMenu;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Calgram {

// The agent's unanswered mentions and wrong emails; a click jumps there.
void ShowOpenItems(not_null<Window::SessionController*> controller);

// Main menu: "My open items (3)" and the "Only my leads" switch.
void AddMainMenuItems(
	not_null<Ui::VerticalLayout*> menu,
	not_null<Window::SessionController*> controller);

// Right-click on a message: View Thread on any reply in a group; in the
// office group also reply "Edited" to the bot's Wrong Email Address post
// and search or copy the numbers in it.
void FillMessageMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

} // namespace Calgram
