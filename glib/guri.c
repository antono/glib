
/**
 * g_uri_split:
 * @uri_string: a string containing a relative or absolute URI
 * @scheme: (out) (allow-none): on return, contains the scheme, or %NULL
 * @userinfo: (out) (allow-none): on return, contains the userinfo, or %NULL
 * @host: (out) (allow-none): on return, contains the host, or %NULL
 * @port: (out) (allow-none): on return, contains the port, or %NULL
 * @path: (out) (allow-none): on return, contains the path, or %NULL
 * @query: (out) (allow-none): on return, contains the query, or %NULL
 * @fragment: (out) (allow-none): on return, contains the fragment, or %NULL
 *
 * Parses @uri_string according to the generic grammar of RFC 3986,
 * and outputs the pieces into the provided variables. This is a
 * low-level method that does not do any pre- or post-processing of
 * @uri_string, does not decode percent-encoded characters, does not
 * translate between URIs and IRIs, etc. It just splits @uri_string
 * into pieces at the appropriate punctuation characters, and returns
 * the pieces.
 *
 * This method always succeeds, though if @uri_string is not a valid
 * URI, then the results may be surprising. (In particular, an invalid
 * absolute URI will likely be interpreted as being a valid relative
 * URI.)
 */
void
g_uri_split (const char  *uri_string,
	     char       **scheme,
	     char       **userinfo,
	     char       **host,
	     char       **port,
	     char       **path,
	     char       **query,
	     char       **fragment)
{
  const char *end, *hash, *colon, *at, *path, *question;
  const char *p, *hostend;
  int len;

  if (scheme)
    *scheme = NULL;
  if (userinfo)
    *userinfo = NULL;
  if (host)
    *host = NULL;
  if (port)
    *port = NULL;
  if (path)
    *path = NULL;
  if (query)
    *query = NULL;
  if (fragment)
    *fragment = NULL;

  /* Find fragment. */
  hash = strchr (uri_string, '#');
  if (hash)
    {
      if (fragment)
	*fragment = g_strdup (hash + 1);
      end = hash;
    }
  else
    end = uri_string + strlen (uri_string);

  /* Find scheme: initial [a-z+.-]* substring until ":" */
  p = uri_string;
  while (p < end && (g_ascii_isalnum (*p) ||
		     *p == '.' || *p == '+' || *p == '-'))
    p++;

  if (p > uri_string && *p == ':')
    {
      if (scheme)
	*scheme = g_ascii_strdown (uri_string, p - uri_string);
      p++;
    }
  else
    p = uri_string;

  /* Check for authority */
  if (strncmp (p, "//", 2) == 0)
    {
      p += 2;

      path = p + strcspn (p, "/?#");
      if (path > end)
	path = end;
      at = strchr (p, '@');
      if (at && at < path)
	{
	  if (userinfo)
	    *userinfo = g_strndup (p, colon - p);
	  p = at + 1;
	}

      /* Find host and port. */
      if (*p == '[')
	{
	  hostend = memchr (p, ']', path - p);
	  if (hostend)
	    hostend++;
	  else
	    hostend = path;
	  if (*hostend == ':')
	    colon = hostend;
	  else
	    colon = NULL;
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
	  if (port)
	    *port = g_strndup (colon + 1, path - (colon + 1));
	}

      p = path;
    }

  /* Find query */
  question = memchr (p, '?', end - p);
  if (question)
    {
      if (query)
	*query = g_strndup (question + 1, end - (question + 1));
      end = question;
    }

  if (end != p)
    {
      if (path)
	*path = g_strndup (p, end - p);
    }
}

static char *
uri_normalize (const char      *segment,
	       GUriParseFlags   flags,
	       GError         **error)
{
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
g_uri_parse_host (const char      *raw_host,
		  GUriParseFlags   flags,
		  GError         **error)
{
  char *decoded, *addr, *ace;

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
    flags |= G_URI_PARSE_ALLOW_SINGLE_PERCENT;
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

GUri *
g_uri_new (const char      *uri_string,
	   GUriParseFlags   flags,
	   GError         **error)
{
  GUri *guri;
  char *copy = NULL;

  if (!(flags & G_URI_PARSE_STRICT))
    {
      int len;

      /* Skip leading whitespace */
      while (g_ascii_isspace (*uri_string))
	uri_string++;

      /* Strip internal tabs and newlines */
      len = strcspn (uri_string, "\t\n\r");
      if (uri_string[len])
	{
	  const char *src;
	  char *dst;

	  copy = g_malloc (strlen (uri_string) + 1);
	  for (src = uri_string, dst = copy; *src; src++)
	    {
	      if (*src != '\t' && *src != '\n' && *src != '\r')
		*dst++ = *src;
	    }

	  /* Strip trailing whitespace */
	  while (dst > copy && g_ascii_isspace (dst[-1]))
	    dst--;

	  *dst = '\0';
	}
      else
	{
	  /* Strip trailing whitespace */
	  len = strlen (uri_string);
	  while (len > 0 && g_ascii_isspace (uri_string[len - 1]))
	    len--;
	  if (uri_string[len])
	    copy = g_strndup (uri_string, len);
	}

      if (copy)
	uri_string = copy;
    }

  guri = g_slice_new0 (GUri);
  g_uri_split (uri_string,
	       &guri->raw_scheme, &guri->raw_userinfo,
	       &guri->raw_host, &guri->raw_port,
	       &guri->raw_path, &guri->raw_query, &guri->raw_fragment);

  if (guri->raw_userinfo)
    {
      guri->userinfo = uri_normalize (guri->raw_userinfo, flags, error);
      if (!guri->userinfo)
	goto fail;
    }

  if (guri->raw_host)
    {
      guri->host = g_uri_parse_host (guri->raw_host, flags, error);
      if (!guri->host)
	goto fail;
    }

  if (guri->raw_port)
    {
      char *end;

      guri->port = strtoul (guri->raw_port, &end, 10);
      if (*end)
	{
	  g_set_error (error, G_URI_ERROR, G_URI_ERROR_PARSE,
		       _("Could not parse port '%s' in URI"),
		       guri->raw_port);
	  g_uri_free (guri);
	  if (copy)
	    g_free (copy);
	  return NULL;
	}
    }

  if (guri->raw_path)
    {
      guri->path = uri_normalize (guri->raw_path, flags, error);
      if (!guri->path)
	goto fail;
    }

  if (guri->raw_query)
    {
      guri->query = uri_normalize (guri->raw_query, flags, error);
      if (!guri->query)
	goto fail;
    }

  if (guri->raw_fragment)
    {
      guri->fragment = uri_normalize (guri->raw_fragment, flags, error);
      if (!guri->fragment)
	goto fail;
    }

  if (copy)
    g_free (copy);
  return guri;

 fail:
  if (copy)
    g_free (copy);
  g_uri_free (guri);
  return NULL;
}

GUri *
g_uri_new_relative (GUri            *base_uri,
		    const char      *uri_string,
		    GUriParseFlags   flags,
		    GError         **error)
{
  GUri *uri;

}

/**
 * soup_uri_new_with_base:
 * @base: a base URI
 * @uri_string: the URI
 *
 * Parses @uri_string relative to @base.
 *
 * Return value: a parsed #SoupURI.
 **/
SoupURI *
soup_uri_new_with_base (SoupURI *base, const char *uri_string)
{
  SoupURI *uri;
  const char *end, *hash, *colon, *at, *path, *question;
  const char *p, *hostend;
  gboolean remove_dot_segments = TRUE;
  int len;

  /* First some cleanup steps (which are supposed to all be no-ops,
   * but...). Skip initial whitespace, strip out internal tabs and
   * line breaks, and ignore trailing whitespace.
   */
  while (g_ascii_isspace (*uri_string))
    uri_string++;

  len = strcspn (uri_string, "\t\n\r");
  if (uri_string[len]) {
    char *clean = g_malloc (strlen (uri_string) + 1), *d;
    const char *s;

    for (s = uri_string, d = clean; *s; s++) {
      if (*s != '\t' && *s != '\n' && *s != '\r')
	*d++ = *s;
    }
    *d = '\0';

    uri = soup_uri_new_with_base (base, clean);
    g_free (clean);
    return uri;
  }
  end = uri_string + len;
  while (end > uri_string && g_ascii_isspace (end[-1]))
    end--;

  uri = g_slice_new0 (SoupURI);

  /* Find fragment. */
  hash = strchr (uri_string, '#');
  if (hash) {
    uri->fragment = uri_normalized_copy (hash + 1, end - hash + 1,
					 NULL, TRUE);
    end = hash;
  }

  /* Find scheme: initial [a-z+.-]* substring until ":" */
  p = uri_string;
  while (p < end && (g_ascii_isalnum (*p) ||
		     *p == '.' || *p == '+' || *p == '-'))
    p++;

  if (p > uri_string && *p == ':') {
    uri->scheme = soup_uri_parse_scheme (uri_string, p - uri_string);
    uri_string = p + 1;
  }

  if (uri_string == end && !base && !uri->fragment)
    return uri;

  /* Check for authority */
  if (strncmp (uri_string, "//", 2) == 0) {
    uri_string += 2;

    path = uri_string + strcspn (uri_string, "/?#");
    if (path > end)
      path = end;
    at = strchr (uri_string, '@');
    if (at && at < path) {
      colon = strchr (uri_string, ':');
      if (colon && colon < at) {
	uri->password = uri_decoded_copy (colon + 1,
					  at - colon - 1,
					  TRUE);
      } else {
	uri->password = NULL;
	colon = at;
      }

      uri->user = uri_decoded_copy (uri_string,
				    colon - uri_string,
				    TRUE);
      uri_string = at + 1;
    } else
      uri->user = uri->password = NULL;

    /* Find host and port. */
    if (*uri_string == '[') {
      uri_string++;
      hostend = strchr (uri_string, ']');
      if (!hostend || hostend > path) {
	soup_uri_free (uri);
	return NULL;
      }
      if (*(hostend + 1) == ':')
	colon = hostend + 1;
      else
	colon = NULL;
    } else {
      colon = memchr (uri_string, ':', path - uri_string);
      hostend = colon ? colon : path;
    }

    uri->host = uri_decoded_copy (uri_string, hostend - uri_string,
				  TRUE);

    if (colon && colon != path - 1) {
      char *portend;
      uri->port = strtoul (colon + 1, &portend, 10);
      if (portend != (char *)path) {
	soup_uri_free (uri);
	return NULL;
      }
    }

    uri_string = path;
  }

  /* Find query */
  question = memchr (uri_string, '?', end - uri_string);
  if (question) {
    uri->query = uri_normalized_copy (question + 1,
				      end - (question + 1),
				      NULL, TRUE);
    end = question;
  }

  if (end != uri_string) {
    uri->path = uri_normalized_copy (uri_string, end - uri_string,
				     NULL, TRUE);
  }

  /* Apply base URI. This is spelled out in RFC 3986. */
  if (base && !uri->scheme && uri->host)
    uri->scheme = base->scheme;
  else if (base && !uri->scheme) {
    uri->scheme = base->scheme;
    uri->user = g_strdup (base->user);
    uri->password = g_strdup (base->password);
    uri->host = g_strdup (base->host);
    uri->port = base->port;

    if (!uri->path) {
      uri->path = g_strdup (base->path);
      if (!uri->query)
	uri->query = g_strdup (base->query);
      remove_dot_segments = FALSE;
    } else if (*uri->path != '/') {
      char *newpath, *last;

      last = strrchr (base->path, '/');
      if (last) {
	newpath = g_strdup_printf ("%.*s/%s",
				   (int)(last - base->path),
				   base->path,
				   uri->path);
      } else
	newpath = g_strdup_printf ("/%s", uri->path);

      g_free (uri->path);
      uri->path = newpath;
    }
  }

  if (remove_dot_segments && uri->path && *uri->path) {
    char *p, *q;

    /* Remove "./" where "." is a complete segment. */
    for (p = uri->path + 1; *p; ) {
      if (*(p - 1) == '/' &&
	  *p == '.' && *(p + 1) == '/')
	memmove (p, p + 2, strlen (p + 2) + 1);
      else
	p++;
    }
    /* Remove "." at end. */
    if (p > uri->path + 2 &&
	*(p - 1) == '.' && *(p - 2) == '/')
      *(p - 1) = '\0';

    /* Remove "<segment>/../" where <segment> != ".." */
    for (p = uri->path + 1; *p; ) {
      if (!strncmp (p, "../", 3)) {
	p += 3;
	continue;
      }
      q = strchr (p + 1, '/');
      if (!q)
	break;
      if (strncmp (q, "/../", 4) != 0) {
	p = q + 1;
	continue;
      }
      memmove (p, q + 4, strlen (q + 4) + 1);
      p = uri->path + 1;
    }
    /* Remove "<segment>/.." at end where <segment> != ".." */
    q = strrchr (uri->path, '/');
    if (q && !strcmp (q, "/..")) {
      p = q - 1;
      while (p > uri->path && *p != '/')
	p--;
      if (strncmp (p, "/../", 4) != 0)
	*(p + 1) = 0;
    }

    /* Remove extraneous initial "/.."s */
    while (!strncmp (uri->path, "/../", 4))
      memmove (uri->path, uri->path + 3, strlen (uri->path) - 2);
    if (!strcmp (uri->path, "/.."))
      uri->path[1] = '\0';
  }

  /* HTTP-specific stuff */
  if (uri->scheme == SOUP_URI_SCHEME_HTTP ||
      uri->scheme == SOUP_URI_SCHEME_HTTPS) {
    if (!uri->path)
      uri->path = g_strdup ("/");
    if (!SOUP_URI_VALID_FOR_HTTP (uri)) {
      soup_uri_free (uri);
      return NULL;
    }
  }

  if (uri->scheme == SOUP_URI_SCHEME_FTP) {
    if (!uri->host) {
      soup_uri_free (uri);
      return NULL;
    }
  }

  if (!uri->port)
    uri->port = soup_scheme_default_port (uri->scheme);
  if (!uri->path)
    uri->path = g_strdup ("");

  return uri;
}

/**
 * soup_uri_new:
 * @uri_string: a URI
 *
 * Parses an absolute URI.
 *
 * You can also pass %NULL for @uri_string if you want to get back an
 * "empty" #SoupURI that you can fill in by hand. (You will need to
 * call at least soup_uri_set_scheme() and soup_uri_set_path(), since
 * those fields are required.)
 *
 * Return value: a #SoupURI, or %NULL.
 **/
SoupURI *
soup_uri_new (const char *uri_string)
{
  SoupURI *uri;

  if (!uri_string)
    return g_slice_new0 (SoupURI);

  uri = soup_uri_new_with_base (NULL, uri_string);
  if (!uri)
    return NULL;
  if (!uri->scheme) {
    soup_uri_free (uri);
    return NULL;
  }

  return uri;
}


/**
 * g_uri_to_string:
 * @uri: a #GUri
 * @flags: flags describing how to convert @uri
 *
 * Returns a string representing @uri.
 *
 * If @just_path_and_query is %TRUE, this concatenates the path and query
 * together. That is, it constructs the string that would be needed in
 * the Request-Line of an HTTP request for @uri.
 *
 * Return value: a string representing @uri, which the caller must free.
 **/
char *
soup_uri_to_string (SoupURI *uri, gboolean just_path_and_query)
{
  GString *str;
  char *return_result;

  g_return_val_if_fail (uri != NULL, NULL);

  /* IF YOU CHANGE ANYTHING IN THIS FUNCTION, RUN
   * tests/uri-parsing AFTERWARD.
   */

  str = g_string_sized_new (20);

  if (uri->scheme && !just_path_and_query)
    g_string_append_printf (str, "%s:", uri->scheme);
  if (uri->host && !just_path_and_query) {
    g_string_append (str, "//");
    if (uri->user) {
      append_uri_encoded (str, uri->user, ":;@?/");
      g_string_append_c (str, '@');
    }
    if (strchr (uri->host, ':')) {
      g_string_append_c (str, '[');
      g_string_append (str, uri->host);
      g_string_append_c (str, ']');
    } else
      append_uri_encoded (str, uri->host, ":/");
    if (uri->port && uri->port != soup_scheme_default_port (uri->scheme))
      g_string_append_printf (str, ":%d", uri->port);
    if (!uri->path && (uri->query || uri->fragment))
      g_string_append_c (str, '/');
  }

  if (uri->path && *uri->path)
    g_string_append (str, uri->path);

  if (uri->query) {
    g_string_append_c (str, '?');
    g_string_append (str, uri->query);
  }
  if (uri->fragment && !just_path_and_query) {
    g_string_append_c (str, '#');
    g_string_append (str, uri->fragment);
  }

  return_result = str->str;
  g_string_free (str, FALSE);

  return return_result;
}

/**
 * soup_uri_copy:
 * @uri: a #SoupURI
 *
 * Copies @uri
 *
 * Return value: a copy of @uri, which must be freed with soup_uri_free()
 **/
SoupURI *
soup_uri_copy (SoupURI *uri)
{
  SoupURI *dup;

  g_return_val_if_fail (uri != NULL, NULL);

  dup = g_slice_new0 (SoupURI);
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

static inline gboolean
parts_equal (const char *one, const char *two, gboolean insensitive)
{
  if (!one && !two)
    return TRUE;
  if (!one || !two)
    return FALSE;
  return insensitive ? !g_ascii_strcasecmp (one, two) : !strcmp (one, two);
}

/**
 * soup_uri_equal:
 * @uri1: a #SoupURI
 * @uri2: another #SoupURI
 *
 * Tests whether or not @uri1 and @uri2 are equal in all parts
 *
 * Return value: %TRUE or %FALSE
 **/
gboolean 
soup_uri_equal (SoupURI *uri1, SoupURI *uri2)
{
  if (uri1->scheme != uri2->scheme                         ||
      uri1->port   != uri2->port                           ||
      !parts_equal (uri1->user, uri2->user, FALSE)         ||
      !parts_equal (uri1->password, uri2->password, FALSE) ||
      !parts_equal (uri1->host, uri2->host, TRUE)          ||
      !parts_equal (uri1->path, uri2->path, FALSE)         ||
      !parts_equal (uri1->query, uri2->query, FALSE)       ||
      !parts_equal (uri1->fragment, uri2->fragment, FALSE))
    return FALSE;

  return TRUE;
}

/**
 * soup_uri_free:
 * @uri: a #SoupURI
 *
 * Frees @uri.
 **/
void
soup_uri_free (SoupURI *uri)
{
  g_return_if_fail (uri != NULL);

  g_free (uri->user);
  g_free (uri->password);
  g_free (uri->host);
  g_free (uri->path);
  g_free (uri->query);
  g_free (uri->fragment);

  g_slice_free (SoupURI, uri);
}

static void
append_uri_encoded (GString *str, const char *in, const char *extra_enc_chars)
{
  const unsigned char *s = (const unsigned char *)in;

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
 * soup_uri_encode:
 * @part: a URI part
 * @escape_extra: additional reserved characters to escape (or %NULL)
 *
 * This %<!-- -->-encodes the given URI part and returns the escaped
 * version in allocated memory, which the caller must free when it is
 * done.
 *
 * Return value: the encoded URI part
 **/
char *
soup_uri_encode (const char *part, const char *escape_extra)
{
  GString *str;
  char *encoded;

  str = g_string_new (NULL);
  append_uri_encoded (str, part, escape_extra);
  encoded = str->str;
  g_string_free (str, FALSE);

  return encoded;
}

#define XDIGIT(c) ((c) <= '9' ? (c) - '0' : ((c) & 0x4F) - 'A' + 10)
#define HEXCHAR(s) ((XDIGIT (s[1]) << 4) + XDIGIT (s[2]))

static char *
uri_decoded_copy (const char *part, int length, gboolean fixup)
{
  unsigned char *s, *d;
  char *decoded = g_strndup (part, length);

  s = d = (unsigned char *)decoded;
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
 * soup_uri_decode:
 * @part: a URI part
 *
 * Fully %<!-- -->-decodes @part.
 *
 * Return value: the decoded URI part, or %NULL if an invalid percent
 * code was encountered.
 */
char *
soup_uri_decode (const char *part)
{
  return uri_decoded_copy (part, strlen (part), FALSE);
}

static char *
uri_normalized_copy (const char *part, int length,
		     const char *unescape_extra, gboolean fixup)
{
  unsigned char *s, *d, c;
  char *normalized = g_strndup (part, length);
  gboolean need_fixup = FALSE;

  s = d = (unsigned char *)normalized;
  do {
    if (*s == '%') {
      if (!g_ascii_isxdigit (s[1]) ||
	  !g_ascii_isxdigit (s[2])) {
	if (!fixup) {
	  g_free (normalized);
	  return NULL;
	}
	*d++ = *s;
	continue;
      }

      c = HEXCHAR (s);
      if (soup_char_is_uri_unreserved (c) ||
	  (unescape_extra && strchr (unescape_extra, c))) {
	*d++ = c;
	s += 2;
      } else {
	*d++ = *s++;
	*d++ = g_ascii_toupper (*s++);
	*d++ = g_ascii_toupper (*s);
      }
    } else {
      if (*s == ' ')
	need_fixup = TRUE;
      *d++ = *s;
    }
  } while (*s++);

  if (fixup && need_fixup) {
    char *tmp, *sp;
    /* This code is lame, but so are people who put
     * unencoded spaces in URLs!
     */
    while ((sp = strchr (normalized, ' '))) {
      tmp = g_strdup_printf ("%.*s%%20%s",
			     (int)(sp - normalized),
			     normalized, sp + 1);
      g_free (normalized);
      normalized = tmp;
    };
  }

  return normalized;
}

/**
 * soup_uri_normalize:
 * @part: a URI part
 * @unescape_extra: reserved characters to unescape (or %NULL)
 *
 * %<!-- -->-decodes any "unreserved" characters (or characters in
 * @unescape_extra) in @part.
 *
 * "Unreserved" characters are those that are not allowed to be used
 * for punctuation according to the URI spec. For example, letters are
 * unreserved, so soup_uri_normalize() will turn
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
soup_uri_normalize (const char *part, const char *unescape_extra)
{
  return uri_normalized_copy (part, strlen (part), unescape_extra, FALSE);
}


/**
 * soup_uri_uses_default_port:
 * @uri: a #SoupURI
 *
 * Tests if @uri uses the default port for its scheme. (Eg, 80 for
 * http.) (This only works for http and https; libsoup does not know
 * the default ports of other protocols.)
 *
 * Return value: %TRUE or %FALSE
 **/
gboolean
soup_uri_uses_default_port (SoupURI *uri)
{
  g_return_val_if_fail (uri->scheme == SOUP_URI_SCHEME_HTTP ||
			uri->scheme == SOUP_URI_SCHEME_HTTPS ||
			uri->scheme == SOUP_URI_SCHEME_FTP, FALSE);

  return uri->port == soup_scheme_default_port (uri->scheme);
}

/**
 * SOUP_URI_SCHEME_HTTP:
 *
 * "http" as an interned string. This can be compared directly against
 * the value of a #SoupURI's <structfield>scheme</structfield>
 **/

/**
 * SOUP_URI_SCHEME_HTTPS:
 *
 * "https" as an interned string. This can be compared directly
 * against the value of a #SoupURI's <structfield>scheme</structfield>
 **/

/**
 * soup_uri_get_scheme:
 * @uri: a #SoupURI
 *
 * Gets @uri's scheme.
 *
 * Return value: @uri's scheme.
 *
 * Since: 2.32
 **/
const char *
soup_uri_get_scheme (SoupURI *uri)
{
  return uri->scheme;
}

/**
 * soup_uri_set_scheme:
 * @uri: a #SoupURI
 * @scheme: the URI scheme
 *
 * Sets @uri's scheme to @scheme. This will also set @uri's port to
 * the default port for @scheme, if known.
 **/
void
soup_uri_set_scheme (SoupURI *uri, const char *scheme)
{
  uri->scheme = soup_uri_parse_scheme (scheme, strlen (scheme));
  uri->port = soup_scheme_default_port (uri->scheme);
}

/**
 * soup_uri_get_user:
 * @uri: a #SoupURI
 *
 * Gets @uri's user.
 *
 * Return value: @uri's user.
 *
 * Since: 2.32
 **/
const char *
soup_uri_get_user (SoupURI *uri)
{
  return uri->user;
}

/**
 * soup_uri_set_user:
 * @uri: a #SoupURI
 * @user: the username, or %NULL
 *
 * Sets @uri's user to @user.
 **/
void
soup_uri_set_user (SoupURI *uri, const char *user)
{
  g_free (uri->user);
  uri->user = g_strdup (user);
}

/**
 * soup_uri_get_password:
 * @uri: a #SoupURI
 *
 * Gets @uri's password.
 *
 * Return value: @uri's password.
 *
 * Since: 2.32
 **/
const char *
soup_uri_get_password (SoupURI *uri)
{
  return uri->password;
}

/**
 * soup_uri_set_password:
 * @uri: a #SoupURI
 * @password: the password, or %NULL
 *
 * Sets @uri's password to @password.
 **/
void
soup_uri_set_password (SoupURI *uri, const char *password)
{
  g_free (uri->password);
  uri->password = g_strdup (password);
}

/**
 * soup_uri_get_host:
 * @uri: a #SoupURI
 *
 * Gets @uri's host.
 *
 * Return value: @uri's host.
 *
 * Since: 2.32
 **/
const char *
soup_uri_get_host (SoupURI *uri)
{
  return uri->host;
}

/**
 * soup_uri_set_host:
 * @uri: a #SoupURI
 * @host: the hostname or IP address, or %NULL
 *
 * Sets @uri's host to @host.
 *
 * If @host is an IPv6 IP address, it should not include the brackets
 * required by the URI syntax; they will be added automatically when
 * converting @uri to a string.
 **/
void
soup_uri_set_host (SoupURI *uri, const char *host)
{
  g_free (uri->host);
  uri->host = g_strdup (host);
}

/**
 * soup_uri_get_port:
 * @uri: a #SoupURI
 *
 * Gets @uri's port.
 *
 * Return value: @uri's port.
 *
 * Since: 2.32
 **/
guint
soup_uri_get_port (SoupURI *uri)
{
  return uri->port;
}

/**
 * soup_uri_set_port:
 * @uri: a #SoupURI
 * @port: the port, or 0
 *
 * Sets @uri's port to @port. If @port is 0, @uri will not have an
 * explicitly-specified port.
 **/
void
soup_uri_set_port (SoupURI *uri, guint port)
{
  uri->port = port;
}

/**
 * soup_uri_get_path:
 * @uri: a #SoupURI
 *
 * Gets @uri's path.
 *
 * Return value: @uri's path.
 *
 * Since: 2.32
 **/
const char *
soup_uri_get_path (SoupURI *uri)
{
  return uri->path;
}

/**
 * soup_uri_set_path:
 * @uri: a #SoupURI
 * @path: the path
 *
 * Sets @uri's path to @path.
 **/
void
soup_uri_set_path (SoupURI *uri, const char *path)
{
  g_free (uri->path);
  uri->path = g_strdup (path);
}

/**
 * soup_uri_get_query:
 * @uri: a #SoupURI
 *
 * Gets @uri's query.
 *
 * Return value: @uri's query.
 *
 * Since: 2.32
 **/
const char *
soup_uri_get_query (SoupURI *uri)
{
  return uri->query;
}

/**
 * soup_uri_set_query:
 * @uri: a #SoupURI
 * @query: the query
 *
 * Sets @uri's query to @query.
 **/
void
soup_uri_set_query (SoupURI *uri, const char *query)
{
  g_free (uri->query);
  uri->query = g_strdup (query);
}

/**
 * soup_uri_set_query_from_form:
 * @uri: a #SoupURI
 * @form: a #GHashTable containing HTML form information
 *
 * Sets @uri's query to the result of encoding @form according to the
 * HTML form rules. See soup_form_encode_hash() for more information.
 **/
void
soup_uri_set_query_from_form (SoupURI *uri, GHashTable *form)
{
  g_free (uri->query);
  uri->query = soup_form_encode_urlencoded (form);
}

/**
 * soup_uri_set_query_from_fields:
 * @uri: a #SoupURI
 * @first_field: name of the first form field to encode into query
 * @...: value of @first_field, followed by additional field names
 * and values, terminated by %NULL.
 *
 * Sets @uri's query to the result of encoding the given form fields
 * and values according to the * HTML form rules. See
 * soup_form_encode() for more information.
 **/
void
soup_uri_set_query_from_fields (SoupURI    *uri,
				const char *first_field,
				...)
{
  va_list args;

  g_free (uri->query);
  va_start (args, first_field);
  uri->query = soup_form_encode_valist (first_field, args);
  va_end (args);
}

/**
 * soup_uri_get_fragment:
 * @uri: a #SoupURI
 *
 * Gets @uri's fragment.
 *
 * Return value: @uri's fragment.
 *
 * Since: 2.32
 **/
const char *
soup_uri_get_fragment (SoupURI *uri)
{
  return uri->fragment;
}

/**
 * soup_uri_set_fragment:
 * @uri: a #SoupURI
 * @fragment: the fragment
 *
 * Sets @uri's fragment to @fragment.
 **/
void
soup_uri_set_fragment (SoupURI *uri, const char *fragment)
{
  g_free (uri->fragment);
  uri->fragment = g_strdup (fragment);
}

/**
 * soup_uri_copy_host:
 * @uri: a #SoupUri
 *
 * Makes a copy of @uri, considering only the protocol, host, and port
 *
 * Return value: the new #SoupUri
 *
 * Since: 2.26.3
 **/
SoupURI *
soup_uri_copy_host (SoupURI *uri)
{
  SoupURI *dup;

  g_return_val_if_fail (uri != NULL, NULL);

  dup = soup_uri_new (NULL);
  dup->scheme = uri->scheme;
  dup->host   = g_strdup (uri->host);
  dup->port   = uri->port;
  if (dup->scheme == SOUP_URI_SCHEME_HTTP ||
      dup->scheme == SOUP_URI_SCHEME_HTTPS)
    dup->path = g_strdup ("");

  return dup;
}

/**
 * soup_uri_host_hash:
 * @key: a #SoupURI
 *
 * Hashes @key, considering only the scheme, host, and port.
 *
 * Return value: a hash
 *
 * Since: 2.26.3
 **/
guint
soup_uri_host_hash (gconstpointer key)
{
  const SoupURI *uri = key;

  g_return_val_if_fail (uri != NULL && uri->host != NULL, 0);

  return GPOINTER_TO_UINT (uri->scheme) + uri->port +
    soup_str_case_hash (uri->host);
}

/**
 * soup_uri_host_equal:
 * @v1: a #SoupURI
 * @v2: a #SoupURI
 *
 * Compares @v1 and @v2, considering only the scheme, host, and port.
 *
 * Return value: whether or not the URIs are equal in scheme, host,
 * and port.
 *
 * Since: 2.26.3
 **/
gboolean
soup_uri_host_equal (gconstpointer v1, gconstpointer v2)
{
  const SoupURI *one = v1;
  const SoupURI *two = v2;

  g_return_val_if_fail (one != NULL && two != NULL, one == two);
  g_return_val_if_fail (one->host != NULL && two->host != NULL, one->host == two->host);

  if (one->scheme != two->scheme)
    return FALSE;
  if (one->port != two->port)
    return FALSE;

  return g_ascii_strcasecmp (one->host, two->host) == 0;
}


GType
soup_uri_get_type (void)
{
  static volatile gsize type_volatile = 0;

  if (g_once_init_enter (&type_volatile)) {
    GType type = g_boxed_type_register_static (
					       g_intern_static_string ("SoupURI"),
					       (GBoxedCopyFunc) soup_uri_copy,
					       (GBoxedFreeFunc) soup_uri_free);
    g_once_init_leave (&type_volatile, type);
  }
  return type_volatile;
}
