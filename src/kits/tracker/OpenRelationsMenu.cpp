#define DEBUG 1

#include "Attributes.h"
#include "AutoLock.h"
#include "Commands.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "OpenRelationsMenu.h"
#include "OpenRelationTargetsMenu.h"
#include "MimeTypes.h"
#include "Sen.h"
#include "Sensei.h"
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

        // get all relations for refs received, what field contains relation type (ALL or SELF)
        BMessage* message = new BMessage(fEntriesToOpen);

		// use SEN action as new message type for passing on to SEN
		uint32 senCmd;
		message->FindUInt32(SEN_ACTION_CMD, &senCmd);
		if (senCmd == 0) {
			senCmd = SEN_RELATIONS_GET_ALL;
		}
		message->what = senCmd;

        //TODO: support multi-selection - when SEN is adapted
        entry_ref ref;
        fEntriesToOpen.FindRef("refs", &ref);
        BEntry entry(&ref);
        BPath path;
        entry.GetPath(&path);

        PRINT(("Tracker->SEN: getting relations for path %s\n", path.Path()));
        message->AddString(SEN_RELATION_SOURCE, path.Path());

        fSenMessenger.SendMessage(message, &fRelationsReply);

        PRINT(("SEN->Tracker: received reply:\n"));

        // also add source to reply for later use in building relation target menu
		// todo: migrate to native refs internally and use source path only for external scripting!
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

	int32 relationCount = 0;
	BString source;
	// safe since we added it from the ref above
	fRelationsReply.FindString(SEN_RELATION_SOURCE, &source);

	// check for self relations and handle them separately
	if (fRelationsReply.what == SENSEI_MESSAGE_RESULT) {
		PRINT(("building self relations menu.\n"));
		relationCount = AddSelfRelationItems(&source);
	} else {
		PRINT(("building relations menu.\n"));
		relationCount = AddRelationItems(&source);
	}

	if (relationCount == 0) {
		BMenuItem* item = new BMenuItem("no relations found.", 0);
		item->SetEnabled(false);
		AddItem(item);
	}
}

uint32 OpenRelationsMenu::AddRelationItems(const BString* source) {
	BString relation;
    int index = 0;

    while (fRelationsReply.FindString(SEN_RELATIONS, index, &relation) == B_OK) {
		// message for relation menu items
        BMessage* message = new BMessage(SEN_RELATIONS_GET);
        message->AddString(SEN_RELATION_SOURCE, (new BString(*source))->String());
        message->AddString(SEN_RELATION_TYPE, (new BString(relation))->String());

		// message for the relation menu itself (to open targets in separate Tracker window)
		BString srcId;
		if (fRelationsReply.FindString(SEN_ID_ATTR, &srcId) != B_OK) {
			srcId.SetTo("0815");
		}
		BMimeType mime(relation.String());
		if (!mime.IsInstalled()) {
			ERROR("skipping relation with unavailable MIME type %s...\n", relation.String());
			index++;
			continue;
		}
		char label[B_ATTR_NAME_LENGTH];
		if (mime.GetShortDescription(label) != B_OK) {
			PRINT(("could not get MIME type for relation %s", relation.String()));
			relation.CopyInto(label, 0, relation.Length());
		}
		BMessage *openRelationTargetsMsg = new BMessage(SEN_OPEN_RELATION_TARGET_VIEW);
        openRelationTargetsMsg->AddString(SEN_RELATION_SOURCE, source->String());
		openRelationTargetsMsg->AddString(SEN_RELATION_SOURCE_ATTR, (new BString(srcId))->String());
		openRelationTargetsMsg->AddString(SEN_RELATION_TYPE, (new BString(relation))->String());
		openRelationTargetsMsg->AddString(SEN_RELATION_LABEL, label);

        BMenuItem* item = new IconMenuItem(
            new OpenRelationTargetsMenu(label, message, fParentWindow, be_app_messenger),
            openRelationTargetsMsg,
			(new BString(relation))->String()
        );
		// redirect open relation targets message to Tracker app directly
		item->SetTarget(be_app_messenger);
		AddItem(item);
        index++;
    }
	return index;
}

uint32 OpenRelationsMenu::AddSelfRelationItems(const BString* source) {
	BMessage pluginConfig;
	int relationsAdded = 0;
    status_t result;

	result = fRelationsReply.FindMessage(SENSEI_SELF_PLUGIN_CONFIG_KEY, &pluginConfig);
	if (result != B_OK) {
		PRINT(("no plugin config found / unexpected reply.\n"));
		return 0;
	}

	// store default type for use in self relation targets menu later
	BString defaultType;
	result = pluginConfig.FindString(SENSEI_SELF_DEFAULT_TYPE_KEY, &defaultType);
	if (result != B_OK) {
		if (result != B_NAME_NOT_FOUND) { // this is valid, but else it's an error
			PRINT(("failed to look up default type in plugin config: %s\n", strerror(result)));
			return 0;
		}
	}

	BMessage typesPlugins;
	result = pluginConfig.FindMessage(SENSEI_SELF_TYPES_PLUGINS_KEY, &typesPlugins);
	if (result != B_OK) {
		PRINT(("no type->plugin mapping found in config received!.\n"));
		return 0;
	}

	int32 itemCount = typesPlugins.CountNames(B_STRING_TYPE);
	if (itemCount == 0) {
		PRINT(("no relations found.\n"));
		return 0;
	}

	PRINT(("got self relations config:\n"));
	typesPlugins.PrintToStream();

	int32 pluginCount;
	char *relationType[B_MIME_TYPE_LENGTH];
	BString pluginName;

    for (int32 index = 0; index < itemCount; index++) {
		result = typesPlugins.GetInfo(B_STRING_TYPE, index, relationType, NULL, &pluginCount);
		if (result != B_OK || *relationType == NULL) {
			PRINT(("failed to parse self relations (%d added): %s\n", relationsAdded, strerror(result)));
			return relationsAdded;
		}
		PRINT(("adding menu item for self relation %s with %u plugins...\n", *relationType, pluginCount));

		result = typesPlugins.FindString(*relationType, &pluginName);
		if (result != B_OK || ! BMimeType(pluginName.String()).IsValid()) {
			PRINT(("failed to get valid plugin (got '%s') to resolve self relation %s: %s",
				pluginName.String(), *relationType, strerror(result)) );

			return relationsAdded;
		}
		PRINT(("got plugin %s to resolve relation %s\n", pluginName.String(), *relationType));

		// message for relation menu items
        BMessage* message = new BMessage(SEN_RELATIONS_GET_SELF);
		message->AddString(SEN_RELATION_SOURCE, (new BString(*source))->String());
        message->AddString(SEN_RELATION_TYPE, (new BString(*relationType))->String());
		// add plugin needed to resolve this self relation
		message->AddString(SENSEI_SELF_PLUGIN_KEY, pluginName);
		// add default type if any
		if (! defaultType.IsEmpty()) {
			message->AddString(SENSEI_SELF_DEFAULT_TYPE_KEY, defaultType);
		}

		PRINT(("message for open targets:\n"));
		message->PrintToStream();

		// message for the relation menu itself (to open targets in separate Tracker window)
		BString srcId;
		if (fRelationsReply.FindString(SEN_ID_ATTR, &srcId) != B_OK) {
			srcId.SetTo("0815");
		}
		BMimeType mime(*relationType);
		if (!mime.IsInstalled()) {
			ERROR("skipping relation with unavailable MIME type %s...\n", *relationType);
			index++;
			continue;
		}
		char label[B_ATTR_NAME_LENGTH];
		if (mime.GetShortDescription(label) != B_OK) {
			PRINT(("could not get short description for relation MIME type %s, falling back to type name.\n", *relationType));
			strcpy(label, *relationType);
		}

		BMessage *openRelationTargetsMsg = new BMessage(SEN_OPEN_RELATION_TARGET_VIEW);
        openRelationTargetsMsg->AddString(SEN_RELATION_SOURCE, (new BString(*source))->String());
		openRelationTargetsMsg->AddString(SEN_RELATION_SOURCE_ATTR, (new BString(srcId))->String());
		openRelationTargetsMsg->AddString(SEN_RELATION_TYPE, (new BString(*relationType))->String());
		openRelationTargetsMsg->AddString(SEN_RELATION_LABEL, label);
		openRelationTargetsMsg->AddString(SENSEI_SELF_PLUGIN_KEY, pluginName);

        BMenuItem* item = new IconMenuItem(
            new OpenRelationTargetsMenu(label, message, fParentWindow, be_app_messenger),
            openRelationTargetsMsg,
			(new BString(*relationType))->String()
        );
		// redirect open relation targets message to Tracker app directly
		item->SetTarget(be_app_messenger);
		AddItem(item);

		relationsAdded++;
    }
	return relationsAdded;
}
