/* ---- multi-select set --------------------------------------------- */
static bool sel_has(const char *uri)
{
	for (int i = 0; i < nselected; i++)
		if (!strcmp(SELECTED[i], uri))
			return true;
	return false;
}

static void sel_add(const char *uri)
{
	if (sel_has(uri))
		return;
	if (nselected == capselected) {
		capselected = capselected ? capselected * 2 : 64;
		SELECTED = realloc(SELECTED, (size_t)capselected * sizeof *SELECTED);
		if (!SELECTED) abort();
	}
	SELECTED[nselected++] = strdup(uri);
}

static void sel_remove(const char *uri)
{
	for (int i = 0; i < nselected; i++) {
		if (!strcmp(SELECTED[i], uri)) {
			free(SELECTED[i]);
			memmove(&SELECTED[i], &SELECTED[i + 1], (size_t)(nselected - i - 1) * sizeof *SELECTED);
			nselected--;
			return;
		}
	}
}

static void sel_toggle_one(const char *uri)
{
	if (sel_has(uri)) sel_remove(uri); else sel_add(uri);
}

/* Toggle a whole group at once: if every song in it is already selected,
 * deselect the lot; otherwise select whatever's missing. */
static void sel_toggle_group(const char *const *uris, int n)
{
	bool all = true;
	for (int i = 0; i < n; i++)
		if (!sel_has(uris[i])) { all = false; break; }
	for (int i = 0; i < n; i++) {
		if (all) sel_remove(uris[i]);
		else sel_add(uris[i]);
	}
}

static void sel_toggle_group_idx(const int *idx, int n)
{
	const char **uris = malloc((size_t)n * sizeof *uris);
	if (!uris) abort();
	for (int i = 0; i < n; i++)
		uris[i] = DB[idx[i]].uri;
	sel_toggle_group(uris, n);
	free(uris);
}

static void free_selection(void)
{
	for (int i = 0; i < nselected; i++)
		free(SELECTED[i]);
	free(SELECTED);
	SELECTED = NULL; nselected = capselected = 0;
}

/* ---- playlist-add overlay ---------------------------------------- */
static void free_playlist_names(void)
{
	free_str_list(playlist_names, nplaylists);
	playlist_names = NULL;
	nplaylists = 0;
}

static void load_playlist_names(void)
{
	free_playlist_names();
	if (!conn || !mpd_send_list_playlists(conn)) {
		check();
		return;
	}
	struct mpd_playlist *pl;
	while ((pl = mpd_recv_playlist(conn))) {
		playlist_names = realloc(playlist_names, (size_t)(nplaylists + 1) * sizeof *playlist_names);
		if (!playlist_names) abort();
		playlist_names[nplaylists++] = strdup(mpd_playlist_get_path(pl));
		mpd_playlist_free(pl);
	}
	mpd_response_finish(conn);
	check();
	qsort(playlist_names, (size_t)nplaylists, sizeof *playlist_names, cmp_str);
}

static void free_plsongs(void)
{
	for (int i = 0; i < nplsongs; i++) {
		free(PLSONGS[i].uri); free(PLSONGS[i].title);
		free(PLSONGS[i].artist); free(PLSONGS[i].album); free(PLSONGS[i].track);
	}
	free(PLSONGS);
	PLSONGS = NULL;
	nplsongs = 0;
}

static void load_plsongs(const char *name)
{
	free_plsongs();
	if (!conn || !mpd_send_list_playlist_meta(conn, name)) {
		check();
		return;
	}
	struct mpd_song *s;
	while ((s = mpd_recv_song(conn))) {
		PLSONGS = realloc(PLSONGS, (size_t)(nplsongs + 1) * sizeof *PLSONGS);
		if (!PLSONGS) abort();
		struct dbsong *o = &PLSONGS[nplsongs++];
		o->dur = mpd_song_get_duration(s);
		o->uri = dupz(mpd_song_get_uri(s));
		o->title = dupz(mpd_song_get_tag(s, MPD_TAG_TITLE, 0));
		o->artist = dupz(mpd_song_get_tag(s, MPD_TAG_ARTIST, 0));
		o->album = dupz(mpd_song_get_tag(s, MPD_TAG_ALBUM, 0));
		o->track = dupz(mpd_song_get_tag(s, MPD_TAG_TRACK, 0));
		o->tags = NULL;
		mpd_song_free(s);
	}
	mpd_response_finish(conn);
	check();
}

static void playlist_enter_songs(void)
{
	if (!nplaylists)
		return;
	snprintf(cur_playlist, sizeof cur_playlist, "%s", playlist_names[psel]);
	load_plsongs(cur_playlist);
	plsel = pltop = 0;
	playlist_level = PLVL_SONGS;
}

static void playlist_back(void)
{
	if (playlist_level == PLVL_SONGS)
		playlist_level = PLVL_LIST;
}

/* "a whole playlist's worth" either way - these don't need DB at all,
 * unlike every other pane's group actions, since the songs are already
 * loaded straight from MPD's own playlist contents. */
static void playlist_queue_next(const char *name)
{
	load_plsongs(name);
	for (int i = 0; i < nplsongs; i++)
		add_up_next(PLSONGS[i].uri);
}

static void playlist_play_all(const char *name)
{
	load_plsongs(name);
	if (!nplsongs)
		return;
	const char **uris = malloc((size_t)nplsongs * sizeof *uris);
	if (!uris) abort();
	for (int i = 0; i < nplsongs; i++)
		uris[i] = PLSONGS[i].uri;
	play_uris_context(uris, nplsongs, 0);
	free(uris);
}

static void playlist_select_all(const char *name)
{
	load_plsongs(name);
	if (!nplsongs)
		return;
	const char **uris = malloc((size_t)nplsongs * sizeof *uris);
	if (!uris) abort();
	for (int i = 0; i < nplsongs; i++)
		uris[i] = PLSONGS[i].uri;
	sel_toggle_group(uris, nplsongs);
	free(uris);
}

static const char *dbsong_title(const struct dbsong *s); /* defined below, with the other formatting helpers */
static const char *ext_of(const char *uri);               /* ditto */

/* uri -> a human-readable label, by looking it up in the library; falls
 * back to the bare filename if it's not in DB for some reason. */
static const char *label_for_uri(const char *uri)
{
	for (int i = 0; i < ndb; i++)
		if (!strcmp(DB[i].uri, uri))
			return dbsong_title(&DB[i]);
	const char *b = strrchr(uri, '/');
	return b ? b + 1 : uri;
}

static void open_playlist_overlay(void)
{
	if (!conn)
		return;
	if (!nselected) {
		msg("nothing selected (space to select songs first)");
		return;
	}
	load_playlist_names();
	playlist_overlay = true;
	ov_focus = 0;
	ov_sel_cur = ov_sel_top = 0;
	ov_target_cur = ov_target_top = 0;
	ov_naming = false;
}

static void ov_move_sel(int dir)
{
	int j = ov_sel_cur + dir;
	if (j < 0 || j >= nselected)
		return;
	char *t = SELECTED[ov_sel_cur];
	SELECTED[ov_sel_cur] = SELECTED[j];
	SELECTED[j] = t;
	ov_sel_cur = j;
}

/* Appends the whole pending selection, in its current order, to `name`
 * (mpd_run_playlist_add creates the playlist file if it doesn't exist
 * yet - same directory MPD already manages, no path handling needed). */
static void commit_to_playlist(const char *name)
{
	int added = 0;
	for (int i = 0; i < nselected; i++) {
		if (mpd_run_playlist_add(conn, name, SELECTED[i]))
			added++;
		else
			check();
	}
	msg("added %d song%s to \"%s\"", added, added == 1 ? "" : "s", name);
	free_selection();
	playlist_overlay = false;
}
