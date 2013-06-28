#ifndef _WC_SERV_H_
#define _WC_SERV_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <libxml/parser.h>

#include "socket.h"

struct wc_ctx
{
	int port;
	int listen_fd;
};

struct peer_ctx
{
	struct peer peer;
	struct module_ctx *ctx;
};

#endif

