/* TODO REPLACE MPD WITH A SMALLER DAEMON WHICH ONLY STAYS ALIVE WHILE MUSIC PLAYS, ESLSE IT DOESNT RUN*/


/* ====================================================================
 * MPD state
 * ==================================================================== */
struct song {
	unsigned id, prio, dur;
	char *uri, *title, *artist, *album, *track;
};

static struct mpd_connection *conn;
static struct song *Q;
static int nq, capq;
static int sel, top_row;
static bool search_mode;
static char search_q[64];
static int search_len;
static bool shuffle_loop = START_SHUFFLE_LOOP;
static long last_connect_try;

/* ---- panes ----------------------------------------------------------
 * 1-6 switch the right-hand pane. Queue is fully live; Directory loads
 * the whole library flat on first visit. Playlists/Artists/Albums/
 * Search are placeholders until built. */
enum pane { PANE_QUEUE, PANE_DIR, PANE_PLAYLIST, PANE_ARTIST, PANE_ALBUM, PANE_SEARCH, PANE_COUNT };
static const char *const PANE_NAMES[PANE_COUNT] = {
	"queue", "directory", "playlists", "artists", "albums", "search"
};
static int current_pane = PANE_QUEUE;

struct dbsong { char *uri, *title, *artist, *album, *track; char *tags; unsigned dur; };
static struct dbsong *DB;
static int ndb, capdb;
static bool db_loaded;
static int dsel, dtop;

/* ---- Artists pane: artist -> album -> song drill-down ---------------- */
enum { ALVL_ARTIST = 0, ALVL_ALBUM, ALVL_SONG };
static int artist_level = ALVL_ARTIST;
static char **artist_list;
static int nartists;
static bool artists_built;
static int asel, atop;

static char **album_list;
static int nalbums;
static int alsel, altop;
static char cur_artist[200];

static int *song_idx; /* indices into DB, for the current artist+album */
static int nsongs;
static int assel, astop;
static char cur_album[300];

/* ---- Albums pane: album -> song (two levels; same idea as Artists,
 * minus the artist grouping). Album names collide across artists here -
 * "Greatest Hits" by two different artists lands in one bucket. */
enum { BLVL_ALBUM = 0, BLVL_SONG };
static int album_level = BLVL_ALBUM;
static char **balbum_list;
static int nbalbums;
static bool albums_built;
static int bsel, btop;

static int *bsong_idx;
static int nbsongs;
static int bssel, bstop;
static char cur_balbum[300];

/* ---- Playlists pane: browse existing MPD playlists -> their songs.
 * Reuses playlist_names/nplaylists (also loaded by the P overlay) for
 * the list level; playlist contents are their own array since they
 * aren't DB indices - a playlist can reference anything in the library,
 * in any order, with repeats. */
enum { PLVL_LIST = 0, PLVL_SONGS };
static int playlist_level = PLVL_LIST;
static int psel, ptop;
static struct dbsong *PLSONGS;
static int nplsongs;
static int plsel, pltop;
static char cur_playlist[300];

/* ---- Search pane: a form over the whole library. One row per field;
 * each row has a text expression, a join (AND / OR / NOT) and, for text
 * fields, case-sensitive / fuzzy / exact flags. An empty row matches
 * everything. Rows combine as:  every AND row  AND  (at least one OR row,
 * if any are filled)  AND  none of the NOT rows.
 *   text   "radiohead|bjork"          either (| = or)
 *   number "<320"  ">=256"  "=320"  "256-320"  "<3:30"  (also joined by |)
 *   format "mp3|flac"                 file extension, exact */
enum { SK_TEXT, SK_NUM, SK_EXT };
enum { SF_ANY, SF_TITLE, SF_ARTIST, SF_ALBUMARTIST, SF_ALBUM, SF_GENRE,
       SF_YEAR, SF_FORMAT, SF_KBPS, SF_LENGTH, SF_PATH, SF_COUNT };
enum { J_AND, J_OR, J_NOT };
static const struct { const char *label; int kind; const char *hint; } SFIELD[SF_COUNT] = {
	[SF_ANY]         = {"Any",          SK_TEXT, "title, artist, album or path"},
	[SF_TITLE]       = {"Title",        SK_TEXT, ""},
	[SF_ARTIST]      = {"Artist",       SK_TEXT, "matches every artist on a song, feat. too"},
	[SF_ALBUMARTIST] = {"Album artist", SK_TEXT, ""},
	[SF_ALBUM]       = {"Album",        SK_TEXT, ""},
	[SF_GENRE]       = {"Genre",        SK_TEXT, ""},
	[SF_YEAR]        = {"Year",         SK_NUM,  "e.g. 1990-1999  or  >=2010"},
	[SF_FORMAT]      = {"Format",       SK_EXT,  "e.g. mp3|flac"},
	[SF_KBPS]        = {"Kbps",         SK_NUM,  "e.g. <320  or  256-320"},
	[SF_LENGTH]      = {"Length",       SK_NUM,  "e.g. <3:30  or  >=600 (seconds)"},
	[SF_PATH]        = {"Path",         SK_TEXT, ""},
};
struct srow { char text[96]; int len; bool cs, fuzzy, exact; int join; };
static struct srow SR[SF_COUNT];
static int fsel, ftop;         /* form cursor: 0..SF_COUNT-1 = fields, SF_COUNT = [Search], SF_COUNT+1 = [Clear] */
static bool s_insert;          /* insert mode: keys type into SR[fsel] */
static bool s_results_focus;   /* keys drive the result list rather than the form */
static bool s_ran;             /* a search has been run since the form was last cleared */
static int *sresults;
static int nsresults;
static int ssel, stop;

/* ---- multi-select for building playlists later: a set of song URIs,
 * togglable with space at any pane/level, shown as a marker beside the
 * song. Nothing consumes this yet - the Playlists pane will. */
static char **SELECTED;
static int nselected, capselected;

/* ---- playlist-add overlay (Shift-P): pick a target for the current
 * selection, or type a new playlist name. Full-screen while open. */
static bool playlist_overlay;
static int ov_focus;              /* 0 = selection list, 1 = target list */
static int ov_sel_cur, ov_sel_top;
static int ov_target_cur, ov_target_top;
static bool ov_naming;
static char ov_name[128];
static int ov_name_len;
static char **playlist_names;
static int nplaylists;

/* ---- metadata editor (Shift-... no, lowercase l on a song): reads tags
 * via `metaflac`/`mid3v2`, edits are shelled out the same way after an
 * explicit confirm showing the exact command about to run. Takes over
 * the right-hand pane; h/Esc returns to wherever you came from. */
struct tagpair { char key[64]; char val[256]; };
static struct tagpair *META_TAGS;
static int nmeta_tags;
static int meta_sel, meta_top;
static bool meta_mode;
static char meta_path[700];   /* absolute path on disk */
static char meta_uri[600];    /* original MPD uri, for the title */
static bool meta_is_flac;
static bool meta_supported;

enum { MEDIT_NONE, MEDIT_VALUE, MEDIT_NEWKEY, MEDIT_NEWVAL, MEDIT_CONFIRM };
static int medit_state = MEDIT_NONE;
static char medit_buf[256];
static int medit_len;
static char medit_newkey[256];
static char pending_cmd[2300];
static char pending_label[340];

static struct {
	enum mpd_state state;
	int pos, id, vol;
	unsigned elapsed_ms, total, kbps, qver, rate, bits;
	bool repeat, random, single, consume;
} g = {.pos = -1, .id = -1, .vol = -1};

static unsigned qver_loaded = (unsigned)-1;
static int last_id = -1;

/* MPD resets a song's priority to 0 the moment it starts playing, so the
 * priority alone can't say "this playing song came from up-next". We keep
 * the ids of up-next songs ourselves and put the flag back in load_queue(),
 * which means Q[i].prio > 0 reliably means "up-next" for the whole program,
 * including while that song is playing and after it has finished. */
static unsigned *upn_ids;
static int nupn, capupn;

static char msgbuf[160];
static long msg_until;

static long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void msg(const char *f, ...)
{
	va_list ap;
	va_start(ap, f);
	vsnprintf(msgbuf, sizeof msgbuf, f, ap);
	va_end(ap);
	msg_until = now_ms() + MSG_MS;
}

static void drop_conn(void)
{
	if (conn)
		mpd_connection_free(conn);
	conn = NULL;
}

/* returns 1 if the connection is healthy, 0 after reporting an error */
static int check(void)
{
	if (!conn)
		return 0;
	if (mpd_connection_get_error(conn) == MPD_ERROR_SUCCESS)
		return 1;
	msg("mpd: %s", mpd_connection_get_error_message(conn));
	if (!mpd_connection_clear_error(conn))
		drop_conn(); /* fatal: reconnect from tick() */
	return 0;
}

static bool connect_mpd(void)
{
	last_connect_try = now_ms();
	conn = mpd_connection_new(MPD_HOST, MPD_PORT, 5000);
	if (!conn)
		return false;
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		msg("mpd: %s", mpd_connection_get_error_message(conn));
		drop_conn();
		return false;
	}
	return true;
}

static char *dupz(const char *s) { return strdup(s ? s : ""); }

static void free_queue(void)
{
	for (int i = 0; i < nq; i++) {
		free(Q[i].uri); free(Q[i].title); free(Q[i].artist);
		free(Q[i].album); free(Q[i].track);
	}
	nq = 0;
}

static int fetch_status(void)
{
	if (!conn)
		return 0;
	struct mpd_status *s = mpd_run_status(conn);
	if (!s) {
		check();
		return 0;
	}
	g.state = mpd_status_get_state(s);
	g.pos = mpd_status_get_song_pos(s);
	g.id = mpd_status_get_song_id(s);
	g.elapsed_ms = mpd_status_get_elapsed_ms(s);
	g.total = mpd_status_get_total_time(s);
	g.kbps = mpd_status_get_kbit_rate(s);
	g.vol = mpd_status_get_volume(s);
	g.repeat = mpd_status_get_repeat(s);
	g.random = mpd_status_get_random(s);
	g.single = mpd_status_get_single(s);
	g.consume = mpd_status_get_consume(s);
	g.qver = mpd_status_get_queue_version(s);
	const struct mpd_audio_format *af = mpd_status_get_audio_format(s);
	g.rate = af ? af->sample_rate : 0;
	g.bits = af ? af->bits : 0;
	mpd_status_free(s);
	return 1;
}

/* Re-flag remembered up-next songs whose priority MPD has reset (the one
 * that is playing, or any that have been played), then rebuild the
 * remembered set from the queue, which forgets ids that have left it. */
static void sync_upnext(void)
{
	for (int i = 0; i < nq; i++) {
		if (Q[i].prio > 0)
			continue;
		for (int k = 0; k < nupn; k++)
			if (upn_ids[k] == Q[i].id) {
				Q[i].prio = UP_NEXT_PRIO;
				break;
			}
	}
	if (capupn < nq) {
		capupn = nq + 256;
		upn_ids = realloc(upn_ids, (size_t)capupn * sizeof *upn_ids);
		if (!upn_ids)
			abort();
	}
	nupn = 0;
	for (int i = 0; i < nq; i++)
		if (Q[i].prio > 0)
			upn_ids[nupn++] = Q[i].id;
}

static void load_queue(void)
{
	free_queue();
	if (!conn || !mpd_send_list_queue_meta(conn)) {
		check();
		return;
	}
	struct mpd_song *s;
	while ((s = mpd_recv_song(conn))) {
		if (nq == capq) {
			capq = capq ? capq * 2 : 1024;
			Q = realloc(Q, (size_t)capq * sizeof *Q);
			if (!Q)
				abort();
		}
		struct song *o = &Q[nq++];
		o->id = mpd_song_get_id(s);
		o->prio = mpd_song_get_prio(s);
		o->dur = mpd_song_get_duration(s);
		o->uri = dupz(mpd_song_get_uri(s));
		o->title = dupz(mpd_song_get_tag(s, MPD_TAG_TITLE, 0));
		o->artist = dupz(mpd_song_get_tag(s, MPD_TAG_ARTIST, 0));
		o->album = dupz(mpd_song_get_tag(s, MPD_TAG_ALBUM, 0));
		o->track = dupz(mpd_song_get_tag(s, MPD_TAG_TRACK, 0));
		mpd_song_free(s);
	}
	mpd_response_finish(conn);
	check();
	qver_loaded = g.qver;
	sync_upnext();
	if (sel >= nq) sel = nq - 1;
	if (sel < 0) sel = 0;
}

static void refresh_all(void)
{
	if (fetch_status())
		load_queue();
}

/* Every tag MPD gave us for a song, as "Name\tvalue\n" lines, so the
 * search pane can match any tag (genre, date, composer, ...). */
static char *song_tags_blob(const struct mpd_song *s)
{
	size_t cap = 256, len = 0;
	char *b = malloc(cap);
	if (!b)
		abort();
	b[0] = 0;
	for (int t = 0; t < MPD_TAG_COUNT; t++) {
		const char *name = mpd_tag_name((enum mpd_tag_type)t);
		const char *v;
		if (!name)
			continue;
		for (unsigned i = 0; (v = mpd_song_get_tag(s, (enum mpd_tag_type)t, i)); i++) {
			size_t need = strlen(name) + strlen(v) + 3;
			while (len + need > cap)
				cap *= 2;
			b = realloc(b, cap);
			if (!b)
				abort();
			len += (size_t)snprintf(b + len, cap - len, "%s\t%s\n", name, v);
		}
	}
	return b;
}

static void free_db(void)
{
	for (int i = 0; i < ndb; i++) {
		free(DB[i].uri); free(DB[i].title); free(DB[i].artist);
		free(DB[i].album); free(DB[i].track); free(DB[i].tags);
	}
	ndb = 0;
}

/* Loads the whole library flat, once. MPD keeps this in memory anyway
 * (it's the same data `update` refreshes), so at ~1300 songs this is
 * cheap; it would need paging well before six figures of songs. */
static void load_db(void)
{
	if (db_loaded || !conn)
		return;
	free_db();
	if (!mpd_send_list_all_meta(conn, "")) {
		check();
		return;
	}
	struct mpd_song *s;
	while ((s = mpd_recv_song(conn))) {
		if (ndb == capdb) {
			capdb = capdb ? capdb * 2 : 1024;
			DB = realloc(DB, (size_t)capdb * sizeof *DB);
			if (!DB)
				abort();
		}
		struct dbsong *o = &DB[ndb++];
		o->dur = mpd_song_get_duration(s);
		o->uri = dupz(mpd_song_get_uri(s));
		o->title = dupz(mpd_song_get_tag(s, MPD_TAG_TITLE, 0));
		o->artist = dupz(mpd_song_get_tag(s, MPD_TAG_ARTIST, 0));
		o->album = dupz(mpd_song_get_tag(s, MPD_TAG_ALBUM, 0));
		o->track = dupz(mpd_song_get_tag(s, MPD_TAG_TRACK, 0));
		o->tags = song_tags_blob(s);
		mpd_song_free(s);
	}
	mpd_response_finish(conn);
	check();
	db_loaded = true;
}


static int find_id(int id)
{
	if (id < 0)
		return -1;
	for (int i = 0; i < nq; i++)
		if (Q[i].id == (unsigned)id)
			return i;
	return -1;
}
