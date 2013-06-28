#ifndef _HTTP_H_
#define _HTTP_H_

#include <libxml/parser.h>

#include "socket.h"

struct module_ctx;

struct http_ctx
{
	int listen_fd;
	unsigned int port;
};

struct http_peer
{
	struct peer peer;
	struct module_ctx *mod_ctx;
};

#endif

