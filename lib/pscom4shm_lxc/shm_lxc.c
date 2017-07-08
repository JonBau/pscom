/*
 * Copyright (C) 2016 Thorsten Born
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Author: Thorsten Born <thorsten.born@rwth-aachen.de>
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
#include <signal.h>


#define CONTAINER_MAX_NODES_OFFSET 1


static void _shm_lxc_sighandler( int signum )
{
  DEBUG_IN
  DPRINT( 1, "***** DEBUG:%s |%s| signal: %d triggered!\n", spaces, __func__, signum );

  shm_nodeid_list( CONTAINER_LIST_DEL, pscom_get_nodeid() );
  // shm_nodeid_list( CONTAINER_LIST_CLEAR, 0 );
  DEBUG_OUT
}

bool shm_lxc_init( void )
{
  DEBUG_IN
  bool bReturn = false;

  /* print info about node */
	int siLocalNodeID = pscom_get_nodeid(); /* IP address of local node */
	char hostname[256];
	gethostname(hostname, sizeof hostname);
	DPRINT( 1, "***** DEBUG:%s |%s| siLocalNodeID=%d  hostname='%s' IP='%d.%d.%d.%d'\n", spaces, __func__, siLocalNodeID, hostname, (siLocalNodeID>>24) & 0xFF, (siLocalNodeID>>16) & 0xFF, (siLocalNodeID>>8) & 0xFF, siLocalNodeID & 0xFF );

	/* add local node id to list */
	if( !shm_nodeid_list( CONTAINER_LIST_ADD, pscom_get_nodeid() ) )
	{
		/* node id was already in list */
		DPRINT( 1, "*** WARNING:%s |%s| node id was already in list => \
                This means either program crashed, was interrupted or shares the same UTS ns with another node! \
                \n", spaces, __func__ ); //Let's assume they share the same UTS ns and therefor we allow shm connection
	}
	shm_nodeid_list( CONTAINER_LIST_PRINT, 0 );

  DEBUG_OUT
  return bReturn;
}



/**
 * @brief check if process is running in a container
 *
 * @return	true   if in container
 * 					false  if not in container
 */
bool shm_is_containerized( void )
{
  DEBUG_IN
  bool bReturn = false;

	/* cache */
	static bool bIsContainerized = undef; /* used for caching */
	if( bIsContainerized != undef )
  {
		bReturn = bIsContainerized;
    goto out;
  }


  /* Try method 1:
   *  Won't work starting from kernel 4.6 because this information
   *  is hidden when using cgroupns!
   *  More info:
   *    https://github.com/torvalds/linux/blob/master/Documentation/cgroup-v2.txt (6-1. Basics)
   */
  if ( !bReturn )
  {
  	const char *strFilenameCGROUP = "/proc/1/cgroup";
  	FILE* pFileCGROUP = NULL;
  	char strBuf[256];
  	char *strContainerType = NULL;

  	pFileCGROUP = fopen( strFilenameCGROUP, "r" );
    if( pFileCGROUP == NULL )
      DPRINT( 1, "***** ERROR:%s |%s| could not open '%s'! \n", spaces, __func__, strFilenameCGROUP );

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
          DPRINT( 1, "***** DEBUG:%s |%s| determined containerization via method 1 (%s) strContainerType='%s'\n", spaces, __func__, strFilenameCGROUP, strContainerType );
        // else
        //   DPRINT( 1, "*** WARNING:%s |%s| strContainerType='%s' unknown\n", spaces, __func__, strContainerType );
#else
  			if( strcmp( strContainerType, "" ) )
  				bReturn = true;
#endif /* CONTAINER_TYPE_WHITELIST */

  			break;
  		}
  	}

  	fclose( pFileCGROUP );
  }

  /* Try method 2:
   * Extract PID from /proc/1/sched. If it is greater than 1, then we can assume
   * we are running inside a container.
   * TODO consider running this check first
   */
  if ( !bReturn )
  {
    const char *strFilenameSCHED = "/proc/1/sched";
    FILE* pFileSCHED = NULL;
    char *strSCHEDPID = NULL;
    char strBuf[256];
    unsigned int uiSCHEDPID;

    pFileSCHED = fopen( strFilenameSCHED, "r" );
    if( pFileSCHED == NULL )
    {
      DPRINT( 1, "***** ERROR:%s |%s| could not open '%s'! \n", spaces, __func__, strFilenameSCHED );
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
        DPRINT( 1, "***** DEBUG:%s |%s| determined containerization via method 2 (%s) uiSCHEDPID=%d\n", spaces, __func__, strFilenameSCHED, uiSCHEDPID );
      }
    }
    fclose( pFileSCHED );
  }

  /* Try method 3
   * TODO implement systemd's detect_container():
   *  http://man7.org/linux/man-pages/man1/systemd-detect-virt.1.html
   *  https://github.com/systemd/systemd/blob/master/src/basic/virt.c
   */
  if ( !bReturn )
  {
    DPRINT( 1, "*** WARNING:%s |%s| method 3 not yet implemented!\n", spaces, __func__ );
  }

	DPRINT( 1, "***** DEBUG:%s |%s| bIsContainerized=%d\n", spaces, __func__, (int )bIsContainerized - 1 );
	bIsContainerized = bReturn;

out:
  DEBUG_OUT
	return bReturn;
}



/**
 * @brief checks if remote node is on the same physical host
 *
 * Test via SHM (nodes need to be in the same IPC namespace) if remote node
 * is on the same physical host
 *
 * @return	true   if both nodes are on the same physical host
 * 					false  if both nodes are NOT on the same physical host
 */
bool shm_is_on_same_ipc_ns( pscom_con_t *con )
{
  DEBUG_IN
	bool bReturn = false;
	int siLocalNodeID = pscom_get_nodeid();
	int siRemoteNodeID = con->pub.remote_con_info.node_id;

	/* local and remote node id's in list? */
	if( shm_nodeid_list( CONTAINER_LIST_GET, siLocalNodeID ) && shm_nodeid_list( CONTAINER_LIST_GET, siRemoteNodeID ) )
	{
		bReturn = true;

		if( siLocalNodeID == siRemoteNodeID )
		{
			DPRINT( 1, "*** WARNING:%s |%s| !!!ATTENTION!!! siLocalNodeID=%d == siRemoteNodeID=%d \
									--> Either containers are running in same UTS namespace \
									or both nodes are running in the same container and (shm plugin is disabled or has lower priority) cannot yet differentiate => allow SHM plugin for now!\n", spaces, __func__, siLocalNodeID, siRemoteNodeID );

			/* TODO we cannot yet guarantee that both nodes share the same IPC NS (because nodeid's are the identifiers!) */
      // bReturn = false;
      bReturn = true;
		}
	}

	if( bReturn )
  {
    /*
     * TODO is this a safe thing to to do? */
    signal( SIGINT, _shm_lxc_sighandler );
    // signal( 9, _shm_lxc_sighandler );

    DPRINT( 1, "***** DEBUG:%s |%s| [OK] siLocalNodeID=%d and siRemoteNodeID=%d are running on the same physical host!\n", spaces, __func__, siLocalNodeID, siRemoteNodeID );
  }
  else
  {
    DPRINT( 1, "***** DEBUG:%s |%s| [XX] siLocalNodeID=%d and siRemoteNodeID=%d are NOT running on the same physical host!\n", spaces, __func__, siLocalNodeID, siRemoteNodeID );
  }

  DPRINT( 1, "***** DEBUG:%s |%s| Printing list of known nodes...\n", spaces, __func__ );
  shm_nodeid_list( CONTAINER_LIST_PRINT, 0 );

  DEBUG_OUT
	return bReturn;
}




#ifdef CONTAINER_USE_SEMAPHORE
/*
 ** https://beej.us/guide/bgipc/output/html/multipage/semaphores.html#semsamp
 */
static int initsem( key_t key, int nsems, bool *bSemWasCreated )
{
  DEBUG_IN
	union semun
	{
		int val;
		struct semid_ds *buf;
		ushort *array;
	};

	int i;
	union semun arg;
	struct semid_ds buf;
	struct sembuf sb;
	int semid;
  int ret;
	*bSemWasCreated = false;

	semid = semget( key, nsems, IPC_CREAT | IPC_EXCL | 0666 );

	if( semid >= 0 )
	{ /* we got it first */
		DPRINT( 1, "***** DEBUG:%s |%s| semaphore (created and) locked\n", spaces, __func__ );
		sb.sem_op = 1;
		sb.sem_flg = 0;
		arg.val = 1;

		for( sb.sem_num = 0; sb.sem_num < nsems; sb.sem_num++ )
		{
			/* do a semop() to "free" the semaphores. */
			/* this sets the sem_otime field, as needed below. */
			if( semop( semid, &sb, 1 ) == -1 )
			{
				int e = errno;
				semctl( semid, 0, IPC_RMID ); /* clean up */
				errno = e;
				ret = -1; /* error, check errno */
        goto out;
			}
		}
		*bSemWasCreated = true;
	}
	else if( errno == EEXIST )
	{ /* some other node got it first */
		// DPRINT( 1, "***** DEBUG:%s |%s| semaphore already exists\n", spaces, __func__ );
		bool bReady = false;

		semid = semget( key, nsems, 0 ); /* get the id */
		if( semid < 0 )
    {
			ret = semid; /* error, check errno */
      goto out;
    }

		/* wait for other node to initialize the semaphore: */
		arg.buf = &buf;
		for( i = 0; i < CONTAINER_SEMAPHORE_MAX_RETRIES && !bReady; i++ )
		{
			semctl( semid, nsems - 1, IPC_STAT, arg );
			if( arg.buf->sem_otime != 0 )
			{
				DPRINT( 1, "***** DEBUG:%s |%s| semaphore (existing) locked\n", spaces, __func__ );
				bReady = true;
			}
			else
				usleep(1000);
		}
		if( !bReady )
		{
			errno = ETIME;
			ret = -1;
      goto out;
		}
	}
	else
	{
		ret = semid; /* error, check errno */
    goto out;
	}

out:
  DEBUG_OUT
	return semid;
}

static bool node_list_lock( bool bLock )
{
  DEBUG_IN

	/* sem */
	key_t key_sem;
	static int semid;
	struct sembuf sb;
	bool bSemWasCreated = false;
	int siRetVal = 0;

	sb.sem_num = 0;
	sb.sem_op = -1;  /* set to allocate resource */
	sb.sem_flg = SEM_UNDO;
	key_sem = CONTAINER_SHM_KEY+1;

	if( bLock )
	{
		/* grab the semaphore set */
		if( ( semid = initsem( key_sem, 1, &bSemWasCreated ) ) == -1 )
		{
			siRetVal = -1;
			DPRINT( 1, "***** ERROR:%s |%s| initsem: %s\n", spaces, __func__, strerror(errno) );
		}

		/* set return code */
		if( bSemWasCreated )
			siRetVal = 2;

		/* lock sem */
		if( semop(semid, &sb, 1) == -1 )
		{
			siRetVal = -1;
			DPRINT( 1, "***** ERROR:%s |%s| semop (lock): %s\n", spaces, __func__, strerror(errno) );
		}
	}
	else
	{
		/* unlock sem */
		sb.sem_op = 1;
		if ( semop(semid, &sb, 1) == -1 )
		{
			siRetVal = -1;
			DPRINT( 1, "***** ERROR:%s |%s| semop (unlock): %s\n", spaces, __func__, strerror(errno) );
		}
		else
			DPRINT( 1, "***** DEBUG:%s |%s| semaphore unlocked\n", spaces, __func__ );
	}

  DEBUG_OUT
	return siRetVal;
}
#endif /* CONTAINER_USE_SEMAPHORE */


/**
 * @brief
 *
 *
 *
 * @return
 */
bool shm_nodeid_list( nodeidlist_action action, int nodeid )
{
  DEBUG_IN
	bool bReturn = false;
	key_t key;
	int shmid;
	int *psiNodeIDs;
	unsigned int i;
	int f = -1; /* index to first free item (first fit) */

#ifdef CONTAINER_USE_SEMAPHORE
	bool bSemWasCreated = false;

	switch(action)
	{
		case CONTAINER_LIST_DEL:
		case CONTAINER_LIST_CLEAR:
		case CONTAINER_LIST_ADD:
			if( node_list_lock(true) == 2 )
				bSemWasCreated = true;
      // cntrNested++; /* Just for visualising locked region */
    default:
		break;
	}
#endif /* CONTAINER_USE_SEMAPHORE */

	/* Note: ftok cannot be used inside a container with different rootfs,
	 * because key is derived from inode of file amongst other identifiers */
	// const char *keyFile = "/tmp/test.key";
	// /* Make sure the file exists. */
	// int descriptor = open( keyFile, O_CREAT | O_RDWR, S_IRWXU );
	// /* Only wanted to make sure that the file exists. */
	// close( descriptor );
	// key = ftok(keyFile, 1);	// TODO will create a unique key for each container!

	/* Generate memory key. */
	// TODO which number to use?
	key = CONTAINER_SHM_KEY;

	if (key == -1)
		DPRINT(0, "***** ERROR:%s |%s| ftok: %s\n", spaces, __func__, strerror(errno) );

	// DPRINT( 1, "***** DEBUG:%s |%s| key=%d\n", spaces, __func__, key );

	/* Get the shared memory segment id */
	shmid = shmget( key, (CONTAINER_MAX_NODES+CONTAINER_MAX_NODES_OFFSET) * sizeof(int), IPC_CREAT | S_IRUSR | S_IWUSR );
	if( shmid == -1 )
		DPRINT( 1, "***** ERROR:%s |%s| shmget: %s\n", spaces, __func__, strerror(errno) );

	// DPRINT( 1, "***** DEBUG:%s |%s| shmid=%d\n", spaces, __func__, shmid );

	/* Attach the shared memory segment. */
	psiNodeIDs = shmat( shmid, NULL, 0 );
	if( psiNodeIDs == (int *) ( -1 ) )
		DPRINT( 1, "***** ERROR:%s |%s| shmat: %s\n", spaces, __func__, strerror(errno) );

  // /* remove shmid after usage */
  // shmctl( shmid, IPC_RMID, NULL );

#if defined(CONTAINER_USE_SHM_MAGIC_NUMBER) && defined(CONTAINER_USE_SEMAPHORE)
	/* check for magic number
   * TODO is this even necessary? */
	if( bSemWasCreated )
	{
		/* semaphore was just created -> set magic number */
		psiNodeIDs[0] = CONTAINER_SHM_MAGIC_NUMBER;
		DPRINT( 1, "***** DEBUG:%s |%s| magic number (%d) was set\n", spaces, __func__, CONTAINER_SHM_MAGIC_NUMBER );
	}
	else
	{
		/* semaphore already exists -> verify magic number */
		if( psiNodeIDs[0] == CONTAINER_SHM_MAGIC_NUMBER )
		{
			// DPRINT( 1, "***** DEBUG:%s |%s| magic number (%d) found\n", spaces, __func__, CONTAINER_SHM_MAGIC_NUMBER );
		}
		else
		{
			DPRINT( 1, "***** ERROR:%s |%s| magic number (%d) NOT found!\n", spaces, __func__, CONTAINER_SHM_MAGIC_NUMBER );
			bReturn = false;
      goto err_magic_number;
		}
	}
#endif /* CONTAINER_USE_SHM_MAGIC_NUMBER && CONTAINER_USE_SEMAPHORE */

	switch( action )
	{
		case CONTAINER_LIST_DEL:
			/* search for id */
			for( i=CONTAINER_MAX_NODES_OFFSET; i<CONTAINER_MAX_NODES+CONTAINER_MAX_NODES_OFFSET; i++ )
			{
				/* node id in list? */
				if( psiNodeIDs[i] == nodeid )
				{
					/* remove */
					DPRINT( 1, "***** DEBUG:%s |%s| removing psiNodeIDs[%d]=%d from list\n", spaces, __func__, i, psiNodeIDs[i] );
					psiNodeIDs[i] = 0;
					bReturn = true;
					break;
				}
			}
		break;

		case CONTAINER_LIST_CLEAR:
			DPRINT( 1, "***** DEBUG:%s |%s| clearing node id list\n", spaces, __func__ );
			for( i=CONTAINER_MAX_NODES_OFFSET; i<CONTAINER_MAX_NODES+CONTAINER_MAX_NODES_OFFSET; i++ )
			{
				if( psiNodeIDs[i] )
				{
					DPRINT( 1, "***** DEBUG:%s |%s| removing psiNodeIDs[%d]=%d from list\n", spaces, __func__, i, psiNodeIDs[i] );
					psiNodeIDs[i] = 0;
					bReturn = true;
				}
			}
		break;

		case CONTAINER_LIST_PRINT:
			/* print all known node id's */
			for( i=CONTAINER_MAX_NODES_OFFSET; i<=CONTAINER_MAX_NODES+CONTAINER_MAX_NODES_OFFSET; i++ )
			{
				if( psiNodeIDs[i] )
					DPRINT( 1, "***** DEBUG:%s |%s| psiNodeIDs[%d]=%d\n", spaces, __func__, i, psiNodeIDs[i] );
			}
			bReturn = true;
		break;

		case CONTAINER_LIST_ADD:
			/* search for id */
			for( i=CONTAINER_MAX_NODES_OFFSET; i<CONTAINER_MAX_NODES+CONTAINER_MAX_NODES_OFFSET; i++ )
			{
				/* already in list? */
				if( psiNodeIDs[i] == nodeid )
				{
          DPRINT( 1, "*** WARNING:%s |%s| node_id already in list: psiNodeIDs[%d]=%d\n", spaces, __func__, i, psiNodeIDs[i] );
          break;
				}

				/* update index to first free item */
				if( f == -1 )
					if( psiNodeIDs[i] == 0 )
						f = i;
			}
			/* add to array if necessary */
			if( i == CONTAINER_MAX_NODES+CONTAINER_MAX_NODES_OFFSET )
			{
				if( f > 0 )
				{
					psiNodeIDs[f] = nodeid;
					bReturn = true;
					DPRINT( 1, "***** DEBUG:%s |%s| adding to list: psiNodeIDs[%d]=%d\n", spaces, __func__, f, psiNodeIDs[f] );
				}
				else
					DPRINT( 1, "***** ERROR:%s |%s| psiNodeIDs[] is full!!!", spaces, __func__ );
			}
		break;

		case CONTAINER_LIST_GET:
			/* search for id */
			for( i=CONTAINER_MAX_NODES_OFFSET; i<CONTAINER_MAX_NODES+CONTAINER_MAX_NODES_OFFSET; i++ )
			{
				/* node id in list? */
				if( psiNodeIDs[i] == nodeid )
				{
					bReturn = true;
					break;
				}
			}
		break;
	}

	/* detach from segment */
	if( shmdt( psiNodeIDs ) == -1 )
		DPRINT( 1, "***** ERROR:%s |%s| shmdt: %s\n", spaces, __func__, strerror(errno) );


#ifdef CONTAINER_USE_SEMAPHORE
	switch(action)
	{
		case CONTAINER_LIST_DEL:
		case CONTAINER_LIST_CLEAR:
		case CONTAINER_LIST_ADD:
      // cntrNested--; /* Just for visualising locked region */
			node_list_lock(false);
    default:
		break;
	}
#endif /* CONTAINER_USE_SEMAPHORE */


err_magic_number:
  DEBUG_OUT
	return bReturn;
}

#endif /* SUPPORT_CONTAINERS */
