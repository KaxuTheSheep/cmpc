/* ====================================================================
 * Formatting helpers
 * ==================================================================== */
static void fmt_time(char *b, size_t n, unsigned s)
{
	if (s >= 3600)
		snprintf(b, n, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
	else
		snprintf(b, n, "%u:%02u", s / 60, s % 60);
}

static void pretty_artist(char *dst, size_t n, const char *s)
{
	size_t o = 0, sl = strlen(ARTIST_SEP), jl = strlen(ARTIST_JOIN);
	while (*s && o + jl + 2 < n) {
		if (sl && !strncmp(s, ARTIST_SEP, sl)) {
			memcpy(dst + o, ARTIST_JOIN, jl);
			o += jl;
			s += sl;
		} else {
			dst[o++] = *s++;
		}
	}
	dst[o] = 0;
}

static const char *ext_of(const char *uri)
{
	const char *e = strrchr(uri, '.');
	return e && !strchr(e, '/') ? e + 1 : "";
}

static bool is_lossless(const char *uri)
{
	static const char *const exts[] = LOSSLESS_EXTS;
	const char *e = ext_of(uri);
	for (int i = 0; exts[i]; i++)
		if (!strcasecmp(e, exts[i]))
			return true;
	return false;
}

static const char *title_of(const char *title, const char *uri)
{
	if (title[0])
		return title;
	const char *b = strrchr(uri, '/');
	return b ? b + 1 : uri;
}

static const char *song_title(const struct song *s) { return title_of(s->title, s->uri); }
static const char *dbsong_title(const struct dbsong *s) { return title_of(s->title, s->uri); }

