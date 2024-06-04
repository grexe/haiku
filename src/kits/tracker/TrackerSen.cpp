/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered trademarks
of Be Incorporated in the United States and other countries. Other brand product
names are registered trademarks or trademarks of their respective holders.
All rights reserved.
*/

#include <FindDirectory.h>
#include <Message.h>
#include <NodeInfo.h>
#include <StringList.h>

#include "Sen.h"
#include "Tracker.h"

bool
TTracker::HandleSenMessage(BMessage* message)
{
	if (message->what != SEN_OPEN_RELATION_VIEW
		&& message->what != SEN_OPEN_RELATION_TARGET_VIEW) {
		// not a SEN command message, handle as normal
		return false;
	}

	// handle SEN command messages
	status_t result = B_OK;
	RelationInfo relationInfo;

	switch (message->what) {
		case SEN_OPEN_RELATION_VIEW:
		{
			result = PrepareRelationWindow(message, &relationInfo);
			break;
		}
		case SEN_OPEN_RELATION_TARGET_VIEW:
		{
			result = PrepareRelationTargetWindow(message, &relationInfo);
			break;
		}
		default:
		{
			ERROR("unknown SEN message %u, ignoring.\n", message->what);
			break;
		}
	}
	if (result == B_OK) {
		// done handling message, send as refs to Tracker for opening as normal
		BMessage* trackerOpenDir = new BMessage(B_REFS_RECEIVED);
		trackerOpenDir->AddRef("refs", &relationInfo.relationDirRef);
		be_app_messenger.SendMessage(trackerOpenDir);
	} else {
		ERROR("error opening relation view: %s\n", strerror(result));
	}
	// just indicates we are done and no further processing by the caller is needed.
	return true;
}

status_t
TTracker::PrepareRelationWindow(BMessage *message, RelationInfo* relationInfo)
{
	//status_t result;
	return B_ERROR;	// not yet implemented
}

status_t
TTracker::PrepareRelationTargetWindow(BMessage *message, RelationInfo* relationInfo)
{
	status_t result;
	if ((result = PrepareRelationDirectory(message, relationInfo) != B_OK)) {
		ERROR("could not create relation target view: %s\n", strerror(result));
		return result;
	}
	BDirectory relationDir(&relationInfo->relationDirRef);

	// get fresh relation targets from SEN server
	BMessenger senMessenger(SEN_SERVER_SIGNATURE);
	BMessage relationTargetsMsg(SEN_RELATIONS_GET);
	relationTargetsMsg.AddString(SEN_RELATION_SOURCE, relationInfo->source);
    relationTargetsMsg.AddString(SEN_RELATION_TYPE, relationInfo->relationType);

	BMessage reply;
	senMessenger.SendMessage(&relationTargetsMsg, &reply);

	BMessage relations;
	if (reply.FindMessage(SEN_RELATIONS, &relations) != B_OK) {
		ERROR("got no relations for type %s and source %s.\n",
			relationInfo->relationType.String(), relationInfo->source.String());
			return B_ERROR;
	}
	BMimeType relationType(relationInfo->relationType);
	entry_ref ref;

	BStringList targetIds;
	relations.FindStrings(SEN_TO_ATTR, &targetIds);
	if (targetIds.IsEmpty()) {
		// should never happen
		ERROR(("no target IDs found for relation %s and source %s!\n"),
			relationInfo->relationType.String(), relationInfo->source.String());
		return B_ERROR;
	}

	// iterate through refs received and populate relation targets dir with relation targets
	int32 index = 0;
	while (reply.FindRef("refs", index, &ref) == B_OK) {
		DEBUG("creating relation file for type %s and ref %s for source with ID %s...\n",
			relationInfo->relationType.String(), ref.name, relationInfo->srcId.String());

		// todo: handle multiple relations for the same target wrt naming file
		BFile relationTarget(&relationDir, ref.name, B_READ_WRITE | B_CREATE_FILE);
		BMessage attrInfo;

		result = relationTarget.InitCheck();
		if (result != B_OK) {
			ERROR("error creating relation target %s: %s\n", ref.name, strerror(result));
			return result;
		}

		result = relationType.GetAttrInfo(&attrInfo);
		if (result != B_OK) {
			ERROR(("error reading attribute info for relation with type %s: %s"),
				relationType.Type(), strerror(result));
			return result;
		}

		BNode relationNode(relationTarget);
		BNodeInfo relationNodeInfo(&relationNode);
		if (result == B_OK) result = relationNodeInfo.InitCheck();

		if (result == B_OK) result = relationNodeInfo.SetType(relationInfo->relationType);
		if (result == B_OK) result = relationNode.WriteAttrString
										(SEN_RELATION_SOURCE_ATTR, &relationInfo->srcId);
		if (result == B_OK) result = relationNode.WriteAttrString
										(SEN_RELATION_TARGET_ATTR,
										new BString(targetIds.StringAt(index)));
		if (result != B_OK) {
			ERROR(("error writing relation attributes: %s"), strerror(result));
			return result;
		}
		// write relation properties received from SEN
		BMessage properties;
		if ((result = relations.FindMessage(SEN_RELATION_PROPERTIES, index, &properties) == B_OK)) {
			// write out relation attributes according to MIME type and value from reply
			BString attrName;
			int32 attrType;
			int32 attrIndex = 0;
			while (attrInfo.FindString("attr:name", attrIndex, &attrName) == B_OK) {
				result = attrInfo.FindInt32("attr:type", attrIndex, &attrType);
				if (result == B_OK) {
					// todo: handle types other than String
					const char* value = properties.FindString(attrName, index);
					if (value != NULL) {
						DEBUG(("creating relation property attribute %s with value %s\n"),
							attrName.String(), value);
						result = relationNode.WriteAttrString(attrName, new BString(value));
						if (result != B_OK) {
							ERROR(("failed to write relation property attribute %s with value %s: %s\n"),
							attrName.String(), value, strerror(result));
						}
					}
				}
				attrIndex++;
			}
		}
		relationNode.Sync();
		relationTarget.Unset();
		index++;
	}

	return B_OK;
}

status_t
TTracker::PrepareRelationDirectory(BMessage *message, RelationInfo* relationInfo)
{
	status_t result = B_OK;

	BString src;
	if ((result = message->FindString(SEN_RELATION_SOURCE, &src)) != B_OK) {
		return result;
	}
	BString srcId;
	if ((result = message->FindString(SEN_RELATION_SOURCE_ATTR, &srcId)) != B_OK) {
		return result;
	}
	BString relationType;
	if ((result = message->FindString(SEN_RELATION_TYPE, &relationType)) != B_OK) {
		return result;
	}
	BString relationLabel;
	if ((result = message->FindString(SEN_RELATION_LABEL, &relationLabel)) != B_OK) {
		return result;
	}
	BString relationName(relationType);
	if (relationName.StartsWith(SEN_RELATION_SUPERTYPE "/")) {
		relationName.Remove(0, sizeof(SEN_RELATION_SUPERTYPE));
	}

	BPath relationsDirPath;
	if ((result = find_directory(B_SYSTEM_TEMP_DIRECTORY, &relationsDirPath)) != B_OK) {
		ERROR("could not find temp directory: %s\n", strerror(result));
		return result;
	}
	BString relationsDirName(relationsDirPath.Path());
	relationsDirName.Append("/sen/") << srcId << "/relations/" << relationName
		<< "/" << BPath(src).Leaf() << "\u2192" << relationLabel << " targets";

	if ((result = create_directory(relationsDirName, B_READ_WRITE) != B_OK)) {
		ERROR("failed to create temp dir at %s: %s\n", relationsDirName.String(), strerror(result));
		return result;
	}

	// populate result param
	BDirectory relationsDir(relationsDirName.String());
	BEntry entry;
	relationsDir.GetEntry(&entry);
	entry.GetRef(&relationInfo->relationDirRef);
	relationInfo->relationType = relationType;
	relationInfo->source = src;
	relationInfo->srcId = srcId;

	return result;
}
