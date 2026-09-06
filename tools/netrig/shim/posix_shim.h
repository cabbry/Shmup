/*
 *  posix_shim.h -- just enough BSD sockets for net_rig to compile the REAL
 *  engine/src/netchannel.c on Windows, with the sockets replaced by an
 *  in-process fake network (see net_bus.h). Nothing here is a real syscall.
 */
#ifndef NETRIG_POSIX_SHIM_H
#define NETRIG_POSIX_SHIM_H

#include <stdint.h>
#include <string.h>
#include <errno.h>

#ifndef EAGAIN
#define EAGAIN 11
#endif

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;
typedef unsigned char  u_char_shim;

#define AF_INET      2
#define SOCK_DGRAM   2
#define IPPROTO_UDP  17
#define SOL_SOCKET   0xffff
#define SO_REUSEADDR 4
#define F_SETFL      4
#define F_GETFL      3
#define O_NONBLOCK   0x4000
#define INADDR_ANY   0u
#define IF_NAMESIZE  16

struct in_addr { unsigned int s_addr; };

struct sockaddr {
	unsigned char  sa_len;
	sa_family_t    sa_family;
	char           sa_data[14];
};

struct sockaddr_in {
	unsigned char  sin_len;			/* BSD-ism the 2010 code relies on */
	sa_family_t    sin_family;
	unsigned short sin_port;
	struct in_addr sin_addr;
	char           sin_zero[8];
};

#define IFF_UP      0x1
#define IFF_RUNNING 0x40

struct if_nameindex {
	unsigned int if_index;
	char*        if_name;
};

struct if_nameindex* rig_if_nameindex(void);
void rig_if_freenameindex(struct if_nameindex* p);
#define if_nameindex()      rig_if_nameindex()
#define if_freenameindex(p) rig_if_freenameindex((p))

struct ifaddrs {
	struct ifaddrs*  ifa_next;
	char*            ifa_name;
	unsigned int     ifa_flags;
	struct sockaddr* ifa_addr;
	struct sockaddr* ifa_netmask;
	struct sockaddr* ifa_dstaddr;
	void*            ifa_data;
};

/* select() surface -- the rig never blocks, so this is a stub. */
typedef struct { int fd_count; int fd_array[16]; } fd_set;
struct timeval_shim { long tv_sec; long tv_usec; };
#define timeval timeval_shim
#define FD_ZERO(s)    ((s)->fd_count = 0)
#define FD_SET(f, s)  ((s)->fd_array[((s)->fd_count < 16) ? (s)->fd_count++ : 0] = (f))
#define FD_ISSET(f,s) (0)

/* Byte order: the rig runs little-endian, same as the devices. */
static unsigned short rig_htons(unsigned short v) { return (unsigned short)((v << 8) | (v >> 8)); }
static unsigned int   rig_htonl(unsigned int v)
{
	return ((v & 0xffu) << 24) | ((v & 0xff00u) << 8) | ((v & 0xff0000u) >> 8) | (v >> 24);
}
#define htons(v) rig_htons((unsigned short)(v))
#define ntohs(v) rig_htons((unsigned short)(v))
#define htonl(v) rig_htonl((unsigned int)(v))
#define ntohl(v) rig_htonl((unsigned int)(v))

#ifndef bzero
#define bzero(p, n) memset((p), 0, (n))
#endif

/* Implemented by the rig (net_bus.c): a fake UDP network in memory. */
int  rig_socket(int domain, int type, int protocol);
int  rig_bind(int s, const struct sockaddr* addr, socklen_t len);
int  rig_close(int s);
int  rig_fcntl(int s, int cmd, int arg);
int  rig_setsockopt(int s, int level, int optname, const void* val, socklen_t len);
int  rig_sendto(int s, const void* buf, int len, int flags, const struct sockaddr* to, socklen_t tolen);
int  rig_recvfrom(int s, void* buf, int len, int flags, struct sockaddr* from, socklen_t* fromlen);
int  rig_select(int nfds, fd_set* r, fd_set* w, fd_set* e, struct timeval* tv);
int  rig_getifaddrs(struct ifaddrs** ifap);
void rig_freeifaddrs(struct ifaddrs* ifa);
char* rig_inet_ntoa(struct in_addr in);
char* rig_if_indextoname(unsigned int idx, char* name);

#define socket(a,b,c)            rig_socket((a),(b),(c))
#define bind(a,b,c)              rig_bind((a),(b),(c))
#define close(a)                 rig_close((a))
#define fcntl(a,b,c)             rig_fcntl((a),(b),(c))
#define setsockopt(a,b,c,d,e)    rig_setsockopt((a),(b),(c),(d),(e))
#define sendto(a,b,c,d,e,f)      rig_sendto((a),(b),(c),(d),(e),(f))
#define recvfrom(a,b,c,d,e,f)    rig_recvfrom((a),(b),(c),(d),(e),(f))
#define select(a,b,c,d,e)        rig_select((a),(b),(c),(d),(e))
#define getifaddrs(a)            rig_getifaddrs((a))
#define freeifaddrs(a)           rig_freeifaddrs((a))
#define inet_ntoa(a)             rig_inet_ntoa((a))
#define if_indextoname(a,b)      rig_if_indextoname((a),(b))

#endif
