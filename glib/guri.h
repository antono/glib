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
  G_URI_PARSE_STRICT    = 1 << 0,
  G_URI_PARSE_HTML5     = 1 << 1,
  G_URI_NO_IRI          = 1 << 2,
  G_URI_HAS_PASSWORD    = 1 << 3,
  G_URI_HAS_AUTH_PARAMS = 1 << 4,
  G_URI_HOST_IS_DNS     = 1 << 5
} GUriFlags;

GUri *       g_uri_new                  (const gchar        *uri_string,
					 GUriFlags           flags,
					 GError            **error);
GUri *       g_uri_new_relative         (GUri               *base_uri,
					 const gchar        *uri_string,
					 GUriFlags           flags,
					 GError            **error);
gboolean     g_uri_reparse              (GUri               *uri,
					 GUriFlags           flags,
					 GError            **error);

typedef enum {
  G_URI_HIDE_PASSWORD    = 1 << 0,
  G_URI_HIDE_AUTH_PARAMS = 1 << 1
} GUriToStringFlags;

char *       g_uri_to_string            (GUri               *uri,
					 GUriToStringFlags   flags);

GUri *       g_uri_copy                 (GUri               *uri);
void         g_uri_free                 (GUri               *uri);

const gchar *g_uri_get_scheme           (GUri               *uri);
void         g_uri_set_scheme           (GUri               *uri,
					 const gchar        *scheme);

const gchar *g_uri_get_encoded_userinfo (GUri               *uri);
void         g_uri_set_encoded_userinfo (GUri               *uri,
					 const gchar        *userinfo);

const gchar *g_uri_get_user             (GUri               *uri);
void         g_uri_set_user             (GUri               *uri,
					 const gchar        *user);

const gchar *g_uri_get_password         (GUri               *uri);
void         g_uri_set_password         (GUri               *uri,
					 const gchar        *password);

const gchar *g_uri_get_auth_params      (GUri               *uri);
void         g_uri_set_auth_params      (GUri               *uri,
					 const gchar        *auth_params);

const gchar *g_uri_get_host             (GUri               *uri);
void         g_uri_set_host             (GUri               *uri,
					 const gchar        *host);

gushort      g_uri_get_port             (GUri               *uri);
void         g_uri_set_port             (GUri               *uri,
					 gushort             port);

const gchar *g_uri_get_encoded_path     (GUri               *uri);
void         g_uri_set_encoded_path     (GUri               *uri,
					 const gchar        *path);

const gchar *g_uri_get_encoded_query    (GUri               *uri);
void         g_uri_set_encoded_query    (GUri               *uri,
					 const gchar        *query);

const gchar *g_uri_get_encoded_fragment (GUri               *uri);
void         g_uri_set_encoded_fragment (GUri               *uri,
					 const gchar        *fragment);


void         g_uri_split                (const gchar        *uri_string,
					 gboolean            strict,
					 gchar             **scheme,
					 gchar             **userinfo,
					 gchar             **host,
					 gchar             **port,
					 gchar             **path,
					 gchar             **query,
					 gchar             **fragment);
GHashTable * g_uri_parse_params         (const gchar        *params,
					 gssize              length,
					 gchar               separator,
					 gboolean            case_insensitive);
gboolean     g_uri_parse_host           (const gchar        *uri_string,
					 GUriFlags           flags,
					 gchar             **scheme,
					 gchar             **host,
					 gushort            *port,
					 GError            **error);

G_END_DECLS

#endif /* __G_URI_H__ */
