#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/poll.h>
#include <fcntl.h>

#include "config.h"

#define MODULE_GENERIC
#include "module.h"
#include "socket.h"

char *name = "socket";
char *version = VERSION;

int
socket_listen(unsigned short port, unsigned long ip)
{
	int fd;
	int ret;
	struct sockaddr_in sin;
	
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	
	fcntl(fd, F_SETFL, O_NONBLOCK);
	ret = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &ret, sizeof(ret));
	
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = ip;
	ret = bind(fd, (struct sockaddr *) &sin, sizeof(sin));
	if (ret)
	{
		close(fd);
		return -1;
	}
	ret = listen(fd, 5);
	if (ret)
	{
		close(fd);
		return -1;
	}
	
	return fd;
}

int
socket_accept(int fd, struct peer *peer, int timeout)
{
	int newfd, socklen;
	struct sockaddr_in sin;
	int ret;
	struct pollfd pfd;
	
	if (fd < 0)
		return -1;
	
	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = fd;
	pfd.events = POLLIN | POLLERR | POLLHUP;
	
	ret = poll(&pfd, 1, timeout);
	if (ret < 0)
		return -1;
	if (ret == 0)
		return -2;
	
	socklen = sizeof(peer->sin);
	newfd = accept(fd, (struct sockaddr *) &sin, &socklen);
	if (newfd == -1)
		return -1;
	
	fcntl(newfd, F_SETFL, O_NONBLOCK);
	
	memset(peer, 0, sizeof(*peer));
	peer->fd = newfd;
	memcpy(&peer->sin, &sin, sizeof(peer->sin));

	return 0;
}

int
socket_accept_thread(int fd, struct peer *peer, void *(*func)(void *), void *arg)
{
	int newfd;
	pthread_attr_t attr;
	pthread_t tid;
	
	newfd = socket_accept(fd, peer, -1);
	if (newfd == -1)
		return -1;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&tid, &attr, func, arg);
	pthread_attr_destroy(&attr);
	
	return 0;
}

int
socket_readline(struct peer *peer, char *buf, unsigned int bufsize, int timeout)
{
	int ret;
	unsigned int count;
	
	if (peer->fd < 0)
		return -1;
	
	count = 0;
	for (;;)
	{
		ret = socket_read(peer, buf, 1, timeout);

		if (ret == -2)
			return -2;
		if (ret == 0)
		{
			socket_close(peer);
			return -3;
		}
		if (ret != 1)
		{
closeerr:
			socket_close(peer);
			return -1;
		}
		if (*buf == '\n')
			break;
		buf++;
		count++;
		if (count >= bufsize)
			goto closeerr;
	}
	
	*buf-- = '\0';
	if (count >= 1 && *buf == '\r')
		*buf = '\0';
		
	return 0;
}

char *
socket_ip(struct peer *peer)
{
	snprintf(peer->tbuf, sizeof(peer->tbuf) - 1,
		"%u.%u.%u.%u",
		(peer->sin.sin_addr.s_addr >>  0) & 0xff,
		(peer->sin.sin_addr.s_addr >>  8) & 0xff,
		(peer->sin.sin_addr.s_addr >> 16) & 0xff,
		(peer->sin.sin_addr.s_addr >> 24) & 0xff);
	return peer->tbuf;
}

unsigned int
socket_port(struct peer *peer)
{
	return (unsigned int) ntohs(peer->sin.sin_port);
}

int
socket_connect(struct peer *peer, char *host, int port, int timeout)
{
	struct peer p;
	int ret;
	struct hostent hent, *hentp;
	int herrno;
	int bufsize;
	char *buf;
	struct pollfd pfd;
	
	if (!peer || !host || !*host)
		return -1;
	if (port <= 0 || port > 0xffff)
		return -1;
	
	memset(&p, 0, sizeof(p));
	
	ret = inet_aton(host, &p.sin.sin_addr);
	if (!ret)
	{
		bufsize = 512;
		for (;;)
		{
			buf = malloc(bufsize);
			errno = 0;
			herrno = 0;
			ret = gethostbyname_r(host, &hent, buf, bufsize, &hentp, &herrno);
			/* the man page isn't clear about where ERANGE is returned, so... */
			if (ret && (ret == ERANGE || errno == ERANGE || herrno == ERANGE))
			{
				free(buf);
				bufsize *= 2;
				continue;
			}
			break;
		}
		if (ret || !hentp)
			return -2;
		if (hent.h_addrtype != AF_INET || hent.h_length != 4 || !hent.h_addr_list[0])
			return -3;
		memcpy(&p.sin.sin_addr.s_addr, hent.h_addr_list[0], 4);
	}
	
	p.sin.sin_family = AF_INET;
	p.sin.sin_port = htons(port);

	p.fd = socket(AF_INET, SOCK_STREAM, 0);
	if (p.fd < 0)
		return -4;

	fcntl(p.fd, F_SETFL, O_NONBLOCK);

	ret = connect(p.fd, (struct sockaddr *) &p.sin, sizeof(p.sin));
	if (ret)
	{
		if (errno == EINPROGRESS)
		{
			if (!timeout)
			{
				memcpy(peer, &p, sizeof(*peer));
				return -6;
			}
			
			memset(&pfd, 0, sizeof(pfd));
			pfd.fd = p.fd;
			pfd.events = POLLOUT | POLLERR | POLLHUP;
			
			ret = poll(&pfd, 1, timeout);
			
			if (ret < 0)
				goto connecterr;
			if (ret == 0)
			{
				close(p.fd);
				return -6;
			}
			if (pfd.revents & (POLLERR | POLLHUP))
			{
				ret = sizeof(herrno);
				getsockopt(p.fd, SOL_SOCKET, SO_ERROR, &herrno, &ret);
				errno = herrno;
				goto connecterr;
			}
		}
		else
		{
connecterr:
			close(p.fd);
			return -5;
		}
	}
		
	memcpy(peer, &p, sizeof(*peer));
	return 0;
}

void
socket_close(struct peer *peer)
{
	if (peer->fd < 0)
		return;
	close(peer->fd);
	peer->fd = -1;
}

int
socket_printf(struct peer *peer, char *format, ...)
{
	va_list vl;
	int ret;
	
	va_start(vl, format);
	ret = socket_vprintf(peer, format, vl);
	va_end(vl);
	
	return ret;
}

int
socket_vprintf(struct peer *peer, char *format, va_list vl)
{
	char buf[1024];
	int ret, len;

	if (peer->fd < 0)
		return -1;
	
	vsnprintf(buf, sizeof(buf) - 1, format, vl);

	len = strlen(buf);
	ret = write(peer->fd, buf, len);
	if (ret != len)
	{
		socket_close(peer);
		return -1;
	}
	
	return 0;
}

void
socket_fill(int fd, struct peer *peer)
{
	int socklen;
	
	memset(peer, 0, sizeof(*peer));
	peer->fd = fd;
	socklen = sizeof(peer->sin);
	getsockname(fd, (struct sockaddr *) &peer->sin, &socklen);
}

int
socket_read(struct peer *peer, char *buf, int size, int timeout)
{
	int ret;
	struct pollfd pfd;

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = peer->fd;
	pfd.events = POLLIN | POLLERR | POLLHUP;
	
	ret = poll(&pfd, 1, timeout);
	if (ret < 0)
		return -1;
	if (ret == 0)
		return -2;

	ret = read(peer->fd, buf, size);
	return ret;
}

int
socket_write(struct peer *peer, char *buf, int size, int timeout)
{
	int ret;
	struct pollfd pfd;
	int written;
	
	written = 0;
	for (;;)
	{
		ret = write(peer->fd, buf, size);
		
		if (ret < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
writepoll:
				memset(&pfd, 0, sizeof(pfd));
				pfd.fd = peer->fd;
				pfd.events = POLLOUT | POLLERR | POLLHUP;
				
				ret = poll(&pfd, 1, timeout);
				
				if (ret < 0)
					return -1;
				if (ret == 0)
					return -2;
				if (!(pfd.events & POLLOUT))
					return -1;
				continue;
			}
			
			return -1;
		}
		else if (ret == 0)
			goto writepoll;
		else
		{
			written += ret;
			size -= ret;
			if (size <= 0)
				return written;
			buf += ret;
		}
	}
}

