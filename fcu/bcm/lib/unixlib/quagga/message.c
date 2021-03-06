/* Copyright (C) 2002-2011 IP Infusion, Inc. All Rights Reserved. */

/* Generic message manager library.  This library includes both client
   and server side code.  */

//#include "pal.h"
#include <sys/un.h>
#include <zebra.h>
#include "thread.h"
#include "vector.h"
#include "network.h"
#include "memory.h"
#include "log.h"
#include "tlv.h"
/* The socket handle type. */
#include "message.h"
//#include "pal_memory.h"

/*
  This is used to indicate the socket handle is not valid.  If this is the
  value of a returned int, there was an error.  It must be a
  type which is compatible with the int definition below.
*/
#define PAL_SOCK_ERROR ((int)-1)
#define int int
#define pal_hton16 hton16
#define pal_hton32 hton32
#define pal_mem_cpy memcpy
#define pal_mem_set memset
#define pal_sock_accept(a, b, c, d) accept(b, c, d)
#define sockaddr_in4 sockaddr_in
#define pal_sockaddr_in4 sockaddr_in4
#define pal_sockaddr sockaddr
#define pal_sockaddr_un sockaddr_un
#define pal_sock_bind bind
#define pal_sock_close(x, y) close(y)
#define pal_sock_connect connect
#define int int
#define pal_sock(w,x,y,z) socket(x,y,z)
#define pal_sock_listen listen
#define pal_sock_select select
//#define pal_sock_set_nonblocking
//#define pal_sock_set_reuseaddr
//#define pal_sock_set_reuseport
#define pal_sock_set_t fd_set
#define pal_sock_write write
#define pal_strerror strerror
#define pal_strlen strlen
#define pal_strncpy strncpy
#define pal_timeval timeval
#define pal_unlink unlink
#define result_t int
#define s_int32_t int
#define RESULT_ERROR -1
#define RESULT_OK 0
#undef pal_ntoh32
#undef pal_hton32
#undef pal_ntoh16
#undef pal_hton16
#define pal_ntoh32 ntohl
#define pal_hton32 htonl
#define pal_ntoh16 ntohs
#define pal_hton16 htons

typedef enum
{
  PAL_FALSE = 0,                        /* Everybody calls zero false... */
  PAL_TRUE = (!PAL_FALSE)               /* Some want TRUE=1 or TRUE=-1 or TRUE!=0 */
}
bool_t;
/*
** Set reuse port option
*/
result_t
pal_sock_set_reuseport (int sock, s_int32_t state)
{
#ifdef SO_REUSEPORT
  return setsockopt (sock, SOL_SOCKET, SO_REUSEPORT, &state, sizeof (state));
#else /* def SO_REUSEPORT */
  errno = EINVAL;
  return PAL_SOCK_ERROR;
#endif /* def SO_REUSEPORT */
}

/*
** Set reuse address option
*/
result_t
pal_sock_set_reuseaddr (int sock, s_int32_t state)
{
  return setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &state, sizeof (state));
}

/*
** Set socket in non-blocking mode
*/
result_t
pal_sock_set_nonblocking (int sock, s_int32_t state)
{
#ifndef HAVE_IPNET
  int val;
  int ret;

  val = fcntl (sock, F_GETFL, 0);
  if (PAL_SOCK_ERROR != val)
    {
      ret = fcntl (sock, F_SETFL, (state ? val | O_NONBLOCK : val & (~O_NONBLOCK)));
      if (ret < 0)
        return RESULT_ERROR;
      return RESULT_OK;
    }
  else
    {
      return errno;
    }
#else
  int ret;
  int on;

  if (state == PAL_TRUE)
    on = 1;
  else
    on = 0;

  ret = ioctl (sock, IP_FIONBIO, (void *) &on);
  if (ret < 0)
    return RESULT_ERROR;

  return RESULT_OK;
#endif /* HAVE_IPNET */
}
int message_server_read (struct thread *);

/* Message client entry memory allocation.  */
struct message_entry *
message_entry_new ()
{
  struct message_entry *me;

  me = XCALLOC (MTYPE_MESSAGE_ENTRY, sizeof (struct message_entry));

  return me;
}

/* Message client entry memory free.  */
void
message_entry_free (struct message_entry *me)
{
  THREAD_OFF (me->t_read);

  if (me->sock >= 0)
    pal_sock_close (me->zg, me->sock);

  XFREE (MTYPE_MESSAGE_ENTRY, me);
}

/* Client is connected to server.  After accept socket, read thread is
   created to read client packet.  */
struct message_entry *
message_entry_register (struct message_handler *ms, int sock)
{
  struct message_entry *me;

  me = message_entry_new ();
  me->sock = sock;
  me->zg = ms->zg;
  me->ms = ms;
  me->t_read = thread_add_read (ms->zg, message_server_read, me, sock);

  return me;
}


/* Create message server.  */
struct message_handler *
message_server_create (void *zg)
{
  struct message_handler *ms;

  ms = XCALLOC (MTYPE_MESSAGE_HANDLER, sizeof (struct message_handler));
  ms->zg = zg;
  ms->clist = vector_init (1);
  ms->sock = -1; /* initialize to -1, s >= 0 is valid socket value */

  return ms;
}

int
message_server_delete (struct message_handler *ms)
{
  if (ms->path)
    XFREE (MTYPE_TMP, ms->path);

  vector_free (ms->clist);

  THREAD_OFF (ms->t_read);
  THREAD_OFF (ms->t_connect);

  XFREE (MTYPE_MESSAGE_HANDLER, ms);

  return 0;
}

#ifndef HAVE_TCP_MESSAGE
/* Set message server type to UNIX domain.  Argument path is a path
   for UNIX domain socket server.  */
void
message_server_set_style_domain (struct message_handler *ms, char *path)
{
  message_server_stop (ms);
  ms->style = MESSAGE_STYLE_UNIX_DOMAIN;
  if (ms->path)
    XFREE (MTYPE_TMP, ms->path);
  ms->path = XSTRDUP (MTYPE_TMP, path);
}
#endif /* HAVE_TCP_MESSAGE */

/* Set message server type to TCP.  */
void
message_server_set_style_tcp (struct message_handler *ms, u_int16_t port)
{
  message_server_stop (ms);
  ms->style = MESSAGE_STYLE_TCP;
  ms->port = port;
}

/* Set event callback function.  */
void
message_server_set_callback (struct message_handler *ms, int event, 
                             MESSAGE_CALLBACK cb)
{
  if (event >= MESSAGE_EVENT_MAX)
    return;

  ms->callback[event] = cb;
}

/* Message client disconnected.  */
void
message_server_disconnect (struct message_handler *ms,
                           struct message_entry *me, int sock)
{
  /* Cancel read thread.  */
  THREAD_OFF (me->t_read);

  /* Close socket.  */
  pal_sock_close (ms->zg, sock);
  me->sock = -1;

  /* Call callback function if registered.  */
  if (ms->callback[MESSAGE_EVENT_DISCONNECT])
    (*ms->callback[MESSAGE_EVENT_DISCONNECT]) (ms, me, sock);

  message_entry_free (me);
}

/* Message server read thread.  */
int
message_server_read (struct thread *t)
{
  struct message_entry *me;
  struct message_handler *ms;
  int sock;
  int nbytes = 0;

  sock = THREAD_FD (t);
  me = THREAD_ARG (t);
  me->t_read = NULL;
  ms = me->ms;

  /* Call read header callback function.  */
  if (ms->callback[MESSAGE_EVENT_READ_HEADER])
    {
      nbytes = (*ms->callback[MESSAGE_EVENT_READ_HEADER]) (ms, me, sock);

      /* Disconnection event handler.  */
      if (nbytes < 0)
        {
          message_server_disconnect (ms, me, sock);
          return -1;
        }
    }

  /* Call read message body callback function.  */
  if (ms->callback[MESSAGE_EVENT_READ_MESSAGE])
    {
      nbytes = (*ms->callback[MESSAGE_EVENT_READ_MESSAGE]) (ms, me, sock);

      /* Disconnection event handler.  */
      if (nbytes < 0)
        {
          message_server_disconnect (ms, me, sock);
          return -1;
        }
    }

  /* Re-register read thread.  */
  me->t_read = thread_add_read (ms->zg, message_server_read, me, sock);

  return 0;
}

/* Accept client connection.  */
int
message_server_accept (struct thread *t)
{
  struct message_handler *ms;
  struct message_entry *me;
  int sock;
  int csock;
#ifdef HAVE_TCP_MESSAGE
  struct pal_sockaddr_in4 addr;
#else /* HAVE_TCP_MESSAGE */
  struct pal_sockaddr_un addr;
#endif /* HAVE_TCP_MESSAGE */
  int len;
  int ret;

  ms = THREAD_ARG (t);
  ms->t_read = NULL;
  sock = THREAD_FD (t);
  len = sizeof (addr);

  /* Re-register accept thread.  */
  ms->t_read = thread_add_read (ms->zg, message_server_accept, ms, sock);

  /* Accept and get client socket.  */
  csock = pal_sock_accept (ms->zg, sock, (struct pal_sockaddr *) &addr, &len);
  if (csock < 0)
    {
      zlog_warn (ms->zg, "accept socket error: %s", pal_strerror (errno));
      return -1;
    }

  /* Make socket non-blocking.  */
  pal_sock_set_nonblocking (csock, PAL_TRUE);

  /* Register client to client list.  */
  me = message_entry_register (ms, csock);

  /* Call back function. */
  if (ms->callback[MESSAGE_EVENT_CONNECT])
    {
      ret = (*ms->callback[MESSAGE_EVENT_CONNECT]) (ms, me, csock);
      if (ret < 0)
        message_entry_free (me);
    }
  else
    message_entry_free (me);

  return 0;
}

#ifndef HAVE_TCP_MESSAGE
int
message_server_socket_unix (struct message_handler *mh)
{
  int sock;
  struct pal_sockaddr_un addr;
  int len;

  /* Create UNIX domain socket.  */
  sock = pal_sock (mh->zg, AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0)
    {
      zlog_warn (mh->zg, "socket create error: %s", pal_strerror (errno));
      return -1;
    }

  /* Unlink. */
  pal_unlink (mh->path);

  /* Prepare accept socket. */
  pal_mem_set (&addr, 0, sizeof (struct pal_sockaddr_un));
  addr.sun_family = AF_UNIX;
  pal_strncpy (addr.sun_path, mh->path, pal_strlen (mh->path));
#ifdef HAVE_SUN_LEN
  len = addr.sun_len = SUN_LEN (&addr);
#else
  len = sizeof (addr.sun_family) + pal_strlen (addr.sun_path);
#endif /* HAVE_SUN_LEN */

  /* Bind socket. */
  if (pal_sock_bind (sock, (struct pal_sockaddr *) &addr, len) < 0)
    {
      zlog_warn (mh->zg, "bind socket error: %s", pal_strerror (errno));
      pal_sock_close (mh->zg, sock);
      return -1;
    }

  return sock;
}
#endif /* HAVE_TCP_MESSAGE */

int
message_server_socket_tcp (struct message_handler *mh)
{
  int sock;
  struct pal_sockaddr_in4 addr;
  int len = sizeof (struct pal_sockaddr_in4);
  int state = 1; /* on. */
  int ret;

  /* Create TCP socket.  */
  sock = pal_sock (mh->zg, AF_INET, SOCK_STREAM, 0);
  if (sock < 0) 
    {
      zlog_warn (mh->zg, "socket create error: %s", pal_strerror (errno));
      return -1;
    }

  /* Prepare accept socket.  */
  pal_mem_set (&addr, 0, sizeof (struct pal_sockaddr_in4));
  addr.sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
  addr.sin_len = len;
#endif /* HAVE_SIN_LEN */
  addr.sin_port = pal_hton16 (mh->port);
  if (mh->addr)
    addr.sin_addr.s_addr = pal_hton32 (mh->addr);
  else
    addr.sin_addr.s_addr = pal_hton32 (INADDR_LOOPBACK);

  pal_sock_set_reuseaddr (sock, state);
  pal_sock_set_reuseport (sock, state);

  /* Bind socket. */
  if (pal_sock_bind (sock, (struct pal_sockaddr *) &addr, len) < 0)
    {
      zlog_warn (mh->zg, "bind socket error: %s", pal_strerror (errno));
      pal_sock_close (mh->zg, sock);
      return -1;
    }

  return sock;
}

/* Create server socket according to specified server type.  Then wait
   a connection from client.  */
int
message_server_start (struct message_handler *ms)
{
#ifdef HAVE_TCP_MESSAGE
  ms->sock = message_server_socket_tcp (ms);
#else /* HAVE_TCP_MESSAGE */
  if (ms->style == MESSAGE_STYLE_UNIX_DOMAIN)
    ms->sock = message_server_socket_unix (ms);
  else if (ms->style == MESSAGE_STYLE_TCP)
    ms->sock = message_server_socket_tcp (ms);
  else
    return -1;
#endif /* HAVE_TCP_MESSAGE */

  if (ms->sock == -1)
    return -1;

  /* Listen to the socket.  */
  if (pal_sock_listen (ms->sock, 5) < 0)
    {
      zlog_warn (ms->zg, "listen socket error: %s", pal_strerror (errno));
      pal_sock_close (ms->zg, ms->sock);
      ms->sock = -1;
      return -1;
    }

  /* Register read thread.  */
  ms->t_read = thread_add_read (ms->zg, message_server_accept, ms, ms->sock);

  return 0;
}

/* Stop message server.  */
int
message_server_stop (struct message_handler *ms)
{
  if (ms->sock >= 0)
    {
      pal_sock_close (ms->zg, ms->sock);
      ms->sock = -1;
    }
  return 0;
}

/* Create client with spcified type.  Type must be one of
   MESSAGE_TYPE_ASYNC or MESSAGE_TYPE_SYNC.  */
struct message_handler *
message_client_create (void *zg, int type)
{
  struct message_handler *mc;

  mc = XCALLOC (MTYPE_MESSAGE_HANDLER, sizeof (struct message_handler));
  mc->zg = zg;
  mc->type = type;
  mc->sock = -1;

  return mc;
}

/* Stop message client.  */
int
message_client_stop (struct message_handler *mc)
{
  if (mc->sock >= 0)
    {
      pal_sock_close (mc->zg, mc->sock);
      mc->sock = -1;
    }

  THREAD_OFF (mc->t_read);
  THREAD_OFF (mc->t_connect);

  return 0;
}

int
message_client_delete (struct message_handler *mc)
{
  /* Stop the client.  */
  message_client_stop (mc);

  if (mc->path)
    XFREE (MTYPE_TMP, mc->path);

  if (mc->clist)
    vector_only_wrapper_free (mc->clist);

  THREAD_OFF (mc->t_read);
  THREAD_OFF (mc->t_connect);

  XFREE (MTYPE_MESSAGE_HANDLER, mc);
   
  return 0;
}

#ifndef HAVE_TCP_MESSAGE
/* Set message client type to UNIX domain.  Argument path is a path
   for UNIX domain socket server.  */
void
message_client_set_style_domain (struct message_handler *mc, char *path)
{
  message_client_stop (mc);
  mc->style = MESSAGE_STYLE_UNIX_DOMAIN;
  if (mc->path)
    XFREE (MTYPE_TMP, mc->path);
  mc->path = XSTRDUP (MTYPE_TMP, path);
}
#endif /* !HAVE_TCP_MESSAGE */

/* Set message server type to TCP.  */
void
message_client_set_style_tcp (struct message_handler *mc, u_int16_t port)
{
  message_client_stop (mc);
  mc->style = MESSAGE_STYLE_TCP;
  mc->port = port;
}

/* Set socket nonblocking.  */
void
message_client_set_nonblocking (struct message_handler *mc)
{
  mc->nonblocking = PAL_TRUE;
}
/* Set event callback function.  */
void
message_client_set_callback (struct message_handler *ms, int event, 
                             MESSAGE_CALLBACK cb)
{
  if (event >= MESSAGE_EVENT_MAX)
    return;

  ms->callback[event] = cb;
}

/* Message client disconnected.  */
void
message_client_disconnect (struct message_handler *mc, int sock)
{
  /* Stop events.  */
  message_client_stop (mc);

  /* Call callback function if registered.  */
  if (mc->callback[MESSAGE_EVENT_DISCONNECT])
    (*mc->callback[MESSAGE_EVENT_DISCONNECT]) (mc, NULL, -1);
}

/* Message server read thread.  */
int
message_client_read (struct thread *t)
{
  struct message_handler *mc;
  int sock;
  int nbytes = 0;

  sock = THREAD_FD (t);
  mc = THREAD_ARG (t);
  mc->t_read = NULL;

  /* Call read message body callback function.  */
  if (mc->callback[MESSAGE_EVENT_READ_MESSAGE])
    {
      nbytes = (*mc->callback[MESSAGE_EVENT_READ_MESSAGE]) (mc, NULL, sock);

      /* Disconnection event handler.  */
      if (nbytes < 0)
        {
          message_client_disconnect (mc, sock);
          return -1;
        }
    }

  /* Re-register read thread.  */
  if (mc->type == MESSAGE_TYPE_ASYNC)
    mc->t_read = thread_add_read (mc->zg, message_client_read, mc, mc->sock);

  return 0;
}

/* Register read thread.  */
void
message_client_read_register (struct message_handler *mc)
{
  if (mc->t_read)
    thread_cancel (mc->t_read);

  mc->t_read = thread_add_read (mc->zg, message_client_read, mc, mc->sock);
}

/* Re-register read thread.  */
void
message_client_read_reregister (struct message_handler *mc)
{
  if (mc->t_read)
    {
      thread_cancel (mc->t_read);
      mc->t_read = thread_add_read (mc->zg, message_client_read, mc, mc->sock);
    }
}

int
message_client_socket_unix (struct message_handler *mh)
{
  int sock;
  struct pal_sockaddr_un addr;
  int len;

  sock = pal_sock (mh->zg, AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0)
    return -1;

  /* Prepare client UNIX domain connection. */
  pal_mem_set (&addr, 0, sizeof (struct pal_sockaddr_un));
  addr.sun_family = AF_UNIX;
  pal_strncpy (addr.sun_path, mh->path, pal_strlen (mh->path));
#ifdef HAVE_SUN_LEN
  len = addr.sun_len = SUN_LEN (&addr);
#else
  len = sizeof (addr.sun_family) + pal_strlen (addr.sun_path);
#endif /* HAVE_SUN_LEN */

  /* Connect to the server. */
  if (mh->nonblocking)
    {
      pal_sock_set_nonblocking (sock, PAL_TRUE);
    }
 
  if (pal_sock_connect (sock, (struct pal_sockaddr *) &addr, len) < 0)
    {
      pal_sock_close (mh->zg, sock);
      return -1;
    }

  return sock;
}

int
message_client_socket_tcp (struct message_handler *mh)
{
  int sock;
  struct pal_sockaddr_in4 addr;
  int len = sizeof (struct pal_sockaddr_in4);

  sock = pal_sock (mh->zg, AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return -1;

  /* Prepare client TCP connection. */
  pal_mem_set (&addr, 0, sizeof (struct pal_sockaddr_in4));
  addr.sin_family = AF_INET;
  addr.sin_port = pal_hton16 (mh->port);
  if (mh->addr)
    addr.sin_addr.s_addr = pal_hton32 (mh->addr);
  else
    addr.sin_addr.s_addr = pal_hton32 (INADDR_LOOPBACK);
#ifdef HAVE_SIN_LEN
  addr.sin_len = len;
#endif /* HAVE_SIN_LEN */

  /* Connect to the server. */
  if (mh->nonblocking)
    {
      pal_sock_set_nonblocking (sock, PAL_TRUE);
    }
  /* Connect to the server. */
  if (pal_sock_connect (sock, (struct pal_sockaddr *) &addr, len) < 0)
    {
      pal_sock_close (mh->zg, sock);
      return -1;
    }

  return sock;
}

int
message_client_start (struct message_handler *mc)
{
  /* If already connected to NSM. */
  if (mc->sock != -1)
    return 0;

  /* Check connect thread. */
  if (mc->t_connect)
    return 0;

  if (mc->style == MESSAGE_STYLE_UNIX_DOMAIN)
    mc->sock = message_client_socket_unix (mc);
  else if (mc->style == MESSAGE_STYLE_TCP)
    mc->sock = message_client_socket_tcp (mc);
  else
    return -1;

  if (mc->sock == -1)
    return -1;

  /* Connection established.  
     Read thread is launched by connect handler. */
  if (mc->callback[MESSAGE_EVENT_CONNECT])
    (*mc->callback[MESSAGE_EVENT_CONNECT]) (mc, NULL, mc->sock);

  return mc->sock;
}


void
message_queue_init (void *zg,
                    struct message_queue *mq)
{
  mq->zg = zg;
  FIFO_INIT (&mq->fifo);
}

struct message_queue_entry *
message_queue_top (struct message_queue *mq)
{
  return (struct message_queue_entry *)FIFO_TOP (&mq->fifo);
}

void
message_queue_clear (struct message_queue *mq)
{
  struct message_queue_entry *mqe;

  while ((mqe = (struct message_queue_entry *)FIFO_TOP (&mq->fifo)))
    {
      FIFO_DEL (mqe);
      XFREE (MTYPE_TMP, mqe->buf);
      XFREE (MTYPE_TMP, mqe);
    }
  THREAD_OFF (mq->t_write);
}

/* De-queue NSM message.  */
int
message_queue_entry_get (struct thread *t)
{
  struct message_queue *mq = THREAD_ARG (t);
  struct message_queue_entry *mqe;
  int sock = THREAD_FD (t);
  int nbytes;

  mq->t_write = NULL;

  mqe = (struct message_queue_entry *) FIFO_HEAD (&mq->fifo);
  if (mqe)
    {
      nbytes = pal_sock_write (sock, mqe->buf + mqe->written,
                               mqe->length - mqe->written);
      if (nbytes <= 0)
        {
          if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            {
              zlog_err (mq->zg, "NSM message send error socket %d %s",
                        sock, pal_strerror (errno));
              return -1;
            }
        }
      else if (nbytes != (mqe->length - mqe->written))
        mqe->written += nbytes;
      else
        {
          FIFO_DEL (mqe);
          XFREE (MTYPE_TMP, mqe->buf);
          XFREE (MTYPE_TMP, mqe);
        }
    }

  if (FIFO_TOP (&mq->fifo))
    THREAD_WRITE_ON (mq->zg, mq->t_write,
                     message_queue_entry_get, mq, sock);

  return 0;
}

void
message_queue_entry_set (struct message_queue *mq,
                         int sock, u_char *buf,
                         u_int32_t length, u_int32_t written)
{
  struct message_queue_entry *mqe;

  mqe = XCALLOC (MTYPE_TMP, sizeof (struct message_queue_entry));
  mqe->buf = XMALLOC (MTYPE_TMP, length);
  pal_mem_cpy (mqe->buf, buf, length);
  mqe->length = length;
  mqe->written = written;

  FIFO_ADD (&mq->fifo, mqe);

  THREAD_WRITE_ON (mq->zg, mq->t_write, message_queue_entry_get, mq, sock);
}

