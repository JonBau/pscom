/*
 * Copyright (C) 2016 Thorsten Born
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Author: Thorsten Born <thorsten.born@rwth-aachen.de>
 */

#include "../pscom/pscom_shm.h"
#include "pscom_shm_lxc.h"
#include "shm_lxc.h"

static clock_t t;

static
int pscom_shm_lxc_con_init( pscom_con_t *con )
{
	DEBUG_IN
	int ret;

	ret = shm_is_containerized() ? 0 : -1;

	if( ret == 0 )
		shm_lxc_init();

	DEBUG_OUT
	return ret;
}


static
void shm_lxc_close( pscom_con_t *con )
{
	DEBUG_IN;
	shm_close( con );
	shm_nodeid_list( CONTAINER_LIST_DEL, pscom_get_nodeid() );
	DEBUG_OUT;
}


static
void pscom_shm_lxc_sock_init( pscom_sock_t *sock )
{
	DEBUG_IN;
	t = clock();
	pscom_shm_sock_init( sock );
	DEBUG_OUT;
}


static
void pscom_shm_lxc_sock_destroy( pscom_sock_t *sock )
{
	DEBUG_IN;
	if( shm_is_containerized() )
		shm_nodeid_list( CONTAINER_LIST_DEL, pscom_get_nodeid() );
	DEBUG_OUT;
}


static
void pscom_shm_lxc_destroy( void )
{
	DEBUG_IN;
	if( shm_is_containerized() )
		shm_nodeid_list( CONTAINER_LIST_DEL, pscom_get_nodeid() );
	DEBUG_OUT;
}


static
bool pscom_shm_lxc_test( pscom_con_t *con )
{
	bool bReturn = false;
	DEBUG_IN;

	/* with container support:
	 * no shared ns -> shm plugin will be skipped (because of different node id's)
	 * shared uts ns -> connection fails (will hang)
	 * shared ipc ns -> works
	 * shared uts + ipc ns -> works sometimes (update: seems to only work if the last container that joined
	 *																				is part of the connection or else connection will hang.
	 *																				So it's limited to only two containers!)
	 *
	 * w/ container support:
	 * no shared ns -> shm plugin will be skipped (because of different node id's)
	 * shared uts ns -> connection fails (will hang)
	 * shared ipc ns -> shm plugin will be skipped (because of different node id's)
	 * shared uts + ipc ns -> same as with container support
	 *
	 * => UTS namespace must NOT be shared for now!
	 */

	// /* is node running in container? */
	// if( ( shm_is_containerized() ) )
	// {
		/* determine whether this node and remote node are running on same physical host */
		bReturn = shm_is_on_same_ipc_ns(con);
	// }
	// else
	// {
	// 	if( con->pub.remote_con_info.node_id == pscom_get_nodeid() )
	// 	{
	// 		/* only triggers if prio of shm_lxc > shm or if shm plugin is disabled */
	// 		bReturn = true;
	// 	}
	// }

	if( !bReturn )
		DPRINT( 1, "***** DEBUG:%s |%s| [XX] Nodes are !NOT! running on the same physical host!\n", spaces, __func__ );
	else
		DPRINT( 1, "***** DEBUG:%s |%s| [OK] Nodes are running on the same physical host!\n", spaces, __func__ );

	// /* translate for pscom */
	// bReturn = bReturn ? 0 : -1;

	DEBUG_OUT;
	return bReturn;
}


static
void pscom_shm_lxc_handshake( pscom_con_t *con, int type, void *data, unsigned size )
{
	DEBUG_IN;
// DPRINT( 1, "***** DEBUG:%s |%s| ************************************ type=%d\n", spaces, __func__, type );


	precon_t *pre = con->precon;

	switch( type )
	{
		case PSCOM_INFO_ARCH_STEP1:
		{
			DPRINT( 1, "***** DEBUG:%s |%s| type=%s\n", spaces, __func__, "PSCOM_INFO_ARCH_STEP1 (PSCOM_INFO_SHM_SHMID)" );

			/* Are nodes in different ipc namespaces? --> arch next */
			if( !pscom_shm_lxc_test(con) )
			{
				/* Only one node can trigger ARCH_NEXT otherwise connection will hang */
				if( pscom_get_nodeid() < con->pub.remote_con_info.node_id )
				{
					DPRINT( 1, "***** DEBUG:%s |%s| ****** SKIPPING SHM_LXC PLUGIN\n", spaces, __func__ );
					pscom_precon_send_PSCOM_INFO_ARCH_NEXT(pre);
				}

				goto out;
			}
		}
		break;
	}

	/* pass to actual shm plugin */
	pscom_shm_handshake( con, type, data, size );

	/* overwrite function pointer to close */
	con->close = shm_lxc_close;

	if( type == PSCOM_INFO_EOF )
	{
		t = clock() - t;
		double time_taken = ((double)t)/CLOCKS_PER_SEC*1000; // in ms
		DPRINT( 1, "///// DEBUG:%s |%s| (SHM_LXC) took %.3f ms to initialize\n", spaces, __func__, time_taken );
	}

out:
	DEBUG_OUT;
	return;
}


pscom_plugin_t pscom_plugin = {
	.name		= "shm_lxc",
	.version	= PSCOM_PLUGIN_VERSION,
	.arch_id	= PSCOM_ARCH_SHM_LXC,
	.priority	= PSCOM_SHM_LXC_PRIO,

	.init	= NULL,
	.destroy = pscom_shm_lxc_destroy,
	.sock_init = pscom_shm_lxc_sock_init,
	.sock_destroy = pscom_shm_lxc_sock_destroy,
	.con_init	= pscom_shm_lxc_con_init,
	.con_handshake = pscom_shm_lxc_handshake,
};
