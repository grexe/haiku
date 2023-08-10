/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
 
#ifndef _SEN_H
#define _SEN_H

#include <stdio.h>

#define SEN_SERVER_SIGNATURE "application/x-vnd.sen-labs.sen-server"

// simple logging
#define DEBUG(x...)		printf(x);
#define LOG(x...)		printf(x);
#define ERROR(x...)		fprintf(stderr, x);

// core	

#define SEN_CORE_INFO 				'SCin'
#define SEN_CORE_STATUS 			'SCst'
#define SEN_CORE_INIT				'SCis'
// validate and repair relation attributes
#define SEN_CORE_CHECK				'SCck'

#define SEN_ATTRIBUTES_PREFIX		"SEN:"
#define SEN_FILE_ID					SEN_ATTRIBUTES_PREFIX "_id"
#define SEN_FILE_ID_SEPARATOR		","

// Relations
#define SEN_RELATION_SOURCE "source"
#define SEN_RELATION_NAME   "relation"
#define SEN_RELATION_TARGET "target"

#define SEN_RELATIONS_GET                       'SCrg'
#define SEN_RELATIONS_GET_TARGETS       'SCrt'
#define SEN_RELATIONS_ADD                       'SCra'
#define SEN_RELATIONS_REMOVE            'SCrr'
#define SEN_RELATIONS_REMOVEALL         'SCrd'

// Message Replies
#define SEN_RESULT_INFO				'SCri'
#define SEN_RESULT_STATUS			'SCrs'
#define SEN_RESULT_RELATIONS		'SCre'

#endif // _SEN_DEFS_H
