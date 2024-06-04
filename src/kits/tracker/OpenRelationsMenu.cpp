#include "Attributes.h"
#include "AutoLock.h"
#include "Commands.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "OpenRelationsMenu.h"
#include "OpenRelationTargetsMenu.h"
#include "MimeTypes.h"
#include "Sen.h"
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

OpenRelationsMenu::OpenRelationsMenu(const char* label, const BMessage* entriesToOpen,
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


OpenRelationsMenu::OpenRelationsMenu(const char* label, const BMessage* entriesToOpen,
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
OpenRelationsMenu::StartBuildingItemList()
{
    if (be_roster->IsRunning(SEN_SERVER_SIGNATURE)) {
		fSenMessenger = BMessenger(SEN_SERVER_SIGNATURE);
        // get all relations for refs received
        BMessage* message = new BMessage(fEntriesToOpen);
		// but change type for SEN relations query
		message->what = SEN_RELATIONS_GET_ALL;

        //TODO: support multi-selection - when SEN is adapted
        entry_ref ref;
        fEntriesToOpen.FindRef("refs", &ref);
        BEntry entry(&ref);
        BPath path;
        entry.GetPath(&path);

        PRINT(("Tracker->SEN: getting relations for path %s", path.Path()));
        message->AddString(SEN_RELATION_SOURCE, path.Path());

        fSenMessenger.SendMessage(message, &fRelationsReply);

        PRINT(("SEN->Tracker: received reply: %c", fRelationsReply.what));

        // also add source to reply for later use in building relation target menu
        fRelationsReply.AddString(SEN_RELATION_SOURCE, path.Path());

        return true;
    } else {
        PRINT(("failed to reach SEN server, please start process '%s' first.", SEN_SERVER_SIGNATURE));
        return false;
    }
}

bool OpenRelationsMenu::AddNextItem()
{
    // nothing to do here
    return false;
}

void OpenRelationsMenu::ClearMenuBuildingState()
{
    //  empty;
    return;
}

void
OpenRelationsMenu::DoneBuildingItemList()
{
    // bail out if SEN server is not running
    if (! be_roster->IsRunning(SEN_SERVER_SIGNATURE)) {
        BMenuItem* item = new BMenuItem("n/a, SEN server not running.", 0);
		item->SetEnabled(false);
		AddItem(item);
        return;
    }

	// target the menu
	if (target != NULL)
		SetTargetForItems(target);
	else
		SetTargetForItems(fMessenger);

    BString relation, source;
    int index = 0;
    fRelationsReply.FindString(SEN_RELATION_SOURCE, &source);

    while (fRelationsReply.FindString(SEN_RELATIONS, index, &relation) == B_OK) {
		BString currentRelation(relation);
		// message for relation menu items
        BMessage* message = new BMessage(SEN_RELATIONS_GET);
        message->AddString(SEN_RELATION_SOURCE, source.String());
        message->AddString(SEN_RELATION_TYPE, currentRelation.String());

		// message for the relation menu itself (to open targets in separate Tracker window)
		BString srcId;
		if (fRelationsReply.FindString(SEN_ID_ATTR, &srcId) != B_OK) {
			srcId.SetTo("0815");
		}
		BMimeType mime(relation);
		char *label = new char[relation.CountChars()];
		if (mime.GetShortDescription(label) != B_OK) {
			PRINT(("could not get MIME type for relation %s", relation.String()));
			currentRelation.CopyInto(label, 0, relation.Length());
		}
		BMessage *openRelationTargetsMsg = new BMessage(SEN_OPEN_RELATION_TARGET_VIEW);
        openRelationTargetsMsg->AddString(SEN_RELATION_SOURCE, source.String());
		openRelationTargetsMsg->AddString(SEN_RELATION_SOURCE_ATTR, srcId.String());
		openRelationTargetsMsg->AddString(SEN_RELATION_TYPE, relation.String());
		openRelationTargetsMsg->AddString(SEN_RELATION_LABEL, label);

        BMenuItem* item = new IconMenuItem(
            new OpenRelationTargetsMenu(label, message, fParentWindow, be_app_messenger),
            openRelationTargetsMsg,
			currentRelation.String()
        );
		// redirect open relation targets message to Tracker app directly
		item->SetTarget(be_app_messenger);
		AddItem(item);
        index++;
    }

	if (index == 0) {
		BMenuItem* item = new BMenuItem("no relations found.", 0);
		item->SetEnabled(false);
		AddItem(item);
	}
}
