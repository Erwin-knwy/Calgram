/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#pragma once

class ChannelData;
class History;
class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Calgram {

// 10-digit numbers in the text, found the way the CRM finds them.
[[nodiscard]] std::vector<QString> FindPhoneNumbers(const QString &text);

// Puts text in the chat's message field as a reply. Nothing is sent.
void PrefillReply(
	not_null<Window::SessionController*> controller,
	not_null<History*> history,
	MsgId replyTo,
	const QString &text);

// tg://calgram_post?channel=<id>&reply=<msg id>&text=<text>, from the
// CRM: the text goes in the office group's field as a reply. Not sent.
bool HandlePostLink(
	not_null<Window::SessionController*> controller,
	const QString &query);

// Before a post in the office group goes out: if it has a number that is
// another agent's lead, asks first. Returns true when it took over and
// will call send() itself (at once, after a check, or if confirmed).
[[nodiscard]] bool HoldForNumberCheck(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const QString &text,
	Fn<void()> send);

// Adds the office's script tags to the hashtag suggestions.
void AppendScriptTags(
	ChannelData *channel,
	const QString &filter,
	std::vector<QString> &rows);

} // namespace Calgram
