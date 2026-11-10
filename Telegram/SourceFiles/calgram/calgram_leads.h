/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#pragma once

class HistoryItem;

namespace Calgram {

// The label next to an office group message's sender, such as
// "Lead: You · Interested · Follow-up Nov 12" or "Email: Sent".
// Empty until the CRM answers; asked in batches, refreshed every 5 min.
[[nodiscard]] QString LeadOwnerLabel(not_null<const HistoryItem*> item);

// Opens that lead in the CRM, nullptr while there is no label.
[[nodiscard]] ClickHandlerPtr LeadLink(not_null<const HistoryItem*> item);

} // namespace Calgram
