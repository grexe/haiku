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
	fParentWindow(parentWindow)
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
	fParentWindow(parentWindow)
{
	InitIconPreloader();

	SetFont(be_plain_font);

	// too long to have triggers - TODO; what does it mean?
	SetTriggersEnabled(false);
}

bool
OpenRelationTargetsMenu::StartBuildingItemList()
{
	if (fEntriesToOpen.what == SENSEI_MESSAGE_RESULT) {	// we are a sub menu of self relations
		return true;
	}

    if (be_roster->IsRunning(SEN_SERVER_SIGNATURE)) {
		fSenMessenger = BMessenger(SEN_SERVER_SIGNATURE);
        PRINT(("Tracker->SEN: getting relation targets" UTF8_ELLIPSIS, path.Path()));
        fSenMessenger.SendMessage(&fEntriesToOpen, &fRelationTargetsReply);

        PRINT(("SEN->Tracker: received reply.\n"));
		fRelationTargetsReply.PrintToStream();
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

	if (fRelationTargetsReply.what == SENSEI_MESSAGE_RESULT) {
		// resolve self relations via plugin and cascade targets menu
		PRINT(("adding self relation targets..."));
		result = AddSelfRelationTargetItems(&targets);
	} else {
		PRINT(("adding relation targets..."));
		result = AddRelationTargetItems(&targets);
	}

	if (result != B_OK) {
		PRINT(("error buiding relations menu: %s\n", strerror(result) ));
		return;
	}

	if (targets == 0) {
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

	while ((result = fRelationTargetsReply.FindRef("refs", index, &ref)) == B_OK) {
		entry.SetTo(&ref);
		entry.GetPath(&path);

		ModelMenuItem* item = new ModelMenuItem(new Model(&ref, true, true), path.Leaf(), NULL);
		AddItem(item);

		index++;
		targetCount++;
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

	// get root node
	result = fRelationTargetsReply.FindMessage("item", &itemMsg);
	if (result != B_OK) {
		PRINT(("failed to build self relations menu, could not find root node, unexpected message reply.\n"));
		return result;
	}

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
			item->SetTarget(be_app_messenger);
		} else {
			// todo: add open target view msg
			PRINT(("adding self relation menu [%d] '%s' of type '%s'.\n", index, label.String(), type.String() ));
			childMsg.what = SENSEI_MESSAGE_RESULT;

			item = new IconMenuItem(
				new OpenRelationTargetsMenu(label.String(), new BMessage(childMsg), fParentWindow, be_app_messenger),
				new BMessage(childMsg),
				type.String()	// this is the MIME type of the relation, so take its icon
			);
		}
		AddItem(item);
		index++;
		targetCount++;
	}
	return B_OK;
}

status_t OpenRelationTargetsMenu::GetItemMessageInfo(BMessage* itemMsg, BMessage* childMsg, BString* label, BString* type, int32 index) {
	// get label
	status_t result = itemMsg->FindString("label", index, label);
	if (result != B_OK) {
		PRINT(("failed to get item label: %s\n", stterror(result) ));
	}
	// get type
	result = itemMsg->FindString("type", index, type);
	if (result != B_OK) {
		if (result == B_NAME_NOT_FOUND) {
			// this is valid if there is a default type
			BString defaultType;
			result = fRelationTargetsReply.FindString(SENSEI_SELF_DEFAULT_TYPE_KEY, &defaultType);
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
			PRINT(("failed to get item info: %s\n", stterror(result) ));
		}
	}
	// get optional child node message (empty or another node)
	result = itemMsg->FindMessage("item", index, childMsg);
	if (result != B_OK) {
		PRINT(("child node not found, is optional but must be at least empty!\n"));
	}
	return result;
}
