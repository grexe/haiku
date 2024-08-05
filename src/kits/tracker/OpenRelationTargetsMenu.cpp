#define DEBUG 1

#include "Attributes.h"
#include "AutoLock.h"
#include "Commands.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "OpenRelationTargetsMenu.h"
#include "Sensei.h"
#include "MimeTypes.h"
#include "StopWatch.h"
#include "Tracker.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Debug.h>
#include <GroupView.h>
#include <GridView.h>
#include <Locale.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>
#include <SpaceLayoutItem.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

OpenRelationTargetsMenu::OpenRelationTargetsMenu(const char* label, const BMessage* entriesToOpen,
	BWindow* parentWindow, BHandler* target)
	:
	BSlowMenu(label),
	fEntriesToOpen(*entriesToOpen),
	target(target),
	fParentWindow(parentWindow),
	fRelationTargetsReply(new BMessage())
{
	InitIconPreloader();

	SetFont(be_plain_font);

	// too long to have triggers
	SetTriggersEnabled(false);
}


OpenRelationTargetsMenu::OpenRelationTargetsMenu(const char* label, const BMessage* entriesToOpen,
	BWindow* parentWindow, const BMessenger &messenger)
	:
	BSlowMenu(label),
	fEntriesToOpen(*entriesToOpen),
	target(NULL),
	fMessenger(messenger),
	fParentWindow(parentWindow),
	fRelationTargetsReply(new BMessage())
{
	InitIconPreloader();

	SetFont(be_plain_font);
	SetTriggersEnabled(false);
}

bool
OpenRelationTargetsMenu::StartBuildingItemList()
{
	switch(fEntriesToOpen.what) {
		case SENSEI_MESSAGE_TYPE:
		{
			PRINT(("self relations submenu: got SENSEI msg SENSEI_MESSAGE_TYPE, nothing to do.\n"));
			break;
		}
		case SENSEI_MESSAGE_RESULT:	// we are a sub menu of self relations
		{
			PRINT(("self relations submenu: got SENSEI_MESSAGE_RESULT: bailing out and building items from existing sub item msg...\n"));
			fRelationTargetsReply = &fEntriesToOpen;
			return true;
		}
		case SEN_RELATIONS_GET_SELF:
		{
			PRINT(("self relations submenu: got SEN_RELATIONS_GET_SELF: building items to submenu from relation reply...\n"));
			break;
		}
		default:
		{
			PRINT(("relations submenu: start building items...\n"));
		}
	}

    if (be_roster->IsRunning(SEN_SERVER_SIGNATURE)) {
		BMessenger fSenMessenger(SEN_SERVER_SIGNATURE);
		if (! fSenMessenger.IsValid()) {
			PRINT(("failed to set up messenger for SEN server!\n"));
			return false;
		}

		PRINT(("Tracker::OpenRelationTargets->SEN: getting relation targets..."));
        status_t result = fSenMessenger.SendMessage(new BMessage(fEntriesToOpen), fRelationTargetsReply);
		if (result != B_OK) {
			PRINT(("failed to communicate with SEN server: %s\n", strerror(result)));
			return false;
		}
		PRINT(("SEN->Tracker::OpenRelationTargets: received reply:\n"));
		if (DEBUG) fRelationTargetsReply->PrintToStream();

		return true;
    } else {
        PRINT(("failed to reach SEN server, is it running?\n"));
        return false;
    }
}

bool OpenRelationTargetsMenu::AddNextItem()
{
    // nothing to do here
    return false;
}

void OpenRelationTargetsMenu::ClearMenuBuildingState()
{
    //  empty;
    return;
}

void
OpenRelationTargetsMenu::DoneBuildingItemList()
{
	// target the menu
	if (target != NULL)
		SetTargetForItems(target);
	else
		SetTargetForItems(fMessenger);

	uint32 targets = 0;
	status_t result;

	if (DEBUG) {
		PRINT(("fRelationTargetsReply is now:\n"));
		fRelationTargetsReply->PrintToStream();
	}
	switch(fRelationTargetsReply->what) {
		case SEN_RELATIONS_GET_SELF:
		{
			PRINT(("SEN_RELATIONS_GET_SELF: adding self relation targets...\n"));
			// resolve self relations via plugin and cascade targets menu
			result = AddSelfRelationTargetItems(&targets);
			break;
		}
		case SENSEI_MESSAGE_RESULT:		// handle sub node of self reresult
		{
			PRINT(("SEN_RELATIONS_MESSAGE_RESULT: adding self relation sub targets...\n"));
			// resolve self relations via plugin and cascade targets menu
			result = AddSelfRelationTargetItems(&targets);
			break;
		}
		case SEN_RESULT_RELATIONS:
		{
			PRINT(("relation RESULT: adding relation targets...\n"));
			result = AddRelationTargetItems(&targets);
			break;
		}
		default:
		{
			PRINT(("relation RESULT: unsupported message type %u\n", fRelationTargetsReply->what));
			result = B_ERROR;
			break;
		}
	}
	if (result != B_OK) {
		PRINT(("error buiding relations menu: %s\n", strerror(result) ));
		return;
	}

	if (targets == 0) {
		PRINT(("no relation targets added.\n"));
		BMenuItem* item = new BMenuItem("no targets found.", 0);
		item->SetEnabled(false);
		AddItem(item);
	} else {
		PRINT(("%u targets added.\n", targets));
	}
}

status_t
OpenRelationTargetsMenu::AddRelationTargetItems(uint32* targetCount)
{
	entry_ref ref;
	BEntry entry;
	BPath path;
	int32 index = 0;
	status_t result;

	PRINT(("adding relation menu target items...\n"));

	while ((result = fRelationTargetsReply->FindRef("refs", index, &ref)) == B_OK) {
		entry.SetTo(&ref);
		entry.GetPath(&path);

		ModelMenuItem* item = new ModelMenuItem(new Model(&ref, true, true), path.Leaf(), NULL);
		AddItem(item);

		index++;
		(*targetCount)++;
	}
	if (result != B_NAME_NOT_FOUND) {
		PRINT(("failed to resolve refs: %s\n", strerror(result)));
		return result;
	}

	return B_OK;
}

status_t
OpenRelationTargetsMenu::AddSelfRelationTargetItems(uint32* targetCount)
{
	int32 index = 0;
	status_t result;
	BMessage itemMsg, childMsg;
	BString label, type;

	PRINT(("adding self relation menu target items...\n"));

	// get root node
	result = fRelationTargetsReply->FindMessage("item", &itemMsg);
	if (result != B_OK) {
		PRINT(("failed to build self relations menu, could not find root node, unexpected message reply.\n"));
		return result;
	}

	// get ref to self from refs received (source of relation)
	// Note: we expect only 1 ref for self relations, but let's keep it consistent across all relation types
	entry_ref ref;
	result = fRelationTargetsReply->FindRef("refs", &ref);
	if (result != B_OK) {
		PRINT(("failed to resolve self relation to source: %s\n", strerror(result)));
		return result;
	}

	PRINT(("iterating through self relation target message...\n"));

	// first add any sub menus, 1st level only as we build slow menus that expand further on demand
	while ((result = GetItemMessageInfo(&itemMsg, &childMsg, &label, &type, index)) == B_OK) {
		if (result != B_OK) {
			PRINT(("failed to build self relations menu, aborting.\n"));
			return result;
		}
		IconMenuItem* item;
		// add as menu if there is a child node, else add as a plain menu item
		if (childMsg.IsEmpty()) {
			// create new self relation item with self ref msg
			// todo: copy all properties of this item
			BMessage openSelfMsg(SEN_OPEN_SELF_RELATION);
			openSelfMsg.AddString("label", label.String());
			openSelfMsg.AddString("type", type.String());

			PRINT(("adding self relation item [%d] '%s' of type '%s'.\n", index, label.String(), type.String() ));
			item = new IconMenuItem(label.String(), new BMessage(openSelfMsg), type.String());
		} else {
			PRINT(("adding self relation menu [%d] '%s' of type '%s'.\n", index, label.String(), type.String() ));
			// transparently handle just like a normal message result above, but for the subtree
			childMsg.what = SENSEI_MESSAGE_RESULT;
			childMsg.AddRef("refs", new entry_ref(ref));
			childMsg.PrintToStream();

			BMessage openRelationTargetsMsg(SEN_OPEN_RELATION_TARGET_VIEW);
			openRelationTargetsMsg.AddString(SEN_RELATION_LABEL, label);
			openRelationTargetsMsg.AddString(SEN_RELATION_TYPE, type.String());
			// add all properties from this item (e.g. page, position,...)
			openRelationTargetsMsg.Append(childMsg);

			item = new IconMenuItem(
				new OpenRelationTargetsMenu(label.String(), new BMessage(childMsg), fParentWindow, be_app_messenger),
				new BMessage(openRelationTargetsMsg),
				type.String()	// this is the MIME type of the relation, so take its icon
			);

			childMsg.MakeEmpty();
		}
		//item->SetTarget(be_app_messenger);
		AddItem(item);

		index++;
		(*targetCount)++;
	}
	return B_OK;
}

status_t OpenRelationTargetsMenu::GetItemMessageInfo(BMessage* itemMsg, BMessage* childMsg, BString* label, BString* type, int32 index) {
	// get label
	status_t result = itemMsg->FindString("label", index, label);
	if (result != B_OK) {
		PRINT(("failed to get item label [%d]: %s\n", index, strerror(result) ));
		// still allow to continue and fall back to default label - more for debugging
		label->SetTo("item #");
		*label << index;
	}
	// get type
	result = itemMsg->FindString("type", index, type);
	if (result != B_OK) {
		if (result == B_NAME_NOT_FOUND) {
			// this is valid if there is a default type
			BString defaultType;
			result = fRelationTargetsReply->FindString(SENSEI_SELF_DEFAULT_TYPE_KEY, &defaultType);
			if (result == B_OK) {
				type->SetTo(defaultType.String());
			} else {
				if (result != B_NAME_NOT_FOUND) {
					PRINT(("internal error, default type lookup failed: %s\n", strerror(result) ));
				} else {
					PRINT(("no type specified and no defaultType provided, falling back to generic self reference.\n"));
					type->SetTo("relation/x-vnd.sen-labs.relation.reference");
					result = B_OK;
				}
			}
		} else {
			PRINT(("failed to get item info: %s\n", strerror(result) ));
		}
	}
	// get optional child node message (empty or another node)
	BMessage subItemMsg;
	result = itemMsg->FindMessage("item", index, &subItemMsg);
	if (result != B_OK) {
		if (result != B_NAME_NOT_FOUND) {
			PRINT(("error looking for child node in message: %s\n", strerror(result)));
		}
	} else {
		if (! subItemMsg.IsEmpty()) {
			childMsg->AddMessage("item", new BMessage(subItemMsg));
		}
	}
	return result;
}
