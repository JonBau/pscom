/*
 * Copyright (C) 2016 Thorsten Born
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Author: Thorsten Born <thorsten.born@rwth-aachen.de>
 *
 * Modified by: Jonas Baude <jonas.baude@rwth-aachen.de>
 *
 */

#include "shm_lxc.h"
#ifdef SUPPORT_CONTAINERS

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/stat.h>
// #include <signal.h>	// TODO still required?


//#define CONTAINER_MAX_NODES_OFFSET 1   //TODO still required?
#define NUM_DETECTION_METHODS  3


void shm_lxc_init( void )
{
	DPRINT( 1, "SHM_LXC_INIT BEGIN");
	//TODO:   init uuid !!
	init_uuid();
	DPRINT( 1, "SHM_LXC_INIT END");
}


bool shm_is_containerized( void )
{
  	bool bReturn = false;
	int iterator = 0;

	/* Array of function pointers to easily add a new containerization
	 * detection method. Just name it bContainerizationDetectionMethodN_and_a_brief_description
	 * add it to this array and adjust the NUM_DETECTION_METHODS contant.
	 * */
	bool (*pDetectionMethods[NUM_DETECTION_METHODS]) (void) = { 	\
 		bContainerizationDetectionMethod1_proc_cgroup,	\
		bContainerizationDetectionMethod2_proc_1_shed,	\
		bContainerizationDetectionMethod3_dev_console
	};

	/* cache */
	static bool bIsContainerized = undef; /* used for caching */
	if( bIsContainerized != undef )
  	{
		return bIsContainerized;
  	}


	/* Iterate over all defined detection methods until one method detects containerization */
	for (iterator = 0; iterator < NUM_DETECTION_METHODS; iterator++)
	{
		/* try all Detection methods until one returns 'true' -> containerized */
		if( (*pDetectionMethods[iterator])() == true)
		{
			DPRINT( 1, "Containerization detected...");
			bReturn = true;
			break;
		}
	}

	return bReturn;

}

bool bContainerizationDetectionMethod1_proc_cgroup(void)
{
  /* Try method 1:
   *  Won't work starting from kernel 4.6 because this information
   *  is hidden when using cgroupns!
   *  More info:
   *    https://github.com/torvalds/linux/blob/master/Documentation/cgroup-v2.txt (6-1. Basics)
   */
  	const char *strFilenameCGROUP = "/proc/1/cgroup";
  	FILE* pFileCGROUP = NULL;
  	char strBuf[256];
  	char *strContainerType = NULL;
	bool bReturn = false;

  	pFileCGROUP = fopen( strFilenameCGROUP, "r" );
    	if( pFileCGROUP == NULL )
      		DPRINT( 1, "***** ERROR: |%s| could not open '%s'! \n", __func__, strFilenameCGROUP );

  	while( fgets( strBuf, sizeof( strBuf ), pFileCGROUP ) )
  	{
  		if( strstr( strBuf, "cpuset" ) )
  		{
  			/* split at first '/'
  			 * line could be e.g. "2:cpuset:/lxc/container1" */
  			strtok_r( strBuf, "/", &strContainerType );
  			/* split again at last '/' */
  			strtok( strContainerType, "/" );

  			/* remove trailing newline */
  			strContainerType[ strcspn( strContainerType, "\r\n" ) ] = '\0';

#ifdef CONTAINER_TYPE_WHITELIST
  			unsigned char i = 0;
  			const char *pStrContainerTypeWhitelist[] = { CONTAINER_TYPE_WHITELIST };

  			for( i = 0; i < sizeof( pStrContainerTypeWhitelist ) / sizeof( pStrContainerTypeWhitelist[0] ); i++ )
  			{
  				/* is this type in the whitelist? */
  				if( !strcmp( strContainerType, pStrContainerTypeWhitelist[i] ) )
  				{
  					bReturn = true;
  					break;
  				}
  			}

  			if( bReturn )
          DPRINT( 2, "***** DEBUG: |%s| determined containerization via method 1 (%s) strContainerType='%s'\n", __func__, strFilenameCGROUP, strContainerType );
        // else
        //   DPRINT( 1, "*** WARNING: |%s| strContainerType='%s' unknown\n", __func__, strContainerType );
#else
  			if( strcmp( strContainerType, "" ) )
  				bReturn = true;
#endif /* CONTAINER_TYPE_WHITELIST */

  			break;
  		}
  	}

  	fclose( pFileCGROUP );
	return bReturn;
  }


bool bContainerizationDetectionMethod2_proc_1_shed(void)
{
  /* Try method 2:
   * Extract PID from /proc/1/sched. If it is greater than 1, then we can assume
   * we are running inside a container.
   * TODO consider running this check first
   */
	const char *strFilenameSCHED = "/proc/1/sched";
    	FILE* pFileSCHED = NULL;
    	char *strSCHEDPID = NULL;
    	char strBuf[256];
    	unsigned int uiSCHEDPID;
    	bool bReturn = false;

    	pFileSCHED = fopen( strFilenameSCHED, "r" );
    	if( pFileSCHED == NULL )
    	{
      		DPRINT( 2, "***** ERROR: |%s| could not open '%s'! \n", __func__, strFilenameSCHED );
    	}
    	else
    	{
      		/* get first line */
      		fgets( strBuf, sizeof( strBuf ), pFileSCHED );

      		/* split at first '('
       		 * line could be e.g. "systemd (1, #threads: 1)" */
      		strtok_r( strBuf, "(", &strSCHEDPID );

      		/* split again at last ',' */
      		strtok( strSCHEDPID, "," );

      		/* remove trailing newline */
      		strSCHEDPID[ strcspn( strSCHEDPID, "\r\n" ) ] = '\0';

      		/* convert to integer */
      		uiSCHEDPID = atoi( strSCHEDPID );

      		/* is this PID > 1? */
      		if( uiSCHEDPID > 1 )
      		{
        		bReturn = true;
        		DPRINT( 2, "***** DEBUG: |%s| determined containerization via method 2 (%s) uiSCHEDPID=%d\n", __func__, strFilenameSCHED, uiSCHEDPID );
      		}
    	}
    	fclose( pFileSCHED );
	return bReturn;
  }

bool bContainerizationDetectionMethod3_dev_console(void)
{
   /* check if /dev/console is links to -> lxc/console */

   const char *strPath = "/dev/console";
   char strBuf[256];
   int strLength;
   int strCompare;
   bool bReturn = false;
   strBuf[0] = '\0';

   if ((strLength = readlink(strPath, strBuf, sizeof(strBuf)-1)) != -1)
	    strBuf[strLength] = '\0';

   if (strLength < 0)
   {
	   /* /dev/console is not a link...
	    * => process is NOT executed inside a container
	    */
	   bReturn = false;
	   goto out;
   }

   if(strLength <11) {
	/* [ERROR] link is too short") */
	   bReturn = false;
	   goto out;
   }

   strCompare = strncmp(strBuf, "lxc/console",11);

   if (!strCompare) {
	   /* [INFO] /dev/console links to -> lxc/console
	      [RESULT] process is executed inside a container */
	   bReturn = true;
           DPRINT( 2, "***** DEBUG: |%s| determined containerization via method 3: %s links to -> %s\n", __func__, strPath, strBuf);
   } else
   {
	   /* [INFO] /dev/console links NOT to -> lxc/console \n
	      [RESULT] process is executed inside a container\n" */
	   bReturn = false;
   }

out:
return bReturn;
}

/***************************************************************************************************************/

/* unique key to access the shared uuid memory region */
#define SHM_LXC_UUID_SHM_KEY 2017	//TODO assign value

/* used to verify that the shm region has been initialized by a pscom shm_lxc plugin */
#define SHM_LXC_UUID_MAGIC 20172017	//TODO assign value

uuid_t local_uuid;

uuid_t* shm_lxc_get_local_uuid(void)
{
	return &local_uuid;
}

typedef struct shm_lxc_uuid_meta_s
{
	int magic;
	uuid_t uuid;
} shm_lxc_uuid_meta_t;

static void create_uuid(int shmid)
{
	shm_lxc_uuid_meta_t * metadata = shmat(shmid,NULL, 0);
	metadata->magic = SHM_LXC_UUID_MAGIC;
	uuid_generate(&(metadata->uuid));
	shmdt(metadata);
}

static void init_uuid(void)
{
	int uuid_shm_id;
	/* segment should contain a uuid and a magic number to ensure that the segment is used by pscom shm_lxc */
	int shm_seg_size = sizeof(shm_lxc_uuid_meta_t);
	shm_lxc_uuid_meta_t * metadata;

	uuid_shm_id = shmget(SHM_LXC_UUID_SHM_KEY, shm_seg_size , IPC_CREAT | IPC_EXCL| 0666 );

	if(uuid_shm_id >= 0)  //TODO   >0 or >=0 ??
	{
		create_uuid(uuid_shm_id);
	}

	if ((uuid_shm_id == -1) && (errno == EEXIST))
	{
		uuid_shm_id = shmget(SHM_LXC_UUID_SHM_KEY,shm_seg_size,  S_IRUSR | S_IWUSR ); //only read permission required
		/*TODO Check return values and errors */
	}

	//DPRINT( 1, "uuid_shm_id=%d",uuid_shm_id);

	metadata = shmat(uuid_shm_id,NULL,0);
	/* ensure that referenced memroy represents a valid uuid */
	//DPRINT( 1, "MAGIC=%d",metadata->magic);
	assert(metadata->magic == SHM_LXC_UUID_MAGIC);
//	*local_uuid = metadata->uuid;
	uuid_copy(local_uuid, metadata->uuid);
	shmdt(metadata);

}

/***************************************************************************************************************/

#endif /* SUPPORT_CONTAINERS */
