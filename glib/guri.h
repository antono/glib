/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1999-2008 Novell, Inc.
 * Copyright (C) 2008-2010 Red Hat, Inc.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#if !defined (__GLIB_H_INSIDE__) && !defined (GLIB_COMPILATION)
#error "Only <glib.h> can be included directly."
#endif

#ifndef __G_URI_H__
#define __G_URI_H__

#include <glib/gtypes.h>

G_BEGIN_DECLS

typedef struct _GUri GUri;

typedef enum {
  G_URI_PARSE_STRICT,
  G_URI_PARSE_HTML5,
  G_URI_PARSE_ALLOW_SINGLE_PERCENT,
  G_URI_PARSE_NO_IRI
} GUriParseFlags;

GUri *      g_uri_new          (const gchar        *uri_string,
				GUriParseFlags      flags,
				GError            **error);
GUri *      g_uri_new_relative (GUri               *base_uri,
				const gchar        *uri_string,
				GUriParseFlags      flags,
				GError            **error);

char *      g_uri_to_string    (GUri               *uri,
				GUriToStringFlags   flags)

GUri *      g_uri_copy         (GUri               *uri);
void        g_uri_free         (GUri               *uri);

const char *g_uri_get_scheme   (GUri               *uri);
void        g_uri_set_scheme   (GUri               *uri,
				const char         *scheme);

typedef enum {
  G_URI_USERINFO_HAS_PASSWORD = 1 << 0,
  G_URI_USERINFO_HAS_PARAMS   = 1 << 1
} GUriUserinfoFlags;

void        g_uri_get_userinfo (GUri               *uri,
				GUriUserinfoFlags   flags,
				gchar             **username,
				gchar             **password,
				gchar             **params);
void        g_uri_set_userinfo (GUri               *uri,
				GUriUserinfoFlags   flags,
				const gchar        *username,
				const gchar        *password,
				const gchar        *params);

const char *g_uri_get_host     (GUri               *uri);
void        g_uri_set_host     (GUri               *uri,
				const char         *host);

gushort     g_uri_get_port     (GUri               *uri);
void        g_uri_set_port     (GUri               *uri,
				gushort             port);

const char *g_uri_get_path     (GUri               *uri);
void        g_uri_set_path     (GUri               *uri,
				const char         *path);

const char *g_uri_get_query    (GUri               *uri);
void        g_uri_set_query    (GUri               *uri,
				const char         *query);

const char *g_uri_get_fragment (GUri               *uri);
void        g_uri_set_fragment (GUri               *uri,
				const char         *fragment);

G_END_DECLS

#endif /* __G_URI_H__ */
