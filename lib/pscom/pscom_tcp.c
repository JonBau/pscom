/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Author:	Jens Hauke <hauke@par-tec.com>
 */
/**
 * pscom_tcp.c: tcp communication
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "pscom_priv.h"
#include "pscom_tcp.h"
#include "pscom_ufd.h"
#include "pscom_precon.h"
#include "pscom_con.h"
#include <fcntl.h>


static
void tcp_do_read(ufd_t *ufd, ufd_funcinfo_t *ufd_info)
{
	pscom_con_t *con = (pscom_con_t *) ufd_info->priv;

	char *buf;
	size_t len;
	ssize_t rlen;

	pscom_read_get_buf(con, &buf, &len);

	rlen = recv(con->arch.tcp.ufd_info.fd, buf, len, MSG_NOSIGNAL | MSG_DONTWAIT);
	if (rlen >= 0) {
		pscom_read_done(con, buf, rlen);
	} else if ((errno != EINTR) && (errno != EAGAIN)) {
		goto err_con_broken;
	}

	return;
	/* --- */
err_con_broken:
	pscom_con_error(con, PSCOM_OP_READ, PSCOM_ERR_STDERROR);
	return;
}


static
void tcp_write_stop(pscom_con_t *con)
{
	D_TR(printf("write stop tcp\n"));
	ufd_event_clr(&pscom.ufd, &con->arch.tcp.ufd_info, POLLOUT);
}


static
void _tcp_do_write(pscom_con_t *con)
{
	struct iovec iov[2];
	struct msghdr msg;
	pscom_req_t *req;

	req = pscom_write_get_iov(con, iov);

	if (req) {
		ssize_t len;
		// len = writev(con->arch.tcp.con_fd,
		//		con->out.iov, con->out.count);
		msg.msg_iov = iov;
		msg.msg_iovlen = 2;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;

		len = sendmsg(con->arch.tcp.ufd_info.fd, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);

		if (len >= 0) {
			pscom_write_done(con, req, len);
		} else if ((errno != EINTR) && (errno != EAGAIN)) {
			goto err_con_broken;
		}
	}
	return;
	/* error code */
err_con_broken:
	pscom_con_error(con, PSCOM_OP_WRITE, PSCOM_ERR_STDERROR);
	return;
}


static
void tcp_do_write(ufd_t *ufd, ufd_funcinfo_t *ufd_info)
{
	_tcp_do_write((pscom_con_t *) ufd_info->priv);
}


static
void tcp_write_start(pscom_con_t *con)
{
	D_TR(printf("write start tcp\n"));

	/* was there a state change request? */
	if (pscom.migration_state == PSCOM_MIGRATION_REQUESTED) {
		pscom_migration_handle_shutdown_req();

	} else {
		/* only send if con is not suspended */
		if(!con->write_is_suspended) {
			ufd_event_set(&pscom.ufd, &con->arch.tcp.ufd_info, POLLOUT);
			_tcp_do_write(con);
			/* Dont do anything after this line.
			   _tcp_do_write() can reenter tcp_set_write_start()! */
		}
	}

	/* ensure to write on resume of the connection */
	con->write_is_signaled = 1;
}


static
void tcp_read_start(pscom_con_t *con)
{
	/* was there a shutdown request? */
	if (pscom.migration_state == PSCOM_MIGRATION_REQUESTED) {
		pscom_migration_handle_shutdown_req();

	} else {
		/* only recv if con is not suspended */
		if(!con->read_is_suspended) {
			D_TR(printf("read start tcp\n"));
			ufd_event_set(&pscom.ufd, &con->arch.tcp.ufd_info, POLLIN);
		}
	}
	
	/* ensure to recv on resume of the connection */
	con->read_is_signaled = 1;
}


static
void tcp_read_stop(pscom_con_t *con)
{
	D_TR(printf("read stop tcp\n"));
	ufd_event_clr(&pscom.ufd, &con->arch.tcp.ufd_info, POLLIN);
}


static
void tcp_close(pscom_con_t *con)
{
	int con_fd = con->arch.tcp.ufd_info.fd;
	if (con_fd >= 0) {
		ufd_del(&pscom.ufd, &con->arch.tcp.ufd_info);
		close(con_fd);
		con->arch.tcp.ufd_info.fd = -1;
	}
}


static
void tcp_set_fd(pscom_con_t *con, int con_fd)
{
	con->arch.tcp.ufd_info.fd = con_fd;
}


static
void tcp_init_con(pscom_con_t *con)
{
	int ret;
	int con_fd = con->arch.tcp.ufd_info.fd;
	assert(con_fd >= 0);

	con->pub.type = PSCOM_CON_TYPE_TCP;

	con->write_start = tcp_write_start;
	con->write_stop = tcp_write_stop;
	con->read_start = tcp_read_start;
	con->read_stop = tcp_read_stop;
	con->close = tcp_close;

	ret = fcntl(con_fd, F_SETFL, O_NONBLOCK);
	if (ret == -1) {
		DPRINT(1, "tcp_init_con(): fcntl(%d, F_SETFL, O_NONBLOCK) failed : %s",
		       con_fd, strerror(errno));
	}

	memset(&con->arch.tcp.ufd_info, 0, sizeof(con->arch.tcp.ufd_info));

	con->arch.tcp.ufd_info.fd = con_fd;
	con->arch.tcp.ufd_info.can_read = tcp_do_read;
	con->arch.tcp.ufd_info.can_write = tcp_do_write;
	con->arch.tcp.ufd_info.priv = con;

	ufd_add(&pscom.ufd, &con->arch.tcp.ufd_info);

	pscom_con_setup_ok(con);
}


/****************************************************************/
static
void pscom_tcp_sock_init(pscom_sock_t *sock)
{
}


static
void pscom_tcp_handshake(pscom_con_t *con, int type, void *data, unsigned size)
{
	precon_t *pre = con->precon;
	switch (type) {
	case PSCOM_INFO_ARCH_REQ:
		pre->closefd_on_cleanup = 0; // Keep fd after usage
		tcp_set_fd(con, pre->ufd_info.fd);
		pscom_precon_send(pre, PSCOM_INFO_ARCH_OK, NULL, 0);
		break;
	case PSCOM_INFO_EOF:
		tcp_init_con(con);
		break;
	}
}


static
int pscom_tcp_con_init(pscom_con_t *con)
{
	return 0;
}


pscom_plugin_t pscom_plugin_tcp = {
	.name		= "tcp",
	.version	= PSCOM_PLUGIN_VERSION,
	.arch_id	= PSCOM_ARCH_TCP,
	.priority	= PSCOM_TCP_PRIO,
	.properties     = PSCOM_PLUGIN_PROP_NOT_MIGRATABLE,

	.init		= NULL,
	.destroy	= NULL,
	.sock_init	= pscom_tcp_sock_init,
	.sock_destroy	= NULL,
	.con_init	= pscom_tcp_con_init,
	.con_handshake	= pscom_tcp_handshake,
};
