/*
This file is part of Calgram, a build of Telegram Desktop.
See CALGRAM.md.
*/
#pragma once

class HistoryItem;

namespace Calgram {

[[nodiscard]] bool IsOfficeChat(not_null<const HistoryItem*> item);

// "Lead: Hanna", "Lead: You", "Lead: Hanna, Ryan" for an office group
// message, empty until the CRM answers. Asks the CRM in batches.
[[nodiscard]] QString LeadOwnerLabel(not_null<const HistoryItem*> item);

// Opens that lead in the CRM, nullptr while there is no label.
[[nodiscard]] ClickHandlerPtr LeadLink(not_null<const HistoryItem*> item);

} // namespace Calgram
