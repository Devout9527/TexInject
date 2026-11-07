#ifndef MS_WINDOWS
#ifdef __VMS
#   include <socket.h>
# else
#   include <sys/socket.h>
# endif
# include <netinet/in.h>
# if !(defined(__BEOS__) || defined(__CYGWIN__) || (defined(PYOS_OS2) && defined(PYCC_VACPP)))
#  include <netinet/tcp.h>
# endif

#else
# include <winsock2.h>
# include <ws2tcpip.h>

# ifdef SIO_GET_MULTICAST_FILTER
#  include <MSTcpIP.h>
#  define HAVE_ADDRINFO
#  define HAVE_SOCKADDR_STORAGE
#  define HAVE_GETADDRINFO
#  define HAVE_GETNAMEINFO
#  define ENABLE_IPV6
# else
typedef int socklen_t;
# endif
#endif

#ifdef HAVE_SYS_UN_H
# include <sys/un.h>
#else
# undef AF_UNIX
#endif

#ifdef HAVE_LINUX_NETLINK_H
# ifdef HAVE_ASM_TYPES_H
#  include <asm/types.h>
# endif
# include <linux/netlink.h>
#else
#  undef AF_NETLINK
#endif

#ifdef HAVE_BLUETOOTH_BLUETOOTH_H
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/sco.h>
#include <bluetooth/hci.h>
#endif

#ifdef HAVE_BLUETOOTH_H
#include <bluetooth.h>
#endif

#ifdef HAVE_NETPACKET_PACKET_H
# include <sys/ioctl.h>
# include <net/if.h>
# include <netpacket/packet.h>
#endif

#ifdef HAVE_LINUX_TIPC_H
# include <linux/tipc.h>
#endif

#ifndef Py__SOCKET_H
#define Py__SOCKET_H
#ifdef __cplusplus
extern "C" {
#endif

#define PySocket_MODULE_NAME    "_socket"
#define PySocket_CAPI_NAME      "CAPI"
#define PySocket_CAPSULE_NAME  (PySocket_MODULE_NAME "." PySocket_CAPI_NAME)

#ifdef MS_WINDOWS
typedef SOCKET SOCKET_T;
#       ifdef MS_WIN64
#               define SIZEOF_SOCKET_T 8
#       else
#               define SIZEOF_SOCKET_T 4
#       endif
#else
typedef int SOCKET_T;
#       define SIZEOF_SOCKET_T SIZEOF_INT
#endif

typedef union sock_addr {
    struct sockaddr_in in;
#ifdef AF_UNIX
    struct sockaddr_un un;
#endif
#ifdef AF_NETLINK
    struct sockaddr_nl nl;
#endif
#ifdef ENABLE_IPV6
    struct sockaddr_in6 in6;
    struct sockaddr_storage storage;
#endif
#ifdef HAVE_BLUETOOTH_BLUETOOTH_H
    struct sockaddr_l2 bt_l2;
    struct sockaddr_rc bt_rc;
    struct sockaddr_sco bt_sco;
    struct sockaddr_hci bt_hci;
#endif
#ifdef HAVE_NETPACKET_PACKET_H
    struct sockaddr_ll ll;
#endif
} sock_addr_t;

typedef struct {
    PyObject_HEAD
    SOCKET_T sock_fd;
    int sock_family;
    int sock_type;
    int sock_proto;
    PyObject *(*errorhandler)(void);

    double sock_timeout;

} PySocketSockObject;

typedef struct {
    PyTypeObject *Sock_Type;
    PyObject *error;
} PySocketModule_APIObject;

#ifndef PySocket_BUILDING_SOCKET

static
PySocketModule_APIObject PySocketModule;

#ifndef DPRINTF
# define DPRINTF if (0) printf
#endif

static
int PySocketModule_ImportModuleAndAPI(void)
{
    void *api;

  DPRINTF(" Loading capsule %s\n", PySocket_CAPSULE_NAME);
  api = PyCapsule_Import(PySocket_CAPSULE_NAME, 1);
    if (api == NULL)
        goto onError;
    memcpy(&PySocketModule, api, sizeof(PySocketModule));
    DPRINTF(" API object loaded and initialized.\n");
    return 0;

 onError:
    DPRINTF(" not found.\n");
    return -1;
}

#endif

#ifdef __cplusplus
}
#endif
#endif
