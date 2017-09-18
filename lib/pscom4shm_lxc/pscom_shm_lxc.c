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
 */

#include "../pscom/pscom_shm.h"
#include "pscom_shm_lxc.h"
#include "shm_lxc.h"

static
int pscom_shm_lxc_con_init( pscom_con_t *con )
{
	int ret;

	ret = shm_is_containerized() ? 0 : -1;

	if( ret == 0 )
		shm_lxc_init();

	return ret;
}


static
void shm_lxc_close( pscom_con_t *con )
{
	shm_close( con );
}


static
void pscom_shm_lxc_sock_init( pscom_sock_t *sock )
{
	pscom_shm_sock_init( sock );
}


static
void pscom_shm_lxc_sock_destroy( pscom_sock_t *sock )
{	//TODO
	// if( shm_is_containerized() )
	//	shm_nodeid_list( CONTAINER_LIST_DEL, pscom_get_nodeid() );
}


static
void pscom_shm_lxc_destroy( void )
{	//TODO	: clear containerization cache (?) &  local uuid !!
	//if( shm_is_containerized() )
	//	shm_nodeid_list( CONTAINER_LIST_DEL, pscom_get_nodeid() );
}


static
int pscom_connecting_state(pscom_con_t *con)
{
       	return ((con->pub.state == PSCOM_CON_STATE_CONNECTING) || (con->pub.state == PSCOM_CON_STATE_CONNECTING_ONDEMAND));
}

#define PSCOM_INFO_SHM_LXC_UUID PSCOM_INFO_ARCH_STEP2
#define PSCOM_INFO_SHM_LXC_UUID_OK PSCOM_INFO_ARCH_STEP3

static
void pscom_shm_lxc_handshake( pscom_con_t *con, int type, void *data, unsigned size )
{
	precon_t *pre = con->precon;

	switch (type) {
        	case PSCOM_INFO_ARCH_REQ:
			{
			uuid_t* local_uuid = shm_lxc_get_local_uuid();
			pscom_precon_send(pre, PSCOM_INFO_SHM_LXC_UUID, local_uuid, sizeof(uuid_t));
			break;
	    	}

		case PSCOM_INFO_SHM_LXC_UUID:
		{
			uuid_t* remote_uuid = data;
			assert(size == sizeof(uuid_t));

				/* Extended Debugging */
				if(pscom.env.debug >= 2)
				{
			   		char local_uuid_str[37], remote_uuid_str[37];
			   		uuid_unparse_lower(*(shm_lxc_get_local_uuid()), local_uuid_str);
			   		uuid_unparse_lower(*remote_uuid, remote_uuid_str);
			   		DPRINT(2,"local uuid: %s ### remote uuid: %s",local_uuid_str, remote_uuid_str);
				}

			/* Compare local and remote uuids */
			if (uuid_compare(*remote_uuid, *(shm_lxc_get_local_uuid())) == 0)
			{
				DPRINT( 2, "UUID MATCH (OK!)");
				pscom_precon_send(pre, PSCOM_INFO_SHM_LXC_UUID_OK, NULL, 0); //Acknowledge the correct uuid
			} else
			{
				DPRINT( 1, "UUID MISMATCH (BAD!)");
			 	/* Only one node should trigger ARCH_NEXT - otherwise connection will hang */
				if (pscom_connecting_state(con)) pscom_precon_send_PSCOM_INFO_ARCH_NEXT(pre);
			}
			break;
		}

		case PSCOM_INFO_SHM_LXC_UUID_OK:
		{
			/* hook back to original handshake by manually setting type to PSCOM_INFO_ARCH_REQ to initialize original handshake*/
			int hook_type = PSCOM_INFO_ARCH_REQ;
			pscom_shm_handshake( con, hook_type, data, size );
			break;
		}

		case PSCOM_INFO_EOF:
		{
			pscom_shm_handshake( con, type, data, size );
			/* hook into shm *connection* control functions by overwriting function pointers */
			con->close = shm_lxc_close;
			break;
		}

		default:
		{
			/* pass to actual shm plugin */
			pscom_shm_handshake( con, type, data, size );
			break;
		}
	}

out:
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
