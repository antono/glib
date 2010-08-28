/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* GLIB - Library of useful routines for C programming
 * Copyright (C) 2010 Red Hat, Inc.
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

#include "config.h"

#include "glib.h"
#include "glibintl.h"

#include <string.h>

#include "galias.h"

/**
 * SECTION:guri
 * @short_description: URI-handling utilities
 * @include: glib.h
 *
 * FIXME
 */

struct _GUri {
  GUriFlags flags;

  gchar   *scheme;
  gchar   *encoded_userinfo;
  gchar   *encoded_host;
  gushort  port;
  gchar   *encoded_path;
  gchar   *encoded_query;
  gchar   *encoded_fragment;

  /* Derived from @encoded_userinfo according to @flags */
  gchar   *user;
  gchar   *password;
  gchar   *auth_params;

  /* Derived from @host according to @flags */
  gchar   *host;
};

#define XDIGIT(c) ((c) <= '9' ? (c) - '0' : ((c) & 0x4F) - 'A' + 10)
#define HEXCHAR(s) ((XDIGIT (s[1]) << 4) + XDIGIT (s[2]))

static gchar *
uri_decoder (const gchar  *part,
             gboolean      just_normalize,
             GUriFlags     flags,
             GError      **error)
{
  gchar *decoded;
  guchar *s, *d;
  guchar c;

  decoded = g_strdup (part);
  s = d = (guchar *)decoded;

  do
    {
      if (*s == '%')
        {
          if (!g_ascii_isxdigit (s[1]) || !g_ascii_isxdigit (s[2]))
            {
              if (flags & G_URI_PARSE_STRICT)
                {
                  g_set_error_literal (error, G_URI_ERROR, G_URI_ERROR_PARSE,
                                       _("Invalid %-encoding in URI"));
                  return FALSE;
                }
              *d++ = *s;
              continue;
            }
          c = HEXCHAR (s);
          if (just_normalize && !g_uri_char_is_unreserved (c))
            *d++ = *s;
          else
            {
              *d++ = HEXCHAR (s);
              s += 2;
            }
        }
      else
        *d++ = *s;
    }
  while (*s++);

  return decoded;
}

static gchar *
uri_decode (const gchar *part, GUriFlags flags)
{
  return uri_decoder (part, FALSE, flags);
}

static gchar *
uri_normalize (const gchar *part, GUriFlags flags)
{
  return uri_decoder (part, TRUE, flags);
}

/* Does the "Remove Dot Segments" algorithm from section 5.2.4 of RFC
 * 3986. @path is assumed to start with '/', and is modified in place.
 */
static void
remove_dot_segments (gchar *path)
{
  gchar *p, *q;

  /* Remove "./" where "." is a complete segment. */
  for (p = path + 1; *p; )
    {
      if (*(p - 1) == '/' &&
	  *p == '.' && *(p + 1) == '/')
	memmove (p, p + 2, strlen (p + 2) + 1);
      else
	p++;
    }
  /* Remove "." at end. */
  if (p > path + 2 &&
      *(p - 1) == '.' && *(p - 2) == '/')
    *(p - 1) = '\0';

  /* Remove "<segment>/../" where <segment> != ".." */
  for (p = path + 1; *p; )
    {
      if (!strncmp (p, "../", 3))
	{
	  p += 3;
	  continue;
	}
      q = strchr (p + 1, '/');
      if (!q)
	break;
      if (strncmp (q, "/../", 4) != 0)
	{
	  p = q + 1;
	  continue;
	}
      memmove (p, q + 4, strlen (q + 4) + 1);
      p = path + 1;
    }
  /* Remove "<segment>/.." at end where <segment> != ".." */
  q = strrchr (path, '/');
  if (q && !strcmp (q, "/.."))
    {
      p = q - 1;
      while (p > path && *p != '/')
	p--;
      if (strncmp (p, "/../", 4) != 0)
	*(p + 1) = 0;
    }

  /* Remove extraneous initial "/.."s */
  while (!strncmp (path, "/../", 4))
    memmove (path, path + 3, strlen (path) - 2);
  if (!strcmp (path, "/.."))
    path[1] = '\0';
}

static char *
uri_cleanup (const char *uri_string)
{
  GString *copy;
  const char *end;
  int len;

  /* Skip leading whitespace */
  while (g_ascii_isspace (*uri_string))
    uri_string++;

  /* Ignore trailing whitespace */
  end = uri_string + strlen (uri_string);
  while (end > uri_string && g_ascii_isspace (uri_string[end - 1]))
    end--;

  /* Copy the rest, encoding unencoded spaces and stripping other whitespace */
  copy = g_string_sized_new (end - uri_string);
  while (uri_string < end)
    {
      if (*uri_string == ' ')
	g_string_append (copy, "%20");
      else if (g_ascii_isspace (*uri_string))
	;
      else
	g_string_append_c (copy, *uri_string);
      uri_string++;
    }

  return g_string_free (copy, FALSE);
}

gchar *
parse_host (const gchar  *raw_host,
            GUriFlags     flags,
            GError      **error)
{
  gchar *decoded, *addr, *ace;

  if (*raw_host == '[')
    {
      int len = strlen (raw_host);

      if (raw_host[len - 1] != ']')
	{
	  g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
		       _("Invalid IP literal '%s' in URI"),
		       raw_host);
	  return NULL;
	}

      addr = g_strndup (raw_host + 1, len - 2);
      /* addr must be an IPv6 address */
      if (!g_hostname_is_ip_address (addr) || !strchr (addr, ':'))
	{
	  g_free (addr);
	  g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
		       _("Invalid IP literal '%s' in URI"),
		       raw_host);
	  return NULL;
	}

      return addr;
    }

  if (g_hostname_is_ip_address (raw_host))
    return g_strdup (raw_host);

  decoded = uri_decode (raw_host, G_URI_PARSE_STRICT, error);
  if (!decoded)
    return NULL;

  /* You're not allowed to %-encode an IP address, so if it wasn't
   * one before, it better not be one now.
   */
  if (g_hostname_is_ip_address (decoded))
    {
      g_free (decoded);
      g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
		   _("Invalid encoded IP literal '%s' in URI"),
		   raw_host);
      return NULL;
    }

  if (!(flags & G_URI_HOST_IS_DNS))
    return decoded;

  if (!g_utf8_validate (decoded, -1, NULL))
    {
      g_free (decoded);
      g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
		   _("Invalid non-ASCII hostname '%s' in URI"),
		   raw_host);
      return NULL;
    }

  if (!g_hostname_is_non_ascii (decoded))
    return decoded;

  if (flags & G_URI_PARSE_NO_IRI)
    {
      g_free (decoded);
      g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
		   _("Non-ASCII hostname '%s' forbidden in this URI"),
		   decoded);
      return FALSE;
    }

  return g_hostname_to_ascii (decoded);
}

static gboolean
parse_port (const gchar  *raw_port,
            gushort      *port,
            GError      **error)
{
  gchar *end;
  int parsed_port;

  parsed_port = strtoul (raw_port, &end, 10);
  if (*end)
    {
      g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
                   _("Could not parse port '%s' in URI"),
                   raw_port);
      return FALSE;
    }
  else if (parsed_port > 65535)
    {
      g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
                   _("Port '%s' in URI is out of range"),
                   raw_port);
      return FALSE;
    }

  *port = parsed_port;
  return TRUE;
}

gboolean
parse_userinfo (GUri       *uri,
                GUriFlags   flags,
                GError    **error)
{
  GUriFlags userflags = flags & (G_URI_HAS_PASSWORD | G_URI_HAS_AUTH_PARAMS);
  gchar *user, *password, *params;
  gchar *decoded_user, *decoded_password, *decoded_params;
  gchar *start, *end;

  start = uri->encoded_userinfo;
  if (userflags == (G_URI_HAS_PASSWORD | G_URI_HAS_AUTH_PARAMS))
    end = start + strcspn (start, ":;");
  else if (userflags == G_URI_HAS_PASSWORD)
    end = start + strcspn (start, ":");
  else if (userflags == G_URI_HAS_AUTH_PARAMS)
    end = start + strcspn (start, ";");
  else
    end = start + strlen (start);
  user = g_strndup (start, end - start);

  decoded_user = uri_decode (user, flags, error);
  g_free (user);
  if (!decoded_user)
    return FALSE;

  if (*end == ':')
    {
      start = end + 1;
      if (userflags & G_URI_HAS_AUTH_PARAMS)
        end = start + strcspn (start, ";");
      else
        end = start + strlen (start);
      password = g_strndup (start, end - start);

      decoded_password = uri_decode (password, flags, error);
      g_free (password);
      if (!decoded_password)
        {
          g_free (decoded_user);
          return FALSE;
        }
    }
  else
    decoded_password = NULL;

  if (*end == ';')
    {
      start = end + 1;
      end = start + strlen (start);
      params = g_strndup (start, end - start);

      decoded_params = uri_decode (params, flags, error);
      g_free (params);
      if (!decoded_params)
        {
          g_free (decoded_user);
          g_free (decoded_password);
          return FALSE;
        }
    }
  else
    decoded_params = NULL;

  g_free (uri->user);
  uri->user = decoded_user;
  g_free (uri->password);
  uri->password = decoded_password;
  g_free (uri->auth_params);
  uri->auth_params = decoded_params;
  return TRUE;
}

/**
 * g_uri_new:
 * @uri_string: a string representing an absolute URI
 * @flags: flags describing how to parse @uri_string
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Parses @uri_string according to @flags. If the result is not a
 * valid absolute URI, it will be discarded, and an error returned.
 *
 * Return value: a new #GUri.
 *
 * Since: 2.28
 */
GUri *
g_uri_new (const gchar     *uri_string,
	   GUriParseFlags   flags,
	   GError         **error)
{
  return g_uri_new_relative (NULL, uri_string, flags, error);
}

/**
 * g_uri_new_relative:
 * @base_uri: (allow-none): a base URI
 * @uri_string: a string representing a relative or absolute URI
 * @flags: flags describing how to parse @uri_string
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Parses @uri_string according to @flags and, if it is a relative
 * URI, merges it with @base_uri. If the result is not a valid
 * absolute URI, it will be discarded, and an error returned.
 *
 * Return value: a new #GUri.
 *
 * Since: 2.28
 */
GUri *
g_uri_new_relative (GUri            *base_uri,
		    const gchar     *uri_string,
		    GUriParseFlags   flags,
		    GError         **error)
{
  GUri *raw = NULL, *uri = NULL;
  gchar *raw_port = NULL;
  gchar *copy = NULL;

  if (base_uri && !base_uri->scheme)
    {
      g_set_error_literal (error, G_URI_ERROR, G_URI_ERROR_PARSE,
			   _("Base URI is not absolute"));
      return NULL;
    }

  if (!(flags & G_URI_PARSE_STRICT) && strpbrk (uri_string, " \t\n\r"))
    uri_string = copy = uri_cleanup (uri_string);

  /* We use a GUri to store the raw data in, for convenience */
  raw = g_slice_new0 (GUri);
  g_uri_split (uri_string,
	       &raw->scheme, &raw->encoded_userinfo,
	       &raw->host, &raw_port,
	       &raw->encoded_path, &raw->encoded_query,
               &raw->encoded_fragment);
  if (copy)
    g_free (copy);

  if (raw->scheme)
    uri->scheme = g_ascii_strdown (raw->scheme, -1);
  else if (!base_uri)
    {
      g_set_error_literal (error, G_URI_ERROR, G_URI_ERROR_PARSE,
			   _("URI is not absolute, and no base URI was provided"));
      goto fail;
    }

  if (raw->encoded_userinfo)
    {
      uri->encoded_userinfo = uri_normalize (raw->encoded_userinfo, flags, error);
      if (!uri->encoded_userinfo)
	goto fail;
      if (!parse_userinfo (uri, flags, error))
        goto fail;
    }

  if (raw->host)
    {
      uri->encoded_host = g_strdup (raw->host);
      uri->host = parse_host (uri->encoded_host, flags, error);
      if (!uri->host)
	goto fail;
    }

  if (raw_port)
    {
      if (!parse_port (raw_port, &uri->port, error);
        goto fail;
    }

  uri->path = uri_normalize (raw->path, flags, error);
  if (!uri->path)
    goto fail;

  if (raw->query)
    {
      uri->query = uri_normalize (raw->query, flags, error);
      if (!uri->query)
	goto fail;
    }

  if (raw->fragment)
    {
      uri->fragment = uri_normalize (raw->fragment, flags, error);
      if (!uri->fragment)
	goto fail;
    }

  if (!uri->scheme && !base_uri)
    {
      g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
		   _("Could not parse '%s' as absolute URI"),
		   uri_string);
      goto fail;
    }

  if (base_uri)
    {
      /* This is section 5.2.2 of RFC 3986, except that we're doing
       * it in place in @guri rather than copying from R to T.
       */
      if (uri->scheme)
	remove_dot_segments (uri->path);
      else
	{
	  if (uri->host)
	    remove_dot_segments (uri->path);
	  else
	    {
	      if (!*uri->path)
		{
		  g_free (uri->path);
		  uri->path = g_strdup (base_uri->path);
		  g_free (raw->path);
		  raw->path = NULL;
		  if (!uri->query)
		    uri->query = g_strdup (base_uri->query);
		}
	      else
		{
		  if (*uri->path != '/')
		    remove_dot_segments (uri->path);
		  else
		    {
		      gchar *newpath, *last;

		      last = strrchr (base_uri->path, '/');
		      if (last)
			{
			  newpath = g_strdup_printf ("%.*s/%s",
						     (int)(last - base_uri->path),
						     base_uri->path,
						     uri->path);
			}
		      else
			newpath = g_strdup_printf ("/%s", uri->path);

		      g_free (uri->path);
		      uri->path = newpath;
		      g_free (raw->path);
		      raw->path = NULL;

		      remove_dot_segments (uri->path);
		    }
		}

	      uri->userinfo = g_strdup (base_uri->userinfo);
	      uri->host = g_strdup (base_uri->host);
	      uri->port = base_uri->port;
	    }
	}
    }

  g_uri_free (raw);
  g_free (raw_port);
  return uri;

 fail:
  g_uri_free (raw);
  g_free (raw_port);
  g_uri_free (uri);
  return NULL;
}

#define REPARSABLE_FLAGS (G_URI_HOST_IS_DNS | G_URI_HAS_PASSWORD | G_URI_HAS_AUTH_PARAMS)

/**
 * g_uri_reparse:
 * @uri: a #GUri
 * @flags: flags describing how to reparse @uri
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Reparses @uri according to certain flags in @flags. In particular,
 * you can reparse with a different setting for %G_URI_HOST_IS_DNS,
 * %G_URI_HAS_PASSWORD, or %G_URI_HAS_AUTH_PARAMS, to change how those
 * fields are interpreted.
 *
 * The other bits in @flags (eg, %G_URI_PARSE_STRICT) are ignored, and
 * if relevant, the URI will be reparsed using the same values for
 * those flags as it was originally created with.
 *
 * Return value: %TRUE if @uri was successfully reparsed with the
 * new flags, %FALSE if the reparsing failed (in which case @uri will
 * be unchanged).
 *
 * Since: 2.28
 */
gboolean
g_uri_reparse (GUri       *uri,
               GUriFlags   flags,
               GError    **error)
{
  gchar *host;

  flags = (uri->flags & ~REPARSABLE_FLAGS) | (flags & REPARSABLE_FLAGS);
  host = parse_host (uri->encoded_host, flags, error);
  if (!host)
    return FALSE;

  if (!parse_userinfo (uri, flags, error))
    {
      g_free (host);
      return FALSE;
    }

  g_free (uri->host);
  uri->host = host;
  return TRUE;
}

/**
 * g_uri_to_string:
 * @uri: a #GUri
 * @flags: flags describing how to convert @uri
 *
 * Returns a string representing @uri.
 *
 * Return value: a string representing @uri, which the caller must free.
 *
 * Since: 2.28
 */
gchar *
g_uri_to_string (GUri              *uri,
		 GUriToStringFlags  flags)
{
  GString *str;

  g_return_val_if_fail (uri != NULL, NULL);

  str = g_string_new (uri->scheme);
  g_string_append_c (str, ':');

  if (uri->host)
    {
      g_string_append (str, "//");
      if (uri->encoded_userinfo)
        {
          g_string_append (str, uri->encoded_userinfo);
	  g_string_append_c (str, '@');
	}

      if (raw_host)
	g_string_append (str, uri->encoded_host);

      if (uri->port)
	g_string_append_printf (str, ":%d", uri->port);
    }

  if (uri->encoded_path)
    g_string_append (str, uri->encoded_path);

  if (uri->encoded_query)
    {
      g_string_append_c (str, '?');
      g_string_append (str, uri->encoded_query);
    }
  if (uri->encoded_fragment)
    {
      g_string_append_c (str, '#');
      g_string_append (str, uri->encoded_fragment);
    }

  return g_string_free (str, FALSE);
}

/**
 * g_uri_copy:
 * @uri: a #GUri
 *
 * Copies @uri
 *
 * Return value: a copy of @uri
 *
 * Since: 2.28
 */
GUri *
g_uri_copy (GUri *uri)
{
  GUri *dup;

  g_return_val_if_fail (uri != NULL, NULL);

  dup = g_slice_new0 (GUri);
  dup->flags            = uri->flags;
  dup->scheme           = g_strdup (uri->scheme);
  dup->encoded_userinfo = g_strdup (uri->encoded_userinfo);
  dup->encoded_host     = g_strdup (uri->encoded_host);
  dup->port             = uri->port;
  dup->encoded_path     = g_strdup (uri->encoded_path);
  dup->encoded_query    = g_strdup (uri->encoded_query);
  dup->encoded_fragment = g_strdup (uri->encoded_fragment);

  /* Decode host/userinfo. If @uri is valid, this must succeed. */
  g_uri_reparse (dup, dup->flags, NULL);

  return dup;
}

/**
 * g_uri_free:
 * @uri: a #GUri
 *
 * Frees @uri.
 *
 * Since: 2.28
 */
void
g_uri_free (GUri *uri)
{
  g_return_if_fail (uri != NULL);

  g_free (uri->encoded_userinfo);
  g_free (uri->user);
  g_free (uri->password);
  g_free (uri->encoded_host);
  g_free (uri->host);
  g_free (uri->encoded_path);
  g_free (uri->encoded_query);
  g_free (uri->encoded_fragment);

  g_slice_free (GUri, uri);
}

/**
 * g_uri_split:
 * @uri_string: a string containing a relative or absolute URI
 * @strict: whether to parse @uri_string strictly
 * @scheme: (out) (allow-none): on return, contains the scheme, or %NULL
 * @userinfo: (out) (allow-none): on return, contains the userinfo, or %NULL
 * @host: (out) (allow-none): on return, contains the host, or %NULL
 * @port: (out) (allow-none): on return, contains the port, or %NULL
 * @path: (out) (allow-none): on return, contains the path, or %NULL
 * @query: (out) (allow-none): on return, contains the query, or %NULL
 * @fragment: (out) (allow-none): on return, contains the fragment, or %NULL
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Parses @uri_string more-or-less according to the generic grammar of
 * RFC 3986 ("more" if @strict is %TRUE, "less" if %FALSE), and
 * outputs the pieces into the provided variables. This is a low-level
 * method that does not do any pre- or post-processing of @uri_string,
 * and is "garbage in, garbage out"; it just splits @uri_string into
 * pieces at the appropriate punctuation characters (consuming
 * delimiters as appropriate), and returns the pieces. Components that
 * are not present in @uri_string will be set to %NULL (but note that
 * the path is always present, though it may be an empty string).
 *
 * Since: 2.28
 */
void
g_uri_split (const gchar  *uri_string,
             gboolean      strict,
             gchar       **scheme,
             gchar       **userinfo,
             gchar       **host,
             gchar       **port,
             gchar       **path,
             gchar       **query,
             gchar       **fragment)
{
  const gchar *end, *colon, *at, *path, *semi, *question;
  const gchar *p, *bracket, *hostend;
  int len;

  if (scheme)
    *scheme = NULL;
  if (userinfo)
    *userinfo = NULL;
  if (host)
    *host = NULL;
  if (port)
    *port = -1;
  if (path)
    *path = NULL;
  if (query)
    *query = NULL;
  if (fragment)
    *fragment = NULL;

  /* Find scheme: initial [a-z+.-]* substring until ":" */
  p = uri_string;
  while (*p && (g_ascii_isalnum (*p) ||
		*p == '.' || *p == '+' || *p == '-'))
    p++;

  if (p > uri_string && *p == ':')
    {
      if (scheme)
	*scheme = g_strndup (uri_string, p - uri_string);
      p++;
    }
  else
    p = uri_string;

  /* Check for authority */
  if (strncmp (p, "//", 2) == 0)
    {
      p += 2;

      path = p + strcspn (p, "/?#");
      at = memchr (p, '@', path - p);
      if (at)
	{
          if (!strict)
            {
              gchar *next_at;

              /* Any "@"s in the userinfo must be %-encoded, but
               * people get this wrong sometimes. Since "@"s in the
               * hostname are unlikely (and also wrong anyway), assume
               * that if there are extra "@"s, they belong in the
               * userinfo.
               */
              do
                {
                  next_at = memchr (at + 1, '@', path - (at + 1));
                  if (next_at)
                    at = next_at;
                }
              while (next_at);
            }

	  if (userinfo)
	    *userinfo = g_strndup (p, at - p);
	  p = at + 1;
	}

      if (!strict)
        {
          semi = strchr (p, ';');
          if (semi && semi < path)
            {
              /* Technically, semicolons are allowed in the "host"
               * production, but no one ever does this, and some
               * schemes mistakenly use semicolon as a delimiter
               * marking the start of the path. We have to check this
               * after checking for userinfo though, because a
               * semicolon before the "@" must be part of the
               * userinfo.
               */
              path = semi;
            }
        }

      /* Find host and port. The host may be a bracket-delimited IPv6
       * address, in which case the colon delimiting the port must come
       * after the close bracket.
       */
      if (*p == '[')
        {
          bracket = memchr (p, ']', path - p);
          if (bracket && *(bracket + 1) == ':')
            colon = bracket + 1;
          else
            colon = NULL;
	}
      else
        colon = memchr (p, ':', path - p);

      if (host)
        {
	  hostend = colon ? colon : path;
          *host = g_strndup (p, hostend - p);
        }

      if (colon && colon != path - 1 && port)
        *port = g_strndup (colon + 1, path - (colon + 1));

      p = path;
    }

  /* Find fragment. */
  end = p + strcspn (p, "#");
  if (*end == '#')
    {
      if (fragment)
	*fragment = g_strdup (end + 1);
    }

  /* Find query */
  question = memchr (p, '?', end - p);
  if (question)
    {
      if (query)
	*query = g_strndup (question + 1, end - (question + 1));
      end = question;
    }

  if (path)
    *path = g_strndup (p, end - p);
}

/* This is just a copy of g_str_hash() with g_ascii_toupper()s added */
static guint
str_ascii_case_hash (gconstpointer key)
{
  const char *p = key;
  guint h = g_ascii_toupper(*p);

  if (h)
    for (p += 1; *p != '\0'; p++)
      h = (h << 5) - h + g_ascii_toupper(*p);

  return h;
}

static gboolean
str_ascii_case_equal (gconstpointer v1,
                      gconstpointer v2)
{
	const char *string1 = v1;
	const char *string2 = v2;

	return g_ascii_strcasecmp (string1, string2) == 0;
}

/**
 * g_uri_parse_params:
 * @params: a string containing "attribute=value" parameters
 * @length: the length of @params, or -1 if it is NUL-terminated
 * @separator: the separator character between parameters.
 *   (usually ';', but sometimes '&')
 * @case_insentitive: whether to match parameter names case-insensitively
 *
 * Many URI schemes include one or more attribute/value pairs
 * as part of the URI value. This method can be used to parse them
 * into a hash table.
 *
 * The @params string is assumed to still be %<!-- -->-encoded, but
 * the returned values will be fully decoded. (Thus it is possible
 * that the returned values may contain '=' or @separator, if the
 * value was encoded in the input.) Invalid %<!-- -->-encoding is
 * treated as with the non-%G_URI_PARSE_STRICT rules for g_uri_new().
 * (However, if @params is the path or query string from a #GUri that
 * was parsed with %G_URI_PARSE_STRICT, then you already know that it
 * does not contain any invalid encoding.)
 *
 * Return value: (element-type utf8 utf8): a hash table of
 * attribute/value pairs. Both names and values will be fully-decoded.
 * If @params cannot be parsed (eg, it contains two @separator
 * characters in a row), then %NULL is returned.
 *
 * Since: 2.28
 */
GHashTable *
g_uri_parse_params (const gchar     *params,
		    gssize           length,
		    gchar            separator,
		    gboolean         case_insensitive)
{
  GHashTable *hash;
  const char *end, attr, *attr_end, *value, *value_end;
  char *decoded_attr, *decoded_value;

  if (case_insensitive)
    {
      hash = g_hash_table_new_full (str_ascii_case_hash,
				    str_ascii_case_equal,
				    g_free, g_free);
    }
  else
    {
      hash = g_hash_table_new_full (g_str_hash, g_str_equal,
				    g_free, g_free);
    }

  if (length == -1)
    end = params + strlen (params);
  else
    end = params + length;

  attr = params;
  while (attr < end)
    {
      value_end = memchr (attr, separator, end - attr);
      if (!value_end)
	value_end = end;

      attr_end = memchr (attr, '=', value_end - attr);
      if (!attr_end)
	{
	  g_hash_table_destroy (hash);
	  return NULL;
	}
      decoded_attr = uri_decode (attr, attr_end - attr, 0, NULL);
      if (!decoded_attr)
        {
	  g_hash_table_destroy (hash);
	  return NULL;
	}

      value = attr_end + 1;
      decoded_value = uri_decode (value, value_end - value, 0, NULL);
      if (!decoded_value)
        {
          g_free (decoded_attr);
	  g_hash_table_destroy (hash);
	  return NULL;
	}

      g_hash_table_insert (hash, decoded_attr, decoded_value);
      attr = value_end + 1;
    }

  return hash;
}

/**
 * g_uri_parse_host:
 * @uri_string: a string containing a network URI
 * @flags: flags for parsing @uri_string
 * @scheme: (out): on return, will contain @uri_string's URI scheme
 * @host: (out): on return, will contain @uri_string's decoded hostname
 * @port: (out): on return, will contain @uri_string's port, or %0
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Utility function for parsing "network" URIs. This extracts just the
 * scheme, host, and port from @uri_string. All three out parameters
 * are mandatory.
 *
 * Return value: %TRUE on success, %FALSE on failure.
 */
gboolean
g_uri_parse_host (const gchar  *uri_string,
                  GUriFlags     flags,
                  gchar       **scheme,
                  gchar       **host,
                  gushort      *port,
                  GError      **error)
{
  gchar *raw_scheme, *raw_host, *raw_port;

  g_uri_split (uri_string, &raw_scheme, NULL, &raw_host, &raw_port,
               NULL, NULL, NULL);

  if (raw_port)
    {
      if (!parse_port (raw_port, port, error))
        goto fail;
    }
  else
    *port = 0;

  *host = parse_host (raw_host, flags, error);
  if (!*host)
    goto fail;

  *scheme = raw_scheme;
  g_free (raw_host);
  g_free (raw_port);
  return TRUE;

 fail:
  g_free (raw_scheme);
  g_free (raw_host);
  g_free (raw_port);
  return FALSE;
}

/**
 * g_uri_get_scheme:
 * @uri: a #GUri
 *
 * Gets @uri's scheme.
 *
 * Return value: @uri's scheme.
 *
 * Since: 2.28
 */
const gchar *
g_uri_get_scheme (GUri *uri)
{
  return uri->scheme;
}

/**
 * g_uri_set_scheme:
 * @uri: a #GUri
 * @scheme: the URI scheme
 *
 * Sets @uri's scheme to @scheme.
 *
 * Since: 2.28
 */
void
g_uri_set_scheme (GUri        *uri,
                  const gchar *scheme)
{
  g_free (uri->scheme);
  uri->scheme = g_strdup (scheme);
}

/**
 * g_uri_get_encoded_userinfo:
 * @uri: a #GUri
 *
 * Gets @uri's userinfo, still %<!-- -->-encoded. See also
 * g_uri_get_user(), g_uri_get_password(), and g_uri_get_auth_params().
 *
 * Return value: @uri's %<!-- -->-encoded userinfo.
 *
 * Since: 2.28
 */
const gchar *
g_uri_get_encoded_userinfo (GUri *uri)
{
  return uri->encoded_userinfo;
}

/**
 * g_uri_set_encoded_userinfo:
 * @uri: a #GUri
 * @userinfo: the %<!-- -->-encoded userinfo, or %NULL
 *
 * Sets @uri's userinfo to @userinfo. This will also change its user
 * value, and if @uri was parsed with %G_URI_HAS_PASSWORD or
 * %G_URI_HAS_AUTH_PARAMS, it will also change its password and auth
 * params.
 *
 * Since: 2.28
 */
void
g_uri_set_encoded_userinfo (GUri        *uri,
                            const gchar *userinfo)
{
  g_free (uri->encoded_userinfo);
  uri->encoded_userinfo = g_strdup (userinfo);
  reparse_userinfo (uri);
}

/**
 * g_uri_get_user:
 * @uri: a #GUri
 *
 * Gets @uri's user. If @uri was parsed with %G_URI_HAS_PASSWORD or
 * %G_URI_HAS_AUTH_PARAMS, this is the string that appears before the
 * password and parameters in the userinfo. If not, then the entire
 * userinfo is considered the user. In either case, unlike userinfo,
 * this value will be %<!-- -->-decoded.
 *
 * Return value: @uri's user.
 *
 * Since: 2.28
 */
const gchar *
g_uri_get_user (GUri *uri)
{
  return uri->user;
}

/**
 * g_uri_set_user:
 * @uri: a #GUri
 * @user: the username, or %NULL
 *
 * Sets @uri's user to @user. See g_uri_get_user() for a description
 * of how this interacts with various parsing flags.
 *
 * Since: 2.28
 */
void
g_uri_set_user (GUri        *uri,
                const gchar *user)
{
  g_free (uri->user);
  uri->user = g_strdup (user);
  reset_userinfo (uri);
}

/**
 * g_uri_get_password:
 * @uri: a #GUri
 *
 * Gets @uri's password. If @uri was not parsed with
 * %G_URI_HAS_PASSWORD, this will always be %NULL.
 *
 * Return value: @uri's password.
 *
 * Since: 2.28
 */
const gchar *
g_uri_get_password (GUri *uri)
{
  return uri->password;
}

/**
 * g_uri_set_password:
 * @uri: a #GUri
 * @password: the password, or %NULL
 *
 * Sets @uri's password to @password. If @uri had not previously been
 * parsed with %G_URI_HAS_PASSWORD, this will set that flag, which may
 * cause the interpretation of the other userinfo fields to change.
 *
 * Since: 2.28
 */
void
g_uri_set_password (GUri        *uri,
                    const gchar *password)
{
  if (!(uri->flags & G_URI_HAS_PASSWORD))
    {
      uri->flags |= G_URI_HAS_PASSWORD;
      reparse_userinfo (uri);
    }

  g_free (uri->password);
  uri->password = g_strdup (password);

  reset_userinfo (uri);
}

/**
 * g_uri_get_auth_params:
 * @uri: a #GUri
 *
 * Gets @uri's authentication parametsr. If @uri was not parsed with
 * %G_URI_HAS_AUTH_PARAMS, this will always be %NULL.
 *
 * Return value: @uri's authentication parameters.
 *
 * Since: 2.28
 */
const gchar *
g_uri_get_auth_params (GUri *uri)
{
  return uri->auth_params;
}

/**
 * g_uri_set_auth_params:
 * @uri: a #GUri
 * @auth_params: the authentication parameters, or %NULL
 *
 * Sets @uri's authentication parameters to @auth_params. If @uri had
 * not previously been parsed with %G_URI_HAS_AUTH_PARAMS, this will
 * set that flag, which may cause the interpretation of the other
 * userinfo fields to change.
 *
 * Since: 2.28
 */
void
g_uri_set_auth_params (GUri        *uri,
                       const gchar *auth_params)
{
  if (!(uri->flags & G_URI_HAS_AUTH_PARAMS))
    {
      uri->flags |= G_URI_HAS_AUTH_PARAMS;
      reparse_userinfo (uri);
    }

  g_free (uri->auth_params);
  uri->auth_params = g_strdup (auth_params);

  reset_userinfo (uri);
}

/**
 * g_uri_get_host:
 * @uri: a #GUri
 *
 * Gets @uri's host.
 *
 * Return value: @uri's host.
 *
 * Since: 2.28
 */
const gchar *
g_uri_get_host (GUri *uri)
{
  return uri->host;
}

/**
 * g_uri_set_host:
 * @uri: a #GUri
 * @host: the hostname or IP address, or %NULL
 *
 * Sets @uri's host to @host.
 *
 * If @host is an IPv6 IP address, it should not include the brackets
 * required by the URI syntax; they will be added automatically when
 * converting @uri to a string.
 *
 * Since: 2.28
 */
void
g_uri_set_host (GUri        *uri,
                const gchar *host)
{
  g_free (uri->host);
  uri->host = g_strdup (host);
}

/**
 * g_uri_get_port:
 * @uri: a #GUri
 *
 * Gets @uri's port.
 *
 * Return value: @uri's port, or %0 if it was unset
 *
 * Since: 2.28
 */
guint
g_uri_get_port (GUri *uri)
{
  return uri->port;
}

/**
 * g_uri_set_port:
 * @uri: a #GUri
 * @port: the port, or %0
 *
 * Sets @uri's port to @port. If @port is 0, @uri will not have an
 * explicitly-specified port.
 *
 * Since: 2.28
 */
void
g_uri_set_port (GUri  *uri,
                guint  port)
{
  uri->port = port;
}

/**
 * g_uri_get_encoded_path:
 * @uri: a #GUri
 *
 * Gets @uri's path, still %<!-- -->-encoded.
 *
 * Return value: @uri's path.
 *
 * Since: 2.28
 */
const gchar *
g_uri_get_encoded_path (GUri *uri)
{
  return uri->encoded_path;
}

/**
 * g_uri_set_encoded_path:
 * @uri: a #GUri
 * @path: the %<!-- -->-encoded path
 *
 * Sets @uri's path to @path.
 *
 * Since: 2.28
 */
void
g_uri_set_encoded_path (GUri        *uri,
                        const gchar *path)
{
  g_free (uri->encoded_path);
  uri->encoded_path = g_strdup (path);
}

/**
 * g_uri_get_encoded_query:
 * @uri: a #GUri
 *
 * Gets @uri's query, still %<!-- -->-encoded.
 *
 * Return value: @uri's query.
 *
 * Since: 2.28
 */
const gchar *
g_uri_get_encoded_query (GUri *uri)
{
  return uri->encoded_query;
}

/**
 * g_uri_set_encoded_query:
 * @uri: a #GUri
 * @query: the %<!-- -->-encoded query
 *
 * Sets @uri's query to @query.
 *
 * Since: 2.28
 */
void
g_uri_set_encoded_query (GUri        *uri,
                         const gchar *query)
{
  g_free (uri->encoded_query);
  uri->encoded_query = g_strdup (query);
}

/**
 * g_uri_get_encoded_fragment:
 * @uri: a #GUri
 *
 * Gets @uri's fragment, still %<!-- -->-encoded.
 *
 * Return value: @uri's fragment.
 *
 * Since: 2.28
 */
const gchar *
g_uri_get_encoded_fragment (GUri *uri)
{
  return uri->encoded_fragment;
}

/**
 * g_uri_set_encoded_fragment:
 * @uri: a #GUri
 * @fragment: the %<!-- -->-encoded fragment
 *
 * Sets @uri's fragment to @fragment.
 *
 * Since: 2.28
 */
void
g_uri_set_encoded_fragment (GUri        *uri,
                            const gchar *fragment)
{
  g_free (uri->encoded_fragment);
  uri->encoded_fragment = g_strdup (fragment);
}
