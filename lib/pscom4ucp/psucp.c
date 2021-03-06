/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * All rights reserved.
 *
 * Author:	Jens Hauke <hauke@par-tec.com>
 */

#include "psucp.h"
#include "pscom_priv.h"

#include <ucp/api/ucp.h>
#include <ucp/api/ucp_def.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>



#define MIN(a,b)      (((a)<(b))?(a):(b))
#define MAX(a,b)      (((a)>(b))?(a):(b))

struct hca_info {
	ucp_worker_h	ucp_worker;
	ucp_context_h	ucp_context;

	ucp_address_t	*my_ucp_address;
	size_t		my_ucp_address_size;

	struct list_head pending_requests; // List of psucp_req_t.next
};



/* UCP specific information about one connection */
struct psucp_con_info {
	ucp_ep_h	ucp_ep;

	/* low level */
	hca_info_t	*hca_info;

	uint64_t	remote_tag;

	/* misc */
	void		*con_priv;		/* priv data from psucp_con_init() */

	int con_broken;
};


typedef struct psucp_req {
	struct list_head	next; // list struct hca_info.pending_requests.
	void	*req_priv;
	void	(*cb)(void *req_priv);
	int	completed;
} psucp_req_t;


static hca_info_t  default_hca;

char *psucp_err_str = NULL; /* last error string */

int psucp_debug = 2;
FILE *psucp_debug_stream = NULL;


#define psucp_dprint(level,fmt,arg... )					\
do {									\
	if ((level) <= psucp_debug) {					\
		fprintf(psucp_debug_stream ? psucp_debug_stream : stderr, \
			"ucp:" fmt "\n",##arg);				\
	}								\
} while(0);


static
void psucp_err(char *str)
{
	if (psucp_err_str) free(psucp_err_str);

	psucp_err_str = str ? strdup(str) : strdup("");
	return;
}


static
void psucp_err_status(char *str, ucs_status_t status)
{
	const char *err_str = ucs_status_string(status);
	int len = strlen(str) + strlen(err_str) + 10;
	char *msg = malloc(len);

	assert(msg);

	strcpy(msg, str);
	strcat(msg, " : ");
	strcat(msg, err_str);

	psucp_err(msg);
	free(msg);
}


static
void psucp_cleanup_hca(hca_info_t *hca_info)
{
	// ToDo: Implement cleanup
}


static
void psucp_req_init(void *_req) {
	psucp_req_t *psucp_req = (psucp_req_t *)_req;
	psucp_req->req_priv = NULL;
	psucp_req->completed = 0;
	psucp_req->cb = NULL;
	INIT_LIST_HEAD(&psucp_req->next); // allow multiple dequeues
}


static
void psucp_pending_req_enqueue(psucp_req_t *psucp_req) {
	hca_info_t *hca_info = &default_hca;

	list_add_tail(&psucp_req->next, &hca_info->pending_requests);
}


static
void psucp_pending_req_dequeue(psucp_req_t *psucp_req) {
//	hca_info_t *hca_info = &default_hca;
	list_del_init(&psucp_req->next);
}


static
void psucp_pending_req_attach(psucp_req_t *psucp_req) {
	if (!psucp_req) return;
	psucp_pending_req_enqueue(psucp_req);
}


static
void psucp_pending_req_release(psucp_req_t *psucp_req) {
	psucp_pending_req_dequeue(psucp_req);

	// Call psucp_req_init. ucp_request_release() move the request to
	// the request pool and do NOT call psucp_req_init() before reusing it!
	psucp_req_init(psucp_req);
	ucp_request_release(psucp_req);
}


static
int psucp_pending_req_progress(psucp_req_t *psucp_req) {
	if (psucp_req->completed) {
		if (psucp_req->cb) {
			psucp_req->cb(psucp_req->req_priv);
		}
		psucp_pending_req_release(psucp_req);
		return 1;
	} else {
		return 0;
	}
}


static
void psucp_req_done(void *request, ucs_status_t status) {
	psucp_req_t *psucp_req = (psucp_req_t *)request;
	psucp_req->completed = 1;
}


static
int psucp_init_hca(hca_info_t *hca_info)
{
	int rc;
	ucs_status_t status;
	ucp_config_t *config;
	ucp_params_t ucp_params;

	memset(hca_info, 0, sizeof(*hca_info));

	/* UCP initialization */
	status = ucp_config_read(NULL, NULL, &config);
	assert(status == UCS_OK);

	ucp_params.features        = UCP_FEATURE_TAG;
	ucp_params.request_size    = sizeof(psucp_req_t);
	ucp_params.request_init    = psucp_req_init;
	ucp_params.request_cleanup = NULL;

	INIT_LIST_HEAD(&hca_info->pending_requests);

	status = ucp_init(&ucp_params, config, &hca_info->ucp_context);
	if (status != UCS_OK) goto err_init;

	// ucp_config_print(config, stdout, NULL, UCS_CONFIG_PRINT_CONFIG);
	ucp_config_release(config);

	status = ucp_worker_create(hca_info->ucp_context, UCS_THREAD_MODE_SINGLE,
				   &hca_info->ucp_worker);
	if (status != UCS_OK) goto err_worker;

	status = ucp_worker_get_address(hca_info->ucp_worker,
					&hca_info->my_ucp_address, &hca_info->my_ucp_address_size);
	if (status != UCS_OK) goto err_worker;

	return 0;
	/* --- */
err_worker:
	psucp_cleanup_hca(hca_info);
err_init:
	return -1;
}


int psucp_init(void)
{
	static int init_state = 1;
	if (init_state == 1) {
		if (psucp_init_hca(&default_hca)) goto err_hca;

		init_state = 0;
	}

	return init_state; /* 0 = success, -1 = error */
	/* --- */
err_hca:
	init_state = -1;
	psucp_dprint(1, "UCP disabled : %s", psucp_err_str);
	return -1;
}


int psucp_progress(void) {
	hca_info_t *hca_info = &default_hca;
	struct list_head *pos, *next;
	int progress = 0;

	list_for_each_safe(pos, next, &hca_info->pending_requests) {
		psucp_req_t *psucp_req = list_entry(pos, psucp_req_t, next);

		progress |= psucp_pending_req_progress(psucp_req);
	}
	return progress;
}


void psucp_con_cleanup(psucp_con_info_t *con_info)
{
	hca_info_t *hca_info = con_info->hca_info;
}


int psucp_con_init(psucp_con_info_t *con_info, hca_info_t *hca_info, void *con_priv)
{
	unsigned int i;

	if (!hca_info) hca_info = &default_hca;
	memset(con_info, 0, sizeof(*con_info));

	con_info->hca_info = hca_info;
	con_info->con_priv = con_priv;
	con_info->con_broken = 0;

	return 0;
	/* --- */
err_alloc:
	psucp_con_cleanup(con_info);
	psucp_dprint(1, "psucp_con_init() : %s", psucp_err_str);
	return -1;
}


int psucp_con_connect(psucp_con_info_t *con_info, psucp_info_msg_t *info_msg)
{
	hca_info_t *hca_info = con_info->hca_info;
	int rc;
	ucs_status_t status;

	con_info->remote_tag = info_msg->tag;

	status = ucp_ep_create(hca_info->ucp_worker, (ucp_address_t *)info_msg->addr, &con_info->ucp_ep);
	if (status != UCS_OK) goto err_ep_create;

	return 0;
	/* --- */
err_ep_create:
	psucp_err_status("ucp_ep_create()", status);
	psucp_dprint(1, "psucp_con_connect() : %s", psucp_err_str);
	return -1;
}


psucp_con_info_t *psucp_con_create(void)
{
	psucp_con_info_t *con_info = malloc(sizeof(*con_info));
	memset(con_info, 0, sizeof(*con_info));
	return con_info;
}


void psucp_con_free(psucp_con_info_t *con_info)
{
	free(con_info);
}


void psucp_con_get_info_msg(psucp_con_info_t *con_info /* in */,
			    unsigned long tag,
			    psucp_info_msg_t *info_msg /* out */)
{
	int rc;
	hca_info_t *hca_info = con_info->hca_info;

	if (hca_info->my_ucp_address_size > sizeof(info_msg->addr)) {
		printf("psucp_info_msg_t.addr to small! Should be at least %zu!\n", hca_info->my_ucp_address_size);
		// ToDo: Error recovery
	}
	info_msg->size = hca_info->my_ucp_address_size;
	memcpy(info_msg->addr, hca_info->my_ucp_address, MIN(sizeof(info_msg->addr), info_msg->size));

	info_msg->tag = tag;
}


int psucp_sendv(psucp_con_info_t *con_info, struct iovec *iov, unsigned size,
		void (*cb)(void *req_priv), void *req_priv)
{
	ucs_status_ptr_t request;
	psucp_req_t *psucp_req;
	unsigned tosend_data;

	assert(size >= iov[0].iov_len);

	//printf("%s:%u:%s send head %3u of %3u : %s\n",
	//      __FILE__, __LINE__, __func__, (unsigned)iov[0].iov_len, size,
	//     pscom_dumpstr(iov[0].iov_base, iov[0].iov_len));
	tosend_data = size - iov[0].iov_len;
	request = ucp_tag_send_nb(con_info->ucp_ep,
				  iov[0].iov_base, iov[0].iov_len,
				  ucp_dt_make_contig(1),
				  con_info->remote_tag,
				  /*(ucp_send_callback_t)*/psucp_req_done);
	if (UCS_PTR_IS_ERR(request)) goto err_send_header;

	psucp_pending_req_attach(request);

	if (tosend_data) {
		//printf("%s:%u:%s send data %3u : %s\n",
		//      __FILE__, __LINE__, __func__, tosend,
		//     pscom_dumpstr(iov[1].iov_base, tosend));
		assert(iov[1].iov_len >= tosend_data);

		request = ucp_tag_send_nb(con_info->ucp_ep,
					  iov[1].iov_base, tosend_data,
					  ucp_dt_make_contig(1),
					  con_info->remote_tag,
					  /*(ucp_send_callback_t)*/psucp_req_done);
		if (UCS_PTR_IS_ERR(request)) goto err_send_data;

		psucp_pending_req_attach(request);
	}
	if (request) {
		psucp_req = (psucp_req_t *)request;
		psucp_req->cb = cb;
		psucp_req->req_priv = req_priv;
	} else {
		if (cb) cb(req_priv);
	}


	return size;
err_send_data:
err_send_header:
	{
		ucs_status_t status = UCS_PTR_STATUS(request);

		psucp_err_status("ucp_tag_send_nb()", status);
		psucp_dprint(2, "psucp_sendv() : %s", psucp_err_str);
	}
	return -EPIPE;;
}


int psucp_probe(psucp_msg_t *msg)
{
	hca_info_t *hca_info = &default_hca;

	msg->msg_tag = ucp_tag_probe_nb(
		hca_info->ucp_worker,
		0 /* tag */, (ucp_tag_t)0 /* tag mask any */,
		1 /* remove */,
		&msg->info_tag);

	if (msg->msg_tag == NULL) {
		ucp_worker_progress(hca_info->ucp_worker);
		// psucp_progress();
		return 0;
	}
	assert(msg->info_tag.length > 0);

	return msg->info_tag.length;
}


static
int recv_in_progress = 0;

static
void recv_handle(void *request, ucs_status_t status,
		 ucp_tag_recv_info_t *info)
{
	recv_in_progress--;
}


ssize_t psucp_recv(psucp_msg_t *msg, void *buf, size_t size)
{
	hca_info_t *hca_info = &default_hca;
	struct ucx_context *request;
	// psucp_req_t *psucp_req;

	size_t len = size < msg->info_tag.length ? size : msg->info_tag.length;

	request = ucp_tag_msg_recv_nb(hca_info->ucp_worker,
				      buf, len,
				      ucp_dt_make_contig(1), msg->msg_tag,
				      recv_handle);

	if (UCS_PTR_IS_ERR(request)) {
		goto err_recv;
	}
	recv_in_progress++;

	// psucp_req = (psucp_req_t *)request;

	// ToDo: rewrite to be non-blocking.
	while (recv_in_progress) {
		ucp_worker_progress(hca_info->ucp_worker);
	}

	ucp_request_release(request);

	//printf("%s:%u:%s recv %u : %s\n", __FILE__, __LINE__, __func__,
	//      (unsigned) len, pscom_dumpstr(buf, len));
	return len;

err_recv:
	{
		ucs_status_t status = UCS_PTR_STATUS(request);

		psucp_err_status("ucp_tag_msg_recv_nb()", status);
		psucp_dprint(2, "psucp_recv() : %s", psucp_err_str);
	}
	return -EPIPE;;
}
