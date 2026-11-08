/*
Calgram - California Recycles' build of Telegram Desktop.
Not part of Telegram Desktop; see CALGRAM.md.
*/
#pragma once

class HistoryItem;

namespace Calgram {

// "Lead: Hanna" (or "Lead: You", "Lead: Pool", "Lead: Hanna, Ryan") for a
// message in the office group: the owners of the CRM leads whose numbers
// are in that message's thread. Empty for other chats, while the CRM
// hasn't answered yet, and for threads without a lead. Asking is cheap:
// the CRM is queried in batches in the background, and the message is
// re-laid out when its answer arrives.
[[nodiscard]] QString LeadOwnerLabel(not_null<const HistoryItem*> item);

} // namespace Calgram
