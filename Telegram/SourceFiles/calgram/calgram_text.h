/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#pragma once

class HistoryItem;

namespace Calgram {

// Long office group posts show their first lines, the rest collapsed.
[[nodiscard]] TextWithEntities CollapseLongText(
	not_null<const HistoryItem*> item,
	TextWithEntities text);

} // namespace Calgram
