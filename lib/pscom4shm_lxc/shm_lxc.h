/*
 * Copyright (C) 2016 Thorsten Born
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Author: Thorsten Born <thorsten.born@rwth-aachen.de>
 */

#ifndef _SHM_LXC_H_
#define _SHM_LXC_H_

#ifndef SUPPORT_CONTAINERS
#define SUPPORT_CONTAINERS
#endif /* SUPPORT_CONTAINERS */


#ifdef SUPPORT_CONTAINERS

#include "../pscom/pscom_con.h"
#include "../pscom/pscom_types.h"
#include <uuid/uuid.h>

/* configs */
#define CONTAINER_TYPE_WHITELIST 				"lxc", "docker" /* comma separated strings */
#define CONTAINER_SHM_KEY								110000000
#define CONTAINER_USE_SHM_MAGIC_NUMBER
#define CONTAINER_SHM_MAGIC_NUMBER			345721600
#define CONTAINER_MAX_NODES							64
#define CONTAINER_USE_SEMAPHORE
#define CONTAINER_SEMAPHORE_MAX_RETRIES 1000


/* */
// #define DEBUG_IN		char spaces[1] = {""};
// #define DEBUG_OUT

// /*
#define DEBUG_IN 		char spaces[100] = {0}; \
										memset( spaces, ' ', (cntrNested++)*4 ); \
										DPRINT( 1, ">>>>> DEBUG:%s |%s| <begin> \n", spaces, __func__ );
#define DEBUG_OUT 	cntrNested--; \
										DPRINT( 1, "<<<<< DEBUG:%s |%s| <end> \n", spaces, __func__ );

unsigned char cntrNested;
// */


/* custom types */
typedef enum
{
	false, true, undef
} bool;

typedef enum
{
	CONTAINER_LIST_GET, CONTAINER_LIST_ADD, CONTAINER_LIST_DEL,
		CONTAINER_LIST_CLEAR, CONTAINER_LIST_PRINT
} nodeidlist_action;


void shm_lxc_init( void );
bool shm_is_containerized( void );
bool shm_is_on_same_ipc_ns( pscom_con_t* );
bool shm_nodeid_list( nodeidlist_action , int );
bool bContainerizationDetectionMethod1_proc_cgroup( void );
bool bContainerizationDetectionMethod2_proc_1_shed( void );
bool bContainerizationDetectionMethod3_dev_console( void );
uuid_t* shm_lxc_get_local_uuid( void );
static void create_uuid( int );
static void init_uuid( void );


#endif /* SUPPORT_CONTAINERS */

#endif /* _SHM_LXC_H_ */
