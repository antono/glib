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

/**
 * g_uri_split:
 * @uri_string: a string containing a relative or absolute URI
 * @scheme: (out) (allow-none): on return, contains the scheme, or %NULL
 * @userinfo: (out) (allow-none): on return, contains the userinfo, or %NULL
 * @host: (out) (allow-none): on return, contains the host, or %NULL
 * @port: (out) (allow-none): on return, contains the port, or -1
 * @path: (out) (allow-none): on return, contains the path, or %NULL
 * @query: (out) (allow-none): on return, contains the query, or %NULL
 * @fragment: (out) (allow-none): on return, contains the fragment, or %NULL
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Parses @uri_string according to the generic grammar of RFC 3986,
 * and outputs the pieces into the provided variables. This is a
 * low-level method that does not do any pre- or post-processing of
 * @uri_string; it just splits @uri_string into pieces at the
 * appropriate punctuation characters (consuming some and outputting
 * others), and returns the pieces. Components that are not present in
 * @uri_string will be set to %NULL (but note that the path is always
 * present, though it may be empty).
 *
 * For the most part, this method is "garbage in, garbage out", but
 * certain syntax errors (such as a non-numeric port) will result in
 * the parse failing completely.
 *
 * Return value: success or failure
 */
gboolean
g_uri_split (const gchar  *uri_string,
	     gchar       **scheme,
	     gchar       **userinfo,
	     gchar       **host,
	     gint        **port,
	     gchar       **path,
	     gchar       **query,
	     gchar       **fragment,
	     GError      **error)
{
  const gchar *end, *hash, *colon, *at, *path, *semi, *question;
  const gchar *p, *hostend;
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
	  gchar *next_at;

	  /* Any "@"s in the userinfo must be %-encoded, but people
	   * get this wrong sometimes. Since "@"s in the hostname are
	   * unlikely (and also wrong anyway), assume that if there
	   * are extra "@"s, they belong in the userinfo.
	   */
	  do
	    {
	      next_at = memchr (at + 1, '@', path - (at + 1));
	      if (next_at)
		at = next_at;
	    }
	  while (next_at);

	  if (userinfo)
	    *userinfo = g_strndup (p, at - p);
	  p = at + 1;
	}

      semi = strchr (p, ';');
      if (semi && semi < path)
	{
	  /* Technically, semicolons are allowed in the "host"
	   * production, but no one ever does, and some schemes
	   * mistakenly use semicolon as a delimiter marking the start
	   * of the path. We have to check this after checking for
	   * userinfo though, because a semicolon before the "@"
	   * must be part of the userinfo.
	   */
	  path = semi;
	}

      /* Find host and port. */
      if (*p == '[')
	{
	  hostend = memchr (p, ']', path - p);
	  if (!hostend)
	    {
	      g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
			   _("Illegal hostname in URI '%s'"), uri_string);
	      goto fail;
	    }

	  p++;
	  hostend--;

	  if (*(hostend + 2) == ':')
	    colon = hostend;
	  else if (hostend + 2 == path)
	    colon = NULL;
	  else
	    {
	      g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
			   _("Illegal hostname in URI '%s'"), uri_string);
	      goto fail;
	    }
	}
      else
	{
	  colon = memchr (p, ':', path - p);
	  hostend = colon ? colon : path;
	}

      if (host)
	*host = g_strndup (p, hostend - p);

      if (colon && colon != path - 1)
	{
	  glong parsed_port;
	  gchar *end;

	  parsed_port = strtol (colon + 1, &end, 10);
	  if (parsed_port < 0 || parsed_port > G_MAXUSHORT || end != path)
	    {
	      g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
			   _("Could not parse port '%.*s' in URI"),
			   colon + 1, path - (colon + 1));
	      goto fail;
	    }

	  if (port)
	    *port = (int)parsed_port;
	}
      else if (port)
	*port = -1;

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

  return TRUE;

 fail:
  if (scheme && *scheme)
    {
      g_free (*scheme);
      *scheme = NULL;
    }
  if (userinfo && *userinfo)
    {
      g_free (*userinfo);
      *userinfo = NULL;
    }
  if (host && *host)
    {
      g_free (*host);
      *host = NULL;
    }
  /* We know none of the other fields have been set yet */
  return FALSE;
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
 * Return value: (element-type utf8 utf8): a hash table of
 * attribute/value pairs. Both names and values will be fully-decoded.
 * If @params cannot be parsed properly, %NULL is returned.
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

/**
 * g_uri_parse_host:
 * @raw_host: the raw (percent-encoded) "host" field of the URI
 * @flags: parsing flags
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Extracts a valid hostname or IP address out of @raw_host. If
 * @raw_host contains an IP address, that address will be returned
 * (without the square brackets in the case of an IPv6 address).
 * If @raw_host contains a Unicode-encoded hostname, it will be
 * decoded and converted to ASCII.
 *
 * Return value: the parsed hostname or IP address, or %NULL on error.
 */
char *
g_uri_parse_host (const gchar     *raw_host,
		  GUriParseFlags   flags,
		  GError         **error)
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

  if (flags & G_URI_PARSE_HTML5)
    flags &= ~G_URI_PARSE_ALLOW_SINGLE_PERCENT;
  decoded = uri_decode (raw_host, flags, error);
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
      return NULL;
    }

  ace = g_hostname_to_ascii (decoded);
  g_free (decoded);
  return ace;
}

/**
 * g_uri_parse_userinfo:
 * @raw_userinfo: the raw (percent-encoded) "userinfo" field of the URI
 * @flags: parsing flags
 * @username: (out) (allow-none): on output, will contain the decoded
 *   username from @raw_userinfo
 * @password: (out) (allow-none): on output, will contain the decoded
 *   password from @raw_userinfo
 * @params: (out) (allow-none): on output, will contain the undecoded
 *   params from @raw_userinfo
 * @error: #GError for error reporting, or %NULL to ignore.
 *
 * Parses @raw_userinfo according to @flags, and outputs it into
 * @username, @password, and @params.
 *
 * If @flags contains %G_URI_PARSE_USERINFO_PARAMS, anything after a
 * ';' in @raw_userinfo will be interpreted as parameters (eg,
 * authentication type), and returned (undecoded) in @params.
 *
 * If @flags contains %G_URI_PARSE_USERINFO_PASSWORD, anything after a
 * ':' in @raw_userinfo (and before ';' if params are also specified)
 * will be interpreted as a password and returned (decoded) in
 * @password.
 *
 * Whatever portion of @raw_userinfo that is not interpreted as params
 * or password will be output in @username.
 *
 * Return value: %TRUE on success, %FALSE on parse failure.
 */
gboolean
g_uri_parse_userinfo (const gchar     *raw_userinfo,
                      GUriParseFlags   flags,
                      gchar          **username,
                      gchar          **password,
                      gchar          **params,
                      GError         **error)
{
  FIXME;
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

  return g_strdup_free (copy, FALSE);
}

static gboolean
uri_normalize (gchar *part, GUriParseFlags flags, GError **error)
{
  gchar *s, *d;
  guchar c;

  s = d = part;
  do
    {
      if (*s == '%')
	{
	  if (!g_ascii_isxdigit (s[1]) ||
	      !g_ascii_isxdigit (s[2]))
	    {
	      if (!(flags & G_URI_PARSE_ALLOW_SINGLE_PERCENT))
		{
		  g_set_error_literal (error, G_URI_ERROR, G_URI_ERROR_PARSE,
				       _("Illegal %-sequence in URI"));
		  return FALSE;
		}
	      *d++ = *s;
	      continue;
	    }

	  c = HEXCHAR (s);
	  if (soup_char_is_uri_unreserved (c))
	    {
	      *d++ = c;
	      s += 2;
	    }
	  else
	    {
	      *d++ = *s++;
	      *d++ = g_ascii_toupper (*s++);
	      *d++ = g_ascii_toupper (*s);
	    }
	}
      else
	*d++ = *s;
    }
  while (*s++);

  return TRUE;
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
	       &raw->scheme, &raw->userinfo,
	       &raw->host, &raw_port,
	       &raw->path, &raw->query, &raw->fragment);
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

  if (raw->userinfo)
    {
      uri->userinfo = uri_normalize (raw->userinfo, flags, error);
      if (!uri->userinfo)
	goto fail;
    }

  if (raw->host)
    {
      uri->host = g_uri_parse_host (raw->host, flags, error);
      if (!uri->host)
	goto fail;
    }

  if (raw_port)
    {
      gchar *end;

      raw->port = strtoul (raw_port, &end, 10);
      if (*end)
	{
	  g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
		       _("Could not parse port '%s' in URI"),
		       raw_port);
	  goto fail;
	}
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

 fail:
  g_uri_free (guri);
  return NULL;
}

GUri *
g_uri_new (const gchar     *uri_string,
	   GUriParseFlags   flags,
	   GError         **error)
{
  return g_uri_new (NULL, uri_string, flags, error);
}

/**
 * g_uri_to_string:
 * @uri: a #GUri
 * @flags: flags describing how to convert @uri
 *
 * Returns a string representing @uri.
 *
 * Return value: a string representing @uri, which the caller must free.
 **/
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
      if (uri->userinfo)
	{
	  if (uri->raw_userinfo)
	    g_string_append (str, uri->raw_userinfo);
	  else
	    append_uri_encoded (str, uri->userinfo, NULL);
	  g_string_append_c (str, '@');
	}

      if (uri->raw_host)
	g_string_append (str, uri->raw_host);
      else
	{
	  if (strchr (uri->host, ':'))
	    {
	      g_string_append_c (str, '[');
	      g_string_append (str, uri->host);
	      g_string_append_c (str, ']');
	    }
	  else
	    append_uri_encoded (str, uri->host, NULL);
	}

      if (uri->port > -1)
	g_string_append_printf (str, ":%d", uri->port);
    }

  if (uri->path)
    g_string_append (str, uri->path);

  if (uri->query)
    {
      g_string_append_c (str, '?');
      g_string_append (str, uri->query);
    }
  if (uri->fragment)
    {
      g_string_append_c (str, '#');
      g_string_append (str, uri->fragment);
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
 */
GUri *
g_uri_copy (GUri *uri)
{
  GUri *dup;

  g_return_val_if_fail (uri != NULL, NULL);

  dup = g_slice_new0 (GUri);
  dup->scheme   = uri->scheme;
  dup->user     = g_strdup (uri->user);
  dup->password = g_strdup (uri->password);
  dup->host     = g_strdup (uri->host);
  dup->port     = uri->port;
  dup->path     = g_strdup (uri->path);
  dup->query    = g_strdup (uri->query);
  dup->fragment = g_strdup (uri->fragment);

  return dup;
}

/**
 * g_uri_free:
 * @uri: a #GUri
 *
 * Frees @uri.
 */
void
g_uri_free (GUri *uri)
{
  g_return_if_fail (uri != NULL);

  g_free (uri->user);
  g_free (uri->password);
  g_free (uri->host);
  g_free (uri->path);
  g_free (uri->query);
  g_free (uri->fragment);

  g_slice_free (GUri, uri);
}

static void
append_uri_encoded (GString *str, const gchar *in, const gchar *extra_enc_chars)
{
  const unsigned gchar *s = (const unsigned gchar *)in;

  while (*s) {
    if (soup_char_is_uri_percent_encoded (*s) ||
	soup_char_is_uri_gen_delims (*s) ||
	(extra_enc_chars && strchr (extra_enc_chars, *s)))
      g_string_append_printf (str, "%%%02X", (int)*s++);
    else
      g_string_append_c (str, *s++);
  }
}

/**
 * g_uri_encode:
 * @part: a URI part
 * @escape_extra: additional reserved gcharacters to escape (or %NULL)
 *
 * This %<!-- -->-encodes the given URI part and returns the escaped
 * version in allocated memory, which the caller must free when it is
 * done.
 *
 * Return value: the encoded URI part
 **/
char *
g_uri_encode (const gchar *part, const gchar *escape_extra)
{
  GString *str;
  gchar *encoded;

  str = g_string_new (NULL);
  append_uri_encoded (str, part, escape_extra);
  encoded = str->str;
  g_string_free (str, FALSE);

  return encoded;
}

#define XDIGIT(c) ((c) <= '9' ? (c) - '0' : ((c) & 0x4F) - 'A' + 10)
#define HEXCHAR(s) ((XDIGIT (s[1]) << 4) + XDIGIT (s[2]))

static gchar *
uri_decoded_copy (const gchar *part, int length, gboolean fixup)
{
  unsigned gchar *s, *d;
  gchar *decoded = g_strndup (part, length);

  s = d = (unsigned gchar *)decoded;
  do {
    if (*s == '%') {
      if (!g_ascii_isxdigit (s[1]) ||
	  !g_ascii_isxdigit (s[2])) {
	if (!fixup) {
	  g_free (decoded);
	  return NULL;
	}
	*d++ = *s;
	continue;
      }
      *d++ = HEXCHAR (s);
      s += 2;
    } else
      *d++ = *s;
  } while (*s++);

  return decoded;
}

/**
 * g_uri_decode:
 * @part: a URI part
 *
 * Fully %<!-- -->-decodes @part.
 *
 * Return value: the decoded URI part, or %NULL if an invalid percent
 * code was encountered.
 */
char *
g_uri_decode (const gchar *part)
{
  return uri_decoded_copy (part, strlen (part), FALSE);
}

/**
 * g_uri_normalize:
 * @part: a URI part
 * @unescape_extra: reserved gcharacters to unescape (or %NULL)
 *
 * %<!-- -->-decodes any "unreserved" gcharacters (or gcharacters in
 * @unescape_extra) in @part.
 *
 * "Unreserved" gcharacters are those that are not allowed to be used
 * for punctuation according to the URI spec. For example, letters are
 * unreserved, so g_uri_normalize() will turn
 * <literal>http://example.com/foo/b%<!-- -->61r</literal> into
 * <literal>http://example.com/foo/bar</literal>, which is guaranteed
 * to mean the same thing. However, "/" is "reserved", so
 * <literal>http://example.com/foo%<!-- -->2Fbar</literal> would not
 * be changed, because it might mean something different to the
 * server.
 *
 * Return value: the normalized URI part, or %NULL if an invalid percent
 * code was encountered.
 */
char *
g_uri_normalize (const gchar *part, const gchar *unescape_extra)
{
  return uri_normalized_copy (part, strlen (part), unescape_extra, FALSE);
}


/**
 * g_uri_uses_default_port:
 * @uri: a #GUri
 *
 * Tests if @uri uses the default port for its scheme. (Eg, 80 for
 * http.) (This only works for http and https; libsoup does not know
 * the default ports of other protocols.)
 *
 * Return value: %TRUE or %FALSE
 **/
gboolean
g_uri_uses_default_port (GUri *uri)
{
  g_return_val_if_fail (uri->scheme == G_URI_SCHEME_HTTP ||
			uri->scheme == G_URI_SCHEME_HTTPS ||
			uri->scheme == G_URI_SCHEME_FTP, FALSE);

  return uri->port == soup_scheme_default_port (uri->scheme);
}

/**
 * G_URI_SCHEME_HTTP:
 *
 * "http" as an interned string. This can be compared directly against
 * the value of a #GUri's <structfield>scheme</structfield>
 **/

/**
 * G_URI_SCHEME_HTTPS:
 *
 * "https" as an interned string. This can be compared directly
 * against the value of a #GUri's <structfield>scheme</structfield>
 **/

/**
 * g_uri_get_scheme:
 * @uri: a #GUri
 *
 * Gets @uri's scheme.
 *
 * Return value: @uri's scheme.
 *
 * Since: 2.32
 **/
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
 * Sets @uri's scheme to @scheme. This will also set @uri's port to
 * the default port for @scheme, if known.
 **/
void
g_uri_set_scheme (GUri *uri, const gchar *scheme)
{
  uri->scheme = g_uri_parse_scheme (scheme, strlen (scheme));
  uri->port = soup_scheme_default_port (uri->scheme);
}

/**
 * g_uri_get_user:
 * @uri: a #GUri
 *
 * Gets @uri's user.
 *
 * Return value: @uri's user.
 *
 * Since: 2.32
 **/
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
 * Sets @uri's user to @user.
 **/
void
g_uri_set_user (GUri *uri, const gchar *user)
{
  g_free (uri->user);
  uri->user = g_strdup (user);
}

/**
 * g_uri_get_password:
 * @uri: a #GUri
 *
 * Gets @uri's password.
 *
 * Return value: @uri's password.
 *
 * Since: 2.32
 **/
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
 * Sets @uri's password to @password.
 **/
void
g_uri_set_password (GUri *uri, const gchar *password)
{
  g_free (uri->password);
  uri->password = g_strdup (password);
}

/**
 * g_uri_get_host:
 * @uri: a #GUri
 *
 * Gets @uri's host.
 *
 * Return value: @uri's host.
 *
 * Since: 2.32
 **/
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
 **/
void
g_uri_set_host (GUri *uri, const gchar *host)
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
 * Return value: @uri's port.
 *
 * Since: 2.32
 **/
guint
g_uri_get_port (GUri *uri)
{
  return uri->port;
}

/**
 * g_uri_set_port:
 * @uri: a #GUri
 * @port: the port, or 0
 *
 * Sets @uri's port to @port. If @port is 0, @uri will not have an
 * explicitly-specified port.
 **/
void
g_uri_set_port (GUri *uri, guint port)
{
  uri->port = port;
}

/**
 * g_uri_get_path:
 * @uri: a #GUri
 *
 * Gets @uri's path.
 *
 * Return value: @uri's path.
 *
 * Since: 2.32
 **/
const gchar *
g_uri_get_path (GUri *uri)
{
  return uri->path;
}

/**
 * g_uri_set_path:
 * @uri: a #GUri
 * @path: the path
 *
 * Sets @uri's path to @path.
 **/
void
g_uri_set_path (GUri *uri, const gchar *path)
{
  g_free (uri->path);
  uri->path = g_strdup (path);
}

/**
 * g_uri_get_query:
 * @uri: a #GUri
 *
 * Gets @uri's query.
 *
 * Return value: @uri's query.
 *
 * Since: 2.32
 **/
const gchar *
g_uri_get_query (GUri *uri)
{
  return uri->query;
}

/**
 * g_uri_set_query:
 * @uri: a #GUri
 * @query: the query
 *
 * Sets @uri's query to @query.
 **/
void
g_uri_set_query (GUri *uri, const gchar *query)
{
  g_free (uri->query);
  uri->query = g_strdup (query);
}

/**
 * g_uri_set_query_from_form:
 * @uri: a #GUri
 * @form: a #GHashTable containing HTML form information
 *
 * Sets @uri's query to the result of encoding @form according to the
 * HTML form rules. See soup_form_encode_hash() for more information.
 **/
void
g_uri_set_query_from_form (GUri *uri, GHashTable *form)
{
  g_free (uri->query);
  uri->query = soup_form_encode_urlencoded (form);
}

/**
 * g_uri_set_query_from_fields:
 * @uri: a #GUri
 * @first_field: name of the first form field to encode into query
 * @...: value of @first_field, followed by additional field names
 * and values, terminated by %NULL.
 *
 * Sets @uri's query to the result of encoding the given form fields
 * and values according to the * HTML form rules. See
 * soup_form_encode() for more information.
 **/
void
g_uri_set_query_from_fields (GUri    *uri,
				const gchar *first_field,
				...)
{
  va_list args;

  g_free (uri->query);
  va_start (args, first_field);
  uri->query = soup_form_encode_valist (first_field, args);
  va_end (args);
}

/**
 * g_uri_get_fragment:
 * @uri: a #GUri
 *
 * Gets @uri's fragment.
 *
 * Return value: @uri's fragment.
 *
 * Since: 2.32
 **/
const gchar *
g_uri_get_fragment (GUri *uri)
{
  return uri->fragment;
}

/**
 * g_uri_set_fragment:
 * @uri: a #GUri
 * @fragment: the fragment
 *
 * Sets @uri's fragment to @fragment.
 **/
void
g_uri_set_fragment (GUri *uri, const gchar *fragment)
{
  g_free (uri->fragment);
  uri->fragment = g_strdup (fragment);
}

GType
g_uri_get_type (void)
{
  static volatile gsize type_volatile = 0;

  if (g_once_init_enter (&type_volatile)) {
    GType type = g_boxed_type_register_static (
					       g_intern_static_string ("GUri"),
					       (GBoxedCopyFunc) g_uri_copy,
					       (GBoxedFreeFunc) g_uri_free);
    g_once_init_leave (&type_volatile, type);
  }
  return type_volatile;
}
