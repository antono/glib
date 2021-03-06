/* GLib testing framework examples and tests
 *
 * Copyright (C) 2008-2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gio/gio.h>

#ifdef G_OS_UNIX
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <gio/gunixconnection.h>
#endif

#include "gnetworkingprivate.h"

typedef struct {
  GSocket *server;
  GSocket *client;
  GSocketFamily family;
  GThread *thread;
  GMainLoop *loop;
} IPTestData;

static gpointer
echo_server_thread (gpointer user_data)
{
  IPTestData *data = user_data;
  GSocket *sock;
  GError *error = NULL;
  gssize nread, nwrote;
  gchar buf[128];

  sock = g_socket_accept (data->server, NULL, &error);
  g_assert_no_error (error);

  while (TRUE)
    {
      nread = g_socket_receive (sock, buf, sizeof (buf), NULL, &error);
      g_assert_no_error (error);
      g_assert_cmpint (nread, >=, 0);

      if (nread == 0)
	break;

      nwrote = g_socket_send (sock, buf, nread, NULL, &error);
      g_assert_no_error (error);
      g_assert_cmpint (nwrote, ==, nread);
    }

  g_socket_close (sock, &error);
  g_assert_no_error (error);
  g_object_unref (sock);
  return NULL;
}

static IPTestData *
create_server (GSocketFamily family,
	       GThreadFunc   server_thread,
	       gboolean      v4mapped)
{
  IPTestData *data;
  GSocket *server;
  GError *error = NULL;
  GSocketAddress *addr;
  GInetAddress *iaddr;

  data = g_slice_new (IPTestData);
  data->family = family;

  data->server = server = g_socket_new (family,
					G_SOCKET_TYPE_STREAM,
					G_SOCKET_PROTOCOL_DEFAULT,
					&error);
  g_assert_no_error (error);

  g_assert_cmpint (g_socket_get_family (server), ==, family);
  g_assert_cmpint (g_socket_get_socket_type (server), ==, G_SOCKET_TYPE_STREAM);
  g_assert_cmpint (g_socket_get_protocol (server), ==, G_SOCKET_PROTOCOL_DEFAULT);

  g_socket_set_blocking (server, TRUE);

#if defined (IPPROTO_IPV6) && defined (IPV6_V6ONLY)
  if (v4mapped)
    {
      int fd, v6_only;

      fd = g_socket_get_fd (server);
      v6_only = 0;
      setsockopt (fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6_only, sizeof (v6_only));
    }
#endif

  if (v4mapped)
    iaddr = g_inet_address_new_any (family);
  else
    iaddr = g_inet_address_new_loopback (family);
  addr = g_inet_socket_address_new (iaddr, 0);
  g_object_unref (iaddr);

  g_assert_cmpint (g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (addr)), ==, 0);
  g_socket_bind (server, addr, TRUE, &error);
  g_assert_no_error (error);
  g_object_unref (addr);

  addr = g_socket_get_local_address (server, &error);
  g_assert_no_error (error);
  g_assert_cmpint (g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (addr)), !=, 0);
  g_object_unref (addr);

  g_socket_listen (server, &error);
  g_assert_no_error (error);

  data->thread = g_thread_new ("server", server_thread, data);

  return data;
}

static const gchar *testbuf = "0123456789abcdef";

static gboolean
test_ip_async_read_ready (GSocket      *client,
			  GIOCondition  cond,
			  gpointer      user_data)
{
  IPTestData *data = user_data;
  GError *error = NULL;
  gssize len;
  gchar buf[128];

  g_assert_cmpint (cond, ==, G_IO_IN);

  len = g_socket_receive (client, buf, sizeof (buf), NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpint (len, ==, strlen (testbuf) + 1);

  g_assert_cmpstr (testbuf, ==, buf);

  g_main_loop_quit (data->loop);

  return FALSE;
}

static gboolean
test_ip_async_write_ready (GSocket      *client,
			   GIOCondition  cond,
			   gpointer      user_data)
{
  IPTestData *data = user_data;
  GError *error = NULL;
  GSource *source;
  gssize len;

  g_assert_cmpint (cond, ==, G_IO_OUT);

  len = g_socket_send (client, testbuf, strlen (testbuf) + 1, NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpint (len, ==, strlen (testbuf) + 1);

  source = g_socket_create_source (client, G_IO_IN, NULL);
  g_source_set_callback (source, (GSourceFunc)test_ip_async_read_ready,
			 data, NULL);
  g_source_attach (source, NULL);
  g_source_unref (source);

  return FALSE;
}

static gboolean
test_ip_async_timed_out (GSocket      *client,
			 GIOCondition  cond,
			 gpointer      user_data)
{
  IPTestData *data = user_data;
  GError *error = NULL;
  GSource *source;
  gssize len;
  gchar buf[128];

  if (data->family == G_SOCKET_FAMILY_IPV4)
    {
      g_assert_cmpint (cond, ==, G_IO_IN);
      len = g_socket_receive (client, buf, sizeof (buf), NULL, &error);
      g_assert_cmpint (len, ==, -1);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
      g_clear_error (&error);
    }

  source = g_socket_create_source (client, G_IO_OUT, NULL);
  g_source_set_callback (source, (GSourceFunc)test_ip_async_write_ready,
			 data, NULL);
  g_source_attach (source, NULL);
  g_source_unref (source);

  return FALSE;
}

static gboolean
test_ip_async_connected (GSocket      *client,
			 GIOCondition  cond,
			 gpointer      user_data)
{
  IPTestData *data = user_data;
  GError *error = NULL;
  GSource *source;
  gssize len;
  gchar buf[128];

  g_socket_check_connect_result (client, &error);
  g_assert_no_error (error);
  /* We do this after the check_connect_result, since that will give a
   * more useful assertion in case of error.
   */
  g_assert_cmpint (cond, ==, G_IO_OUT);

  g_assert (g_socket_is_connected (client));

  /* This adds 1 second to "make check", so let's just only do it once. */
  if (data->family == G_SOCKET_FAMILY_IPV4)
    {
      len = g_socket_receive (client, buf, sizeof (buf), NULL, &error);
      g_assert_cmpint (len, ==, -1);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK);
      g_clear_error (&error);

      source = g_socket_create_source (client, G_IO_IN, NULL);
      g_source_set_callback (source, (GSourceFunc)test_ip_async_timed_out,
			     data, NULL);
      g_source_attach (source, NULL);
      g_source_unref (source);
    }
  else
    test_ip_async_timed_out (client, 0, data);

  return FALSE;
}

static gboolean
idle_test_ip_async_connected (gpointer user_data)
{
  IPTestData *data = user_data;

  return test_ip_async_connected (data->client, G_IO_OUT, data);
}

static void
test_ip_async (GSocketFamily family)
{
  IPTestData *data;
  GError *error = NULL;
  GSocket *client;
  GSocketAddress *addr;
  GSource *source;
  gssize len;
  gchar buf[128];

  data = create_server (family, echo_server_thread, FALSE);
  addr = g_socket_get_local_address (data->server, &error);

  client = g_socket_new (family,
			 G_SOCKET_TYPE_STREAM,
			 G_SOCKET_PROTOCOL_DEFAULT,
			 &error);
  g_assert_no_error (error);
  data->client = client;

  g_assert_cmpint (g_socket_get_family (client), ==, family);
  g_assert_cmpint (g_socket_get_socket_type (client), ==, G_SOCKET_TYPE_STREAM);
  g_assert_cmpint (g_socket_get_protocol (client), ==, G_SOCKET_PROTOCOL_DEFAULT);

  g_socket_set_blocking (client, FALSE);
  g_socket_set_timeout (client, 1);

  if (g_socket_connect (client, addr, NULL, &error))
    {
      g_assert_no_error (error);
      g_idle_add (idle_test_ip_async_connected, data);
    }
  else
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PENDING);
      g_clear_error (&error);
      source = g_socket_create_source (client, G_IO_OUT, NULL);
      g_source_set_callback (source, (GSourceFunc)test_ip_async_connected,
			     data, NULL);
      g_source_attach (source, NULL);
      g_source_unref (source);
    }
  g_object_unref (addr);

  data->loop = g_main_loop_new (NULL, TRUE);
  g_main_loop_run (data->loop);
  g_main_loop_unref (data->loop);

  g_socket_shutdown (client, FALSE, TRUE, &error);
  g_assert_no_error (error);

  g_thread_join (data->thread);

  len = g_socket_receive (client, buf, sizeof (buf), NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpint (len, ==, 0);

  g_socket_close (client, &error);
  g_assert_no_error (error);
  g_socket_close (data->server, &error);
  g_assert_no_error (error);

  g_object_unref (data->server);
  g_object_unref (client);

  g_slice_free (IPTestData, data);
}

static void
test_ipv4_async (void)
{
  test_ip_async (G_SOCKET_FAMILY_IPV4);
}

static void
test_ipv6_async (void)
{
  test_ip_async (G_SOCKET_FAMILY_IPV6);
}

static void
test_ip_sync (GSocketFamily family)
{
  IPTestData *data;
  GError *error = NULL;
  GSocket *client;
  GSocketAddress *addr;
  gssize len;
  gchar buf[128];

  data = create_server (family, echo_server_thread, FALSE);
  addr = g_socket_get_local_address (data->server, &error);

  client = g_socket_new (family,
			 G_SOCKET_TYPE_STREAM,
			 G_SOCKET_PROTOCOL_DEFAULT,
			 &error);
  g_assert_no_error (error);

  g_assert_cmpint (g_socket_get_family (client), ==, family);
  g_assert_cmpint (g_socket_get_socket_type (client), ==, G_SOCKET_TYPE_STREAM);
  g_assert_cmpint (g_socket_get_protocol (client), ==, G_SOCKET_PROTOCOL_DEFAULT);

  g_socket_set_blocking (client, TRUE);
  g_socket_set_timeout (client, 1);

  g_socket_connect (client, addr, NULL, &error);
  g_assert_no_error (error);
  g_assert (g_socket_is_connected (client));
  g_object_unref (addr);

  /* This adds 1 second to "make check", so let's just only do it once. */
  if (family == G_SOCKET_FAMILY_IPV4)
    {
      len = g_socket_receive (client, buf, sizeof (buf), NULL, &error);
      g_assert_cmpint (len, ==, -1);
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
      g_clear_error (&error);
    }

  len = g_socket_send (client, testbuf, strlen (testbuf) + 1, NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpint (len, ==, strlen (testbuf) + 1);
  
  len = g_socket_receive (client, buf, sizeof (buf), NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpint (len, ==, strlen (testbuf) + 1);

  g_assert_cmpstr (testbuf, ==, buf);

  g_socket_shutdown (client, FALSE, TRUE, &error);
  g_assert_no_error (error);

  g_thread_join (data->thread);

  len = g_socket_receive (client, buf, sizeof (buf), NULL, &error);
  g_assert_no_error (error);
  g_assert_cmpint (len, ==, 0);

  g_socket_close (client, &error);
  g_assert_no_error (error);
  g_socket_close (data->server, &error);
  g_assert_no_error (error);

  g_object_unref (data->server);
  g_object_unref (client);

  g_slice_free (IPTestData, data);
}

static void
test_ipv4_sync (void)
{
  test_ip_sync (G_SOCKET_FAMILY_IPV4);
}

static void
test_ipv6_sync (void)
{
  test_ip_sync (G_SOCKET_FAMILY_IPV6);
}

#if defined (IPPROTO_IPV6) && defined (IPV6_V6ONLY)
static gpointer
v4mapped_server_thread (gpointer user_data)
{
  IPTestData *data = user_data;
  GSocket *sock;
  GError *error = NULL;
  GSocketAddress *addr;

  sock = g_socket_accept (data->server, NULL, &error);
  g_assert_no_error (error);

  g_assert_cmpint (g_socket_get_family (sock), ==, G_SOCKET_FAMILY_IPV6);

  addr = g_socket_get_local_address (sock, &error);
  g_assert_no_error (error);
  g_assert_cmpint (g_socket_address_get_family (addr), ==, G_SOCKET_FAMILY_IPV4);
  g_object_unref (addr);

  addr = g_socket_get_remote_address (sock, &error);
  g_assert_no_error (error);
  g_assert_cmpint (g_socket_address_get_family (addr), ==, G_SOCKET_FAMILY_IPV4);
  g_object_unref (addr);

  g_socket_close (sock, &error);
  g_assert_no_error (error);
  g_object_unref (sock);
  return NULL;
}

static void
test_ipv6_v4mapped (void)
{
  IPTestData *data;
  GError *error = NULL;
  GSocket *client;
  GSocketAddress *addr, *v4addr;
  GInetAddress *iaddr;

  data = create_server (G_SOCKET_FAMILY_IPV6, v4mapped_server_thread, TRUE);

  client = g_socket_new (G_SOCKET_FAMILY_IPV4,
			 G_SOCKET_TYPE_STREAM,
			 G_SOCKET_PROTOCOL_DEFAULT,
			 &error);
  g_assert_no_error (error);

  g_socket_set_blocking (client, TRUE);
  g_socket_set_timeout (client, 1);

  addr = g_socket_get_local_address (data->server, &error);
  iaddr = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
  v4addr = g_inet_socket_address_new (iaddr, g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (addr)));
  g_object_unref (iaddr);
  g_object_unref (addr);

  g_socket_connect (client, v4addr, NULL, &error);
  g_assert_no_error (error);
  g_assert (g_socket_is_connected (client));

  g_thread_join (data->thread);

  g_socket_close (client, &error);
  g_assert_no_error (error);
  g_socket_close (data->server, &error);
  g_assert_no_error (error);

  g_object_unref (data->server);
  g_object_unref (client);
  g_object_unref (v4addr);

  g_slice_free (IPTestData, data);
}
#endif

#ifdef G_OS_UNIX
static void
test_unix_from_fd (void)
{
  gint fd;
  GError *error;
  GSocket *s;

  fd = socket (AF_UNIX, SOCK_STREAM, 0);
  g_assert_cmpint (fd, !=, -1);

  error = NULL;
  s = g_socket_new_from_fd (fd, &error);
  g_assert_no_error (error);
  g_assert_cmpint (g_socket_get_family (s), ==, G_SOCKET_FAMILY_UNIX);
  g_assert_cmpint (g_socket_get_socket_type (s), ==, G_SOCKET_TYPE_STREAM);
  g_assert_cmpint (g_socket_get_protocol (s), ==, G_SOCKET_PROTOCOL_DEFAULT);
  g_object_unref (s);
}

static void
test_unix_connection (void)
{
  gint fd;
  GError *error;
  GSocket *s;
  GSocketConnection *c;

  fd = socket (AF_UNIX, SOCK_STREAM, 0);
  g_assert_cmpint (fd, !=, -1);

  error = NULL;
  s = g_socket_new_from_fd (fd, &error);
  g_assert_no_error (error);
  c = g_socket_connection_factory_create_connection (s);
  g_assert (G_IS_UNIX_CONNECTION (c));
  g_object_unref (c);
  g_object_unref (s);
}

static GSocketConnection *
create_connection_for_fd (int fd)
{
  GError *err = NULL;
  GSocket *socket;
  GSocketConnection *connection;

  socket = g_socket_new_from_fd (fd, &err);
  g_assert_no_error (err);
  g_assert (G_IS_SOCKET (socket));
  connection = g_socket_connection_factory_create_connection (socket);
  g_assert (G_IS_UNIX_CONNECTION (connection));
  g_object_unref (socket);
  return connection;
}

#define TEST_DATA "failure to say failure to say 'i love gnome-panel!'."

static void
test_unix_connection_ancillary_data (void)
{
  GError *err = NULL;
  gint pv[2], sv[3];
  gint status, fd, len;
  char buffer[1024];
  pid_t pid;

  status = pipe (pv);
  g_assert_cmpint (status, ==, 0);

  status = socketpair (PF_UNIX, SOCK_STREAM, 0, sv);
  g_assert_cmpint (status, ==, 0);

  pid = fork ();
  g_assert_cmpint (pid, >=, 0);

  /* Child: close its copy of the write end of the pipe, receive it
   * again from the parent over the socket, and write some text to it.
   *
   * Parent: send the write end of the pipe (still open for the
   * parent) over the socket, close it, and read some text from the
   * read end of the pipe.
   */
  if (pid == 0)
    {
      GSocketConnection *connection;

      close (sv[1]);
      connection = create_connection_for_fd (sv[0]);

      status = close (pv[1]);
      g_assert_cmpint (status, ==, 0);

      err = NULL;
      fd = g_unix_connection_receive_fd (G_UNIX_CONNECTION (connection), NULL,
					 &err);
      g_assert_no_error (err);
      g_assert_cmpint (fd, >, -1);
      g_object_unref (connection);

      do
	len = write (fd, TEST_DATA, sizeof (TEST_DATA));
      while (len == -1 && errno == EINTR);
      g_assert_cmpint (len, ==, sizeof (TEST_DATA));
      exit (0);
    }
  else
    {
      GSocketConnection *connection;

      close (sv[0]);
      connection = create_connection_for_fd (sv[1]);

      err = NULL;
      g_unix_connection_send_fd (G_UNIX_CONNECTION (connection), pv[1], NULL,
				 &err);
      g_assert_no_error (err);
      g_object_unref (connection);

      status = close (pv[1]);
      g_assert_cmpint (status, ==, 0);

      memset (buffer, 0xff, sizeof buffer);
      do
	len = read (pv[0], buffer, sizeof buffer);
      while (len == -1 && errno == EINTR);

      g_assert_cmpint (len, ==, sizeof (TEST_DATA));
      g_assert_cmpstr (buffer, ==, TEST_DATA);

      waitpid (pid, &status, 0);
      g_assert (WIFEXITED (status));
      g_assert_cmpint (WEXITSTATUS (status), ==, 0);
    }

  /* TODO: add test for g_unix_connection_send_credentials() and
   * g_unix_connection_receive_credentials().
   */
}
#endif /* G_OS_UNIX */

int
main (int   argc,
      char *argv[])
{
  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/socket/ipv4_sync", test_ipv4_sync);
  g_test_add_func ("/socket/ipv4_async", test_ipv4_async);
  g_test_add_func ("/socket/ipv6_sync", test_ipv6_sync);
  g_test_add_func ("/socket/ipv6_async", test_ipv6_async);
#if defined (IPPROTO_IPV6) && defined (IPV6_V6ONLY)
  g_test_add_func ("/socket/ipv6_v4mapped", test_ipv6_v4mapped);
#endif
#ifdef G_OS_UNIX
  g_test_add_func ("/socket/unix-from-fd", test_unix_from_fd);
  g_test_add_func ("/socket/unix-connection", test_unix_connection);
  g_test_add_func ("/socket/unix-connection-ancillary-data", test_unix_connection_ancillary_data);
#endif

  return g_test_run();
}

