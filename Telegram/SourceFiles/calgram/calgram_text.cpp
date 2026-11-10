/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#include "calgram/calgram_text.h"

#include "calgram/calgram_crm.h"
#include "history/history_item.h"

namespace Calgram {
namespace {

constexpr auto kHeadLines = 3;
constexpr auto kMinHiddenChars = 300;

[[nodiscard]] bool IsSeparator(QStringView line) {
	const auto trimmed = line.trimmed();
	return (trimmed.size() >= 3)
		&& ranges::all_of(trimmed, [](QChar ch) {
			return (ch == '-')
				|| (ch == '_')
				|| (ch == '=')
				|| (ch == QChar(0x2014))
				|| (ch == QChar(0x2013));
		});
}

// Where the hidden part starts: after the first few lines, plus the
// value of a "Subject:" left alone on its line, plus a ----- line.
[[nodiscard]] int HiddenStart(const QString &text) {
	auto shown = 0;
	auto position = 0;
	auto wantValue = false;
	while (position < text.size()) {
		auto end = text.indexOf('\n', position);
		if (end < 0) {
			end = text.size();
		}
		const auto line = QStringView(text).mid(position, end - position);
		const auto trimmed = line.trimmed();
		if (!trimmed.isEmpty()) {
			if (shown >= kHeadLines && !wantValue && !IsSeparator(line)) {
				return position;
			}
			++shown;
			wantValue = trimmed.endsWith(':');
		}
		position = end + 1;
	}
	return -1;
}

} // namespace

TextWithEntities CollapseLongText(
		not_null<const HistoryItem*> item,
		TextWithEntities text) {
	if (!IsOfficeChat(item)) {
		return text;
	}
	const auto start = HiddenStart(text.text);
	if (start < 0) {
		return text;
	}
	auto end = int(text.text.size());
	while (end > start && text.text[end - 1].isSpace()) {
		--end;
	}
	const auto hidden = QStringView(text.text).mid(start, end - start);
	if (hidden.size() < kMinHiddenChars) {
		return text;
	}
	auto insertAt = text.entities.size();
	for (auto i = 0; i != text.entities.size(); ++i) {
		const auto &entity = text.entities[i];
		const auto from = entity.offset();
		const auto till = from + entity.length();
		if (entity.type() == EntityType::Blockquote
			|| entity.type() == EntityType::Pre
			|| (from < start && till > start)) {
			return text;
		} else if (from >= start && insertAt == text.entities.size()) {
			insertAt = i;
		}
	}
	text.entities.insert(insertAt, EntityInText(
		EntityType::Blockquote,
		start,
		end - start,
		u"1"_q)); // Collapsed.
	return text;
}

} // namespace Calgram
