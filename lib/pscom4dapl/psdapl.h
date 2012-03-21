/*
 * ParaStation
 *
 * Copyright (C) 2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Author:	Jens Hauke <hauke@par-tec.com>
 */
/**
 * psdapl.c: DAPL/Infiniband communication
 */

#ifndef _PSDAPL_H_
#define _PSDAPL_H_

#include <stdint.h>
#include <stdio.h>
#include <sys/uio.h>
#include "dat/udat.h"

typedef struct psdapl_con_info psdapl_con_info_t;
typedef struct psdapl_socket psdapl_socket_t;

/* Create a socket.
 * return NULL on error.
 * Will be automaticaly freed by last psdapl_con_destroy()
 * (socket->use_cnt == 0), or you must call psdapl_socket_hold().
 */
psdapl_socket_t *psdapl_socket_create(void);

/* Hold one ref to socket */
void psdapl_socket_hold(psdapl_socket_t *socket);

/* Put one ref. Destroy socket, if use_cnt == 0 */
void psdapl_socket_put(psdapl_socket_t *socket);


/* Create a con_info usable for psdapl_accept_wait() or
 * psdapl_connect().
 * return NULL on error */
psdapl_con_info_t *psdapl_con_create(psdapl_socket_t *socket);


/* create a PSP to listen on a port.
 * return: DAT_CONN_QUAL (the port) in socket->listen_conn_qual and
 * the address in DAT_SOCK_ADDR socket->sock_addr (already set by
 * psdapl_socket_create()
 *
 * return -1 on error. */
int psdapl_listen(psdapl_socket_t *socket);


/* Wait for a new connection on the PSP (psdapl_listen()).
 *
 * return -1 on error */
int psdapl_accept_wait(psdapl_con_info_t *ci);


/* Connect a remote PSP at addr : conn_qual
 * return -1 on error */
int psdapl_connect(psdapl_con_info_t *ci, DAT_SOCK_ADDR *addr, DAT_CONN_QUAL conn_qual);


/* Close connection ci and free resources */
void psdapl_con_destroy(psdapl_con_info_t *ci);


/* send size bytes from iov through ci.
 * return number of bytes send or:
 * -EAGAIN if ci is busy or
 * -EPIPE in case of a broken connection.
 *
 * (sending with size = 0, will send a 0 message. But you will not
 * receive this message with psdapl_recvlook! To send EOF, use
 * psdapl_send_eof().) *
 */
int psdapl_sendv(psdapl_con_info_t *ci, struct iovec *iov, int size);


/* Send EOF and close sending side of connection. Further psdapl_sendv()
 * are allowed, but will return -EPIPE. */
void psdapl_send_eof(psdapl_con_info_t *ci);


/* Start receiving.
 * return:
 * number of bytes received or
 * 0 for EOF (from psdapl_send_eof()) or
 * -EAGAIN nothing received or
 * -EPIPE broken connction.
 * (call psdapl_recvdone after usage of *buf!)
 */
int psdapl_recvlook(psdapl_con_info_t *ci, void **buf);

/* End receiving. */
void psdapl_recvdone(psdapl_con_info_t *ci);

extern int psdapl_debug;
extern FILE *psdapl_debug_stream;


const char *psdapl_addr2str(DAT_SOCK_ADDR *addr, DAT_CONN_QUAL conn_qual);
/* return -1 on parse error */
int psdapl_str2addr(DAT_SOCK_ADDR *addr, DAT_CONN_QUAL *conn_qual, const char *str);


DAT_SOCK_ADDR *psdapl_socket_get_addr(psdapl_socket_t *socket);
DAT_CONN_QUAL psdapl_socket_get_conn_qual(psdapl_socket_t *socket);


/*
 * RDMA calls
 */

typedef struct psdapl_mregion_cache psdapl_mregion_cache_t;

typedef struct psdapl_rdma_req psdapl_rdma_req_t;
struct psdapl_rdma_req {
	psdapl_con_info_t *ci;
	DAT_RMR_CONTEXT rmr_context;
	DAT_VADDR	rmr_vaddr;
	char		*lmr_buf;
	size_t		size;

	psdapl_mregion_cache_t *mreg; /* set by psdapl_post_rma_put() */

	void		(*io_done)(psdapl_rdma_req_t *req);
	void		*priv;
};


/* get lmr and rmr Handles from mem region buf:size. from cache.
 * call psdapl_put_mregion() after usage!
 * return NULL on error. */
psdapl_mregion_cache_t *
psdapl_get_mregion(void *buf, size_t size, psdapl_con_info_t *ci);

void psdapl_put_mregion(psdapl_mregion_cache_t *mreg);

/* return -1 on error */
int psdapl_post_rdma_put(psdapl_rdma_req_t *req);

/* return -1 on error */
int psdapl_post_rdma_get(psdapl_rdma_req_t *req);


static inline
DAT_VADDR psdapl_mem2vaddr(char *mem)
{
	return (DAT_VADDR)(long)mem;
}


static inline
DAT_VADDR psdapl_get_rmr_vaddr(void *buf)
{
	return psdapl_mem2vaddr(buf);
}


DAT_RMR_CONTEXT psdapl_get_rmr_context(psdapl_mregion_cache_t *mreg);


#endif /* _PSDAPL_H_ */
