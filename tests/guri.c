/*
 * Copyright (C) 2012 Antono Vasiljev <self@antono.info>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * licence, or (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA.
 */

#include <glib/glib.h>

static void
test_parse_html5 (void)
{
  GUri *guri;

  guri = g_uri_new ("http://antono.info:80/dict",
                    G_URI_PARSE_HTML5,
                    NULL);

  g_assert (g_str_equal (g_uri_get_host (guri), "antono.info"));
  g_assert (g_str_equal (g_uri_get_path (guri), "/dict"));
  g_assert (g_str_equal (g_uri_get_scheme (guri), "http"));
  g_assert (g_uri_get_port (guri) == 80);

}

static void
test_make_html5 (void)
{
  GUri *guri;

  guri = g_uri_new ("http://antono.info:80/dict",
                    G_URI_PARSE_HTML5,
                    NULL);

  // construction
  g_uri_set_scheme (guri, "gopher");
  g_uri_set_path (guri, "/about");
  g_uri_set_host (guri, "antono.info");
  g_uri_set_port (guri, 70);

  g_assert (g_str_equal (g_uri_get_host (guri), "antono.info"));
  g_assert (g_str_equal (g_uri_get_path (guri), "/about"));
  g_assert (g_str_equal (g_uri_get_scheme (guri), "gopher"));

  g_assert (g_str_equal (g_uri_to_string(guri, G_URI_HIDE_PASSWORD),
                         "gopher://antono.info:70"));

}




int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func("/guri/parse/html5", test_parse_html5);
  g_test_add_func("/guri/make/html5", test_make_html5);

  return g_test_run ();
}
