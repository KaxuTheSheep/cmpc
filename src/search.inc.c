/* ====================================================================
 * Selection helpers and search
 * ==================================================================== */
static void move_sel(int dir)
{
	int j = sel + dir;
	if (j < 0 || j >= nq)
		return;
	if (sel == g.pos || j == g.pos || (Q[sel].prio > 0) != (Q[j].prio > 0)) {
		msg("can't move across levels or past the playing song");
		return;
	}
	if (!mpd_run_move(conn, (unsigned)sel, (unsigned)j)) {
		check();
		return;
	}
	sel = j;
	refresh_all();
}

static void toggle_pause(void)
{
	bool ok = g.state == MPD_STATE_STOP ? mpd_run_play(conn) : mpd_run_toggle_pause(conn);
	if (!ok)
		check();
}

static bool ci_contains(const char *hay, const char *needle)
{
	if (!*needle)
		return true;
	size_t nl = strlen(needle);
	for (const char *h = hay; *h; h++)
		if (!strncasecmp(h, needle, nl))
			return true;
	return false;
}

static void search_next(int from, int dir)
{
	if (!search_len || !nq) {
		msg("no active search");
		return;
	}
	for (int step = 1; step <= nq; step++) {
		int i = ((from + dir * step) % nq + nq) % nq;
		if (Q[i].prio > 0)
			continue;
		char hay[900];
		char art[256];
		pretty_artist(art, sizeof art, Q[i].artist);
		snprintf(hay, sizeof hay, "%s %s %s %s", song_title(&Q[i]), art, Q[i].album, Q[i].uri);
		if (ci_contains(hay, search_q)) {
			sel = i;
			return;
		}
	}
	msg("no match for \"%s\"", search_q);
}

/* ====================================================================
 * Search form: matching. See the comment above SFIELD in mpd.inc.c for
 * the semantics. Everything here works on DB[] and never talks to MPD.
 * ==================================================================== */

/* Case-insensitive (ASCII) / exact-case substring. */
static bool contains_cs(const char *hay, const char *needle, bool cs)
{
	return cs ? strstr(hay, needle) != NULL : ci_contains(hay, needle);
}

static bool fuzzy_contains(const char *hay, const char *needle, bool cs)
{
	size_t m = strlen(needle);
	if (m == 0)
		return true;
	int k = (m >= 3) + (m >= 6) + (m >= 10);
	if (k == 0)
		return contains_cs(hay, needle, cs);
	if (m > 95)
		m = 95;
	int col[96 + 1];
	for (size_t j = 0; j <= m; j++)
		col[j] = (int)j;
	for (const unsigned char *h = (const unsigned char *)hay; *h; h++) {
		int c = cs ? *h : tolower(*h);
		int diag = col[0];
		col[0] = 0;
		for (size_t j = 1; j <= m; j++) {
			int nc = cs ? (unsigned char)needle[j - 1] : tolower((unsigned char)needle[j - 1]);
			int up = col[j];
			int best = diag + (nc != c);
			if (col[j] + 1 < best) best = col[j] + 1;
			if (col[j - 1] + 1 < best) best = col[j - 1] + 1;
			col[j] = best;
			diag = up;
		}
		if (col[m] <= k)
			return true;
	}
	return false;
}

struct txt { const char *needle; bool cs, fuzzy, exact, split; };

static bool streq_cs(const char *a, const char *b, bool cs)
{
	return cs ? !strcmp(a, b) : !strcasecmp(a, b);
}

/* one tag/field value against one text alternative */
static bool val_match(const struct txt *t, const char *v)
{
	if (t->exact) {
		if (streq_cs(v, t->needle, t->cs))
			return true;
		if (!t->split)
			return false;
		/* artist-like: the value is "A//B//C" - match any one name */
		size_t sl = strlen(ARTIST_SEP);
		const char *cur = v;
		while (sl) {
			const char *sep = strstr(cur, ARTIST_SEP);
			size_t len = sep ? (size_t)(sep - cur) : strlen(cur);
			char one[256];
			if (len >= sizeof one) len = sizeof one - 1;
			memcpy(one, cur, len);
			one[len] = 0;
			if (streq_cs(one, t->needle, t->cs))
				return true;
			if (!sep)
				break;
			cur = sep + sl;
		}
		return false;
	}
	return t->fuzzy ? fuzzy_contains(v, t->needle, t->cs) : contains_cs(v, t->needle, t->cs);
}

/* any value of MPD tag `field` (all of them: a song can list several
 * artists, genres...) matches */
static bool tag_vals_match(const struct dbsong *sg, const char *field, const struct txt *t)
{
	if (!sg->tags)
		return false;
	size_t fl = strlen(field);
	for (const char *p = sg->tags; *p;) {
		const char *tab = strchr(p, '\t');
		const char *nl = tab ? strchr(tab, '\n') : NULL;
		if (!tab || !nl)
			break;
		if ((size_t)(tab - p) == fl && !strncasecmp(p, field, fl)) {
			char v[512];
			size_t len = (size_t)(nl - tab - 1);
			if (len >= sizeof v) len = sizeof v - 1;
			memcpy(v, tab + 1, len);
			v[len] = 0;
			if (val_match(t, v))
				return true;
		}
		p = nl + 1;
	}
	return false;
}

/* ---- numbers: Year, Kbps, Length ---- */
enum { NO_LT, NO_LE, NO_GT, NO_GE, NO_EQ, NO_RANGE };
struct numalt { int op; double a, b; };

/* "320", "12.5", or "3:30" (= 210) */
static bool parse_num(const char *s, double *out)
{
	while (*s == ' ') s++;
	char *e;
	double v = strtod(s, &e);
	if (e == s)
		return false;
	if (*e == ':') {
		char *e2;
		double sec = strtod(e + 1, &e2);
		if (e2 == e + 1)
			return false;
		v = v * 60 + sec;
		e = e2;
	}
	while (*e == ' ') e++;
	*out = v;
	return *e == 0;
}

static bool parse_numalt(const char *s, struct numalt *n)
{
	while (*s == ' ') s++;
	const char *v = s;
	if (!strncmp(s, "<=", 2)) { n->op = NO_LE; v = s + 2; }
	else if (!strncmp(s, ">=", 2)) { n->op = NO_GE; v = s + 2; }
	else if (*s == '<') { n->op = NO_LT; v = s + 1; }
	else if (*s == '>') { n->op = NO_GT; v = s + 1; }
	else if (*s == '=') { n->op = NO_EQ; v = s + 1; }
	else {
		const char *dash = strchr(s + 1, '-');
		if (dash) {
			char lo[48];
			size_t len = (size_t)(dash - s);
			if (len >= sizeof lo) return false;
			memcpy(lo, s, len);
			lo[len] = 0;
			n->op = NO_RANGE;
			return parse_num(lo, &n->a) && parse_num(dash + 1, &n->b) && n->a <= n->b;
		}
		n->op = NO_EQ;
	}
	return parse_num(v, &n->a);
}

static bool num_match(const struct numalt *n, double v)
{
	switch (n->op) {
	case NO_LT: return v < n->a;
	case NO_LE: return v <= n->a;
	case NO_GT: return v > n->a;
	case NO_GE: return v >= n->a;
	case NO_RANGE: return v >= n->a && v <= n->b;
	default: return v - n->a < 0.5 && n->a - v < 0.5;
	}
}

static int *dbkbps;
static int ndbkbps;

static int song_kbps(int i)
{
	if (ndbkbps != ndb) {
		free(dbkbps);
		dbkbps = malloc((size_t)(ndb ? ndb : 1) * sizeof *dbkbps);
		if (!dbkbps) abort();
		ndbkbps = ndb;
		for (int k = 0; k < ndb; k++)
			dbkbps[k] = -2;
	}
	if (dbkbps[i] != -2)
		return dbkbps[i];
	int v = -1;
	char path[1400];
	struct stat st;
	snprintf(path, sizeof path, "%s/%s", MUSIC_DIR, DB[i].uri);
	if (DB[i].dur > 0 && stat(path, &st) == 0 && S_ISREG(st.st_mode))
		v = (int)((double)st.st_size * 8.0 / ((double)DB[i].dur * 1000.0) + 0.5);
	dbkbps[i] = v;
	return v;
}

static int song_year(const struct dbsong *sg)
{
	if (!sg->tags)
		return -1;
	for (const char *p = sg->tags; *p;) {
		const char *tab = strchr(p, '\t');
		const char *nl = tab ? strchr(tab, '\n') : NULL;
		if (!tab || !nl)
			break;
		if ((size_t)(tab - p) == 4 && !strncasecmp(p, "Date", 4) && isdigit((unsigned char)tab[1]))
			return atoi(tab + 1);
		p = nl + 1;
	}
	return -1;
}

static void song_ext(const struct dbsong *sg, char *out, size_t n)
{
	const char *slash = strrchr(sg->uri, '/');
	const char *dot = strrchr(slash ? slash : sg->uri, '.');
	size_t i = 0;
	if (dot)
		for (dot++; *dot && i + 1 < n; dot++)
			out[i++] = (char)tolower((unsigned char)*dot);
	out[i] = 0;
}

/* ---- a prepared row: the text split on | into alternatives ---- */
#define MAX_ALTS 8
struct prow {
	int f, join;
	int nalt;
	char alt[MAX_ALTS][96];
	struct numalt num[MAX_ALTS];
};

/* true = ok. On error a message is shown and the search isn't run. */
static bool prep_row(int f, struct prow *pr)
{
	const struct srow *r = &SR[f];
	pr->f = f;
	pr->join = r->join;
	pr->nalt = 0;
	char buf[sizeof r->text];
	memcpy(buf, r->text, sizeof buf);
	buf[sizeof buf - 1] = 0;
	char *save;
	for (char *tok = strtok_r(buf, "|", &save); tok && pr->nalt < MAX_ALTS; tok = strtok_r(NULL, "|", &save)) {
		while (*tok == ' ') tok++;
		size_t len = strlen(tok);
		while (len && tok[len - 1] == ' ') tok[--len] = 0;
		if (!len)
			continue;
		if (SFIELD[f].kind == SK_NUM && !parse_numalt(tok, &pr->num[pr->nalt])) {
			msg("%s: can't read \"%s\" (try <320, 256-320, >=2010)", SFIELD[f].label, tok);
			return false;
		}
		if (SFIELD[f].kind == SK_EXT && *tok == '.')
			tok++;
		snprintf(pr->alt[pr->nalt++], sizeof pr->alt[0], "%s", tok);
	}
	return true;
}

static bool row_match(int i, const struct prow *pr, int *unknown_kbps)
{
	const struct dbsong *sg = &DB[i];
	const struct srow *r = &SR[pr->f];
	switch (SFIELD[pr->f].kind) {
	case SK_NUM: {
		double v;
		if (pr->f == SF_KBPS) {
			int k = song_kbps(i);
			if (k < 0) { (*unknown_kbps)++; return false; }
			v = k;
		} else if (pr->f == SF_YEAR) {
			int y = song_year(sg);
			if (y < 0) return false;
			v = y;
		} else {
			v = sg->dur;
		}
		for (int a = 0; a < pr->nalt; a++)
			if (num_match(&pr->num[a], v))
				return true;
		return false;
	}
	case SK_EXT: {
		char ext[24];
		song_ext(sg, ext, sizeof ext);
		for (int a = 0; a < pr->nalt; a++)
			if (!strcasecmp(ext, pr->alt[a]))
				return true;
		return false;
	}
	default:
		break;
	}
	for (int a = 0; a < pr->nalt; a++) {
		struct txt t = {pr->alt[a], r->cs, r->fuzzy, r->exact,
				pr->f == SF_ARTIST || pr->f == SF_ALBUMARTIST};
		switch (pr->f) {
		case SF_ANY:
			if (val_match(&t, dbsong_title(sg)) || val_match(&t, sg->album) ||
			    val_match(&t, sg->uri) || tag_vals_match(sg, "Artist", &t) ||
			    val_match(&t, sg->artist))
				return true;
			break;
		case SF_TITLE: if (val_match(&t, dbsong_title(sg))) return true; break;
		case SF_ARTIST: if (tag_vals_match(sg, "Artist", &t) || val_match(&t, sg->artist)) return true; break;
		case SF_ALBUMARTIST: if (tag_vals_match(sg, "AlbumArtist", &t)) return true; break;
		case SF_ALBUM: if (val_match(&t, sg->album)) return true; break;
		case SF_GENRE: if (tag_vals_match(sg, "Genre", &t)) return true; break;
		case SF_PATH: if (val_match(&t, sg->uri)) return true; break;
		}
	}
	return false;
}

/* every AND row, AND at least one OR row (if any), AND no NOT row.
 * Kbps is evaluated last: it needs a stat() per file, so songs the cheap
 * rows already rule out never pay for it. */
static bool song_matches(int i, const struct prow *rows, int n, int *unknown_kbps)
{
	bool and_ok = true, any_has = false, any_ok = false, excl = false;
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 1 && (!and_ok || excl))
			break;
		for (int r = 0; r < n; r++) {
			if ((rows[r].f == SF_KBPS) != (pass == 1))
				continue;
			bool m = row_match(i, &rows[r], unknown_kbps);
			if (rows[r].join == J_AND) and_ok = and_ok && m;
			else if (rows[r].join == J_OR) { any_has = true; any_ok = any_ok || m; }
			else excl = excl || m;
		}
	}
	return and_ok && (!any_has || any_ok) && !excl;
}

static void run_search(void)
{
	if (!db_loaded)
		load_db();
	struct prow rows[SF_COUNT];
	int n = 0;
	for (int f = 0; f < SF_COUNT; f++) {
		if (!SR[f].len)
			continue;            /* empty = * */
		if (!prep_row(f, &rows[n]))
			return;
		if (rows[n].nalt)
			n++;
	}
	free(sresults);
	sresults = NULL;
	nsresults = 0;
	ssel = stop = 0;
	int cap = 0, unknown = 0;
	for (int i = 0; i < ndb; i++) {
		if (!song_matches(i, rows, n, &unknown))
			continue;
		if (nsresults == cap) {
			cap = cap ? cap * 2 : 64;
			sresults = realloc(sresults, (size_t)cap * sizeof *sresults);
			if (!sresults) abort();
		}
		sresults[nsresults++] = i;
	}
	s_ran = true;
	s_results_focus = nsresults > 0;
	if (unknown && SR[SF_KBPS].len)
		msg("%d song%s; kbps unknown for %d (is MUSIC_DIR right?)", nsresults, nsresults == 1 ? "" : "s", unknown);
	else
		msg(nsresults ? "%d song%s" : "no matches", nsresults, nsresults == 1 ? "" : "s");
}

static void free_search(void)
{
	free(sresults);
	sresults = NULL;
	nsresults = 0;
	free(dbkbps);
	dbkbps = NULL;
	ndbkbps = 0;
}

/* Same, over the Directory pane's flat library list. */
static void dsearch_next(int from, int dir)
{
	if (!search_len || !ndb) {
		msg("no active search");
		return;
	}
	for (int step = 1; step <= ndb; step++) {
		int i = ((from + dir * step) % ndb + ndb) % ndb;
		char hay[900];
		char art[256];
		pretty_artist(art, sizeof art, DB[i].artist);
		snprintf(hay, sizeof hay, "%s %s %s %s", dbsong_title(&DB[i]), art, DB[i].album, DB[i].uri);
		if (ci_contains(hay, search_q)) {
			dsel = i;
			return;
		}
	}
	msg("no match for \"%s\"", search_q);
}

