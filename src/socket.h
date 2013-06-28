#ifndef _SOCKET_H_
#define _SOCKET_H_

/* $Id: socket.h,v 1.8 2002/09/20 13:02:51 dfx Exp $ */

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdarg.h>

#include "config.h"

struct peer
{
	int fd;
	struct sockaddr_in sin;
	char tbuf[32];
};

/* Opens listening socket on specified port (in host byte order)
 * and ip (in network byte order, may be 0) and returns
 * the socket's fd or -1 on error */
int socket_listen(unsigned short port, unsigned long ip);

/* Accepts one new connection on given listening socket and fills the peer
 * struct with info. Will wait specified timeout msecs (0 = don't wait,
 * negative = wait infinitely). Returns -1 on error or -2 on timeout. */
int socket_accept(int fd, struct peer *, int timeout);

/* Creates a new socket and connects it to the specified host/port. The
 * peer struct will be filled with info. On error, a negative value is
 * returned: -1 == invalid values passed; -2 == resolve of host failed;
 * -3 == other resolver error; -4 == socket creation failed; -5 ==
 * connect failed. errno may not be meaningful in all cases.
 * Timeout is as in socket_accept. Returns -6 on timeout. If timed out
 * and timeout was == 0, connection will still be in progress. Otherwise,
 * if timeout != 0, and also on all other errors, socket
 * will be invalid and closed. Note that if timeout == 0,
 * the connection may still succeed immediately and the function will
 * return 0. Also note that the host resolving isn't taken into account
 * when doing the timeout. */
int socket_connect(struct peer *, char *host, int port, int timeout);

/* Printf's to a socket. Auto-closes it on error and returns -1. */
int socket_printf(struct peer *, char *format, ...)
	__attribute__ ((format (printf, 2, 3)));
int socket_vprintf(struct peer *, char *format, va_list);

/* Self descriptive */
void socket_close(struct peer *);

/* Fills the peer struct with info for local socket fd (getsockname) */
void socket_fill(int fd, struct peer *);

/* Same as above, but creates a new thread when a new connection is accepted.
 * The pointer that gets passed to the thread func should points to a
 * dynamically allocated context structure, which should contain the peer
 * structure containing the connection info. Example:
 *
 * struct ctx {
 *   struct peer;
 *   other_data;
 * };
 * struct ctx *ctx;
 * for (;;) {
 *   ctx = malloc(sizeof(*ctx));
 *   ctx->other_data = stuff;
 *   if (socket_accept_thread(fd, &ctx->peer, thread_func, ctx) == -1)
 *     break;
 * }
 *
 * Of course, if you don't need any additional data, you can omit the custom
 * struct and only use the peer struct. The thread func is responsible for
 * free'ing ctx/peer when it's done with it. */
int socket_accept_thread(int fd, struct peer *, void *(*func)(void *), void *arg);

/* Wrapper around read(), implementing timeout. Returns -2 on timeout. */
int socket_read(struct peer *, char *buf, int size, int timeout);

/* Wrapper around write(), handling partial writes and timeout. Returns -2
 * on timeout. */
int socket_write(struct peer *, char *buf, int size, int timeout);

/* Reads one line from fd into buf of given size. Last arg is timeout as in
 * socket_accept. Returns -1 on error, -2 on timeout, -3 on eof. Socket will
 * be closed automatically on error or eof. Note that when there's a timeout,
 * a partial line may be already read and discarded from the socket (and stored
 * into buf, but not null terminated), and another socket_readline may only return
 * the latter part of that line, not the complete line. Also, the timeout is
 * reset for each read(), so socket_readline may take longer than the specified
 * timeout to return. */
int socket_readline(struct peer *, char *buf, unsigned int bufsize, int timeout);

/* Returns the ip (1.2.3.4) from the peer struct. It is stored in peer->tbuf */
char *socket_ip(struct peer *);
/* Returns port number */
unsigned int socket_port(struct peer *);

#endif

