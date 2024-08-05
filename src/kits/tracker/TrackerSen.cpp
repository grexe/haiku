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
#define DEBUG 1

#include <Debug.h>
#include <FindDirectory.h>
#include <Message.h>
#include <NodeInfo.h>
#include <Query.h>
#include <StringList.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_attr.h>

#include "Commands.h"
#include "Sen.h"
#include "Tracker.h"

bool
TTracker::HandleSenMessage(BMessage* message)
{
	if (message->what != SEN_OPEN_RELATION_VIEW
		&& message->what != SEN_OPEN_RELATION_TARGET_VIEW
		&& message->what != kOpenRelations
		&& message->what != kOpenSelfRelations) {
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
		case kOpenRelations:
		case kOpenSelfRelations:	//fallthrough
		{
			PRINT(("open (self) relations view not yet implemented. Please check back later.\n"));
			break;
		}
		default:
		{
			PRINT(("unknown SEN message %u, ignoring.\n", message->what));
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

bool TTracker::ResolveRelation(const entry_ref* ref, BString* srcId, BString* targetId)
{
	BFile relationTarget(ref, B_READ_WRITE);
	status_t result = relationTarget.InitCheck();
	if (result != B_OK) {
		ERROR("error reading relation target %s: %s\n", ref->name, strerror(result));
		return result;
	}

	BNode relationNode(relationTarget);
	BNodeInfo relationNodeInfo(&relationNode);
	if (result == B_OK) result = relationNodeInfo.InitCheck();
	if (result != B_OK) {
		ERROR("error accessing relation target %s nodeInfo: %s\n", ref->name, strerror(result));
		return result;
	}
	if (result == B_OK) result = relationNode.ReadAttrString(SEN_RELATION_SOURCE_ATTR, srcId);
	if (result == B_NAME_NOT_FOUND) {
		// todo: check for self relation
		return false;
	}
	if (result == B_OK) result = relationNode.ReadAttrString(SEN_RELATION_TARGET_ATTR, targetId);
	if (result == B_NAME_NOT_FOUND) return false;

	return (result == B_OK);
}

status_t TTracker::PrepareLaunchTarget(
	const entry_ref* srcRef, const char* targetId, entry_ref* targetRef, BMessage* params)
{
	// get relation target by ID from SEN server
	BMessenger senMessenger(SEN_SERVER_SIGNATURE);
	BMessage queryTargetIdMsg(SEN_QUERY_ID);
	queryTargetIdMsg.AddString(SEN_ID_ATTR, targetId);

	BMessage reply;
	senMessenger.SendMessage(&queryTargetIdMsg, &reply);

	status_t result;
	if ((result = reply.FindRef("ref", targetRef)) == B_OK) {
		PRINT(("got target ref %s\n", targetRef->name));
		result = ConvertAttributesToMessage(srcRef, params);
	}
	return result;
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

    // check relations
	BMessage relations;
	if (reply.FindMessage(SEN_RELATIONS, &relations) != B_OK) {
		PRINT(("no relations for type %s and source %s to show.\n",
			relationInfo->relationType.String(), relationInfo->source.String()) );
			return B_OK;
	}

    // check target ids
	// todo: handle self relations
	BStringList targetIds;
	relations.FindStrings(SEN_TO_ATTR, &targetIds);
	if (targetIds.IsEmpty()) {
		// should never happen
		ERROR(("no target IDs found for relation %s and source %s!\n"),
			relationInfo->relationType.String(), relationInfo->source.String());
		return B_ENTRY_NOT_FOUND;
	}

	// get MIME type for target relation
	BMessage attrInfo;
	if ((result = GetRelationTypeAttributeInfo(relationInfo->relationType, &attrInfo)) != B_OK) {
		return result;
	}

	PRINT(("got relations for type %s for source %s:\n", relationInfo->relationType.String(), relationInfo->source.String()) );
	relations.PrintToStream();

	// iterate through targets with their properties and populate relation targets dir
    // with target files and write relation properties in file attributes for usual visualisation.
    for (int32 targetIndex = 0; targetIndex < targetIds.CountStrings(); targetIndex++) {
        PRINT(("handling properties for target ID %s ...\n", targetIds.StringAt(targetIndex).String()) );

        BMessage properties;
        int32 propertiesIndex = 0;

        while (relations.FindMessage(targetIds.StringAt(targetIndex), propertiesIndex, &properties) == B_OK) {
            // create a file for each set of properties for all targets
            entry_ref ref;
            reply.FindRef("refs", targetIndex, &ref); // has been checked above already

            PRINT(("processing properties #%d for ref %s:\n", propertiesIndex, ref.name));
            properties.PrintToStream();

            BString fileName(ref.name);
            fileName.Append(" #") << propertiesIndex + 1; // human readable 1-based index

            BFile relationTarget(&relationDir, fileName, B_READ_WRITE | B_CREATE_FILE/* | O_APPEND*/);
            result = relationTarget.InitCheck();
            if (result != B_OK) {
                ERROR("error creating relation target %s: %s\n", ref.name, strerror(result));
                return result;
            }

            BNode relationNode(relationTarget);
            BNodeInfo relationNodeInfo(&relationNode);
            if (result == B_OK) result = relationNodeInfo.InitCheck();
            if (result == B_OK) result = relationNodeInfo.SetType(relationInfo->relationType);
            if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_SOURCE_ATTR,
											&relationInfo->srcId);
            if (result == B_OK) result = relationNode.WriteAttrString(SEN_RELATION_TARGET_ATTR,
                                            new BString(targetIds.StringAt(targetIndex)));
            if (result != B_OK) {
                ERROR(("error writing relation attributes: %s"), strerror(result));
                return result;
            }

            // write out relation properties as file attributes according to MIME type
            BString attrName;
            int32 attrType;
            int32 attrIndex = 0;

            while (attrInfo.FindString("attr:name", attrIndex, &attrName) == B_OK) {
                result = attrInfo.FindInt32("attr:type", attrIndex, &attrType);
                if (result == B_OK) {
                    PRINT(("handling attribute %s of type %d...\n", attrName.String(), attrType));

                    const void* data;
                    ssize_t size;
                    result = properties.FindData(attrName.String(), static_cast<type_code>(attrType), 0, &data, &size);
                    if (result == B_OK) {
						BString value("value ");
                        PRINT(("creating relation property attribute %s with %s\n",
							attrName.String(), (value << result).String()) );

                        result = relationNode.WriteAttr(attrName.String(), attrType, 0, data, size);
                        if (result <= 0) {
                            ERROR(("failed to write relation property attribute %s.\n"), attrName.String());
                        }
                    }
                }
                attrIndex++;
            } // attributes loop
            relationNode.Sync();
            relationTarget.Unset();

            propertiesIndex++;
        } // properties loop
	}

	return B_OK;
}

status_t
TTracker::GetRelationTypeAttributeInfo(const char* relationType, BMessage* attrInfo) {
	status_t result;
	BMimeType relationMimeType(relationType);

	if (!relationMimeType.IsValid()) {
		ERROR("invalid MIME type for relation %s.\n", relationType);
		return B_ERROR;
	}
	if (!relationMimeType.IsInstalled()) {
		ERROR("MIME type for relation %s not installed, check config.\n", relationType);
		return B_ERROR;
	}

	// get defined attributes for MIME type of relation, same for all targets of this relation
	result = relationMimeType.GetAttrInfo(attrInfo);
	if (result != B_OK) {
		ERROR("error reading attribute info for relation with type %s: %s", relationType, strerror(result));
		return result;
	}

	// get additional attributes from relation supertype
	BMimeType relationSuperType(SEN_RELATION_SUPERTYPE);
	if (!relationSuperType.IsInstalled()) {
		ERROR("MIME supertype for relation %s is not installed, check SEN installation!\n", SEN_RELATION_SUPERTYPE);
		return B_ERROR;
	}
	BMessage attrInfoSuperType;
	result = relationSuperType.GetAttrInfo(&attrInfoSuperType);
	if (result != B_OK) {
		ERROR("error reading attribute info for relation super type: %s", strerror(result));
		return result;
	}
	// merge with attributes from supertype (relation)
	result = attrInfo->Append(attrInfoSuperType);
	if (result != B_OK) {
		ERROR("failed to construct attribute info for relation type and supertype: %s", strerror(result));
		return result;
	}
	PRINT(("got attributeInfo for type %s and supertype %s:\n", relationType, relationSuperType.Type()) );
	attrInfo->PrintToStream();

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

status_t TTracker::ConvertAttributesToMessage(const entry_ref* ref, BMessage* params) {
	status_t result;

	PRINT(("ConvertAttr2Msg: converting attributes of file %s...\n", ref->name));

	BNode node(ref);
	BPath path(ref);

	if ((result = node.InitCheck()) != B_OK) {
		ERROR("failed to init node for ref %s: %s\n", path.Path(), strerror(result));
		return result;
	}

	char attrName[B_ATTR_NAME_LENGTH];
	int32 attrCount = 0;
	attr_info attrInfo;

	// iterate through relation target attributes and convert based on the attrInfo
	while ((result = node.GetNextAttrName(attrName)) == B_OK) {
	    if ((result = node.GetAttrInfo(attrName, &attrInfo)) != B_OK) {
		    ERROR("error reading attr_info of attribute %s of ref %s: %s\n", attrName, path.Path(), strerror(result));
		    return result;
        }
		if (! BString(attrName).StartsWith(SEN_ATTR_PREFIX)) {
			PRINT(("skipping non-managed attribute %s of file %s...\n", attrName, path.Leaf()) );
			continue;
		}
		PRINT(("adding attribute %s of file %s...\n", attrName, path.Leaf()) );
		const void *data[attrInfo.size];
		ssize_t bytesRead = node.ReadAttr(attrName, attrInfo.type, 0, data, attrInfo.size);

		if (bytesRead <= 0) {
			ERROR("failed to read attribute value of attribute %s from file %s: %s\n",
				attrName, path.Path(), strerror(result));
			return result;
		}
		// now add to message as typed field
		params->AddData(attrName, attrInfo.type, data, bytesRead);
		attrCount++;
	}
	if (result != B_ENTRY_NOT_FOUND) {
		ERROR("failed to read attributes of ref %s: %s\n", path.Path(), strerror(result));
		return result;
	}
	PRINT(("converted %d attribute(s) for file %s\n", attrCount, path.Leaf()) );
	params->PrintToStream();

	return B_OK;
}