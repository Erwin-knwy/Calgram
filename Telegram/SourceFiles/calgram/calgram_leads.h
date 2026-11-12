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

struct LeadRef {
	int64 id = 0; // 0 if the user can't open it.
	QString owner;
	bool mine = false;
	bool pool = false;
};

// The lead the label is about, once the CRM has answered.
[[nodiscard]] std::optional<LeadRef> LeadFor(
	not_null<const HistoryItem*> item);

// Asks the CRM again soon, e.g. after a claim.
void RefreshLead(not_null<const HistoryItem*> item);

// "Only my leads": office group posts that aren't the user's own, don't
// mention them, don't reply to them and aren't about their leads are
// drawn faded. Remembered between launches.
[[nodiscard]] bool OnlyMyLeads();
void SetOnlyMyLeads(bool enabled);

// Fades the item's drawing while it lives, when "Only my leads" says so.
class OnlyMyLeadsDimmer final {
public:
	OnlyMyLeadsDimmer(QPainter &p, not_null<const HistoryItem*> item);
	~OnlyMyLeadsDimmer();

private:
	QPainter &_p;
	double _opacity = 1.;

};

} // namespace Calgram
