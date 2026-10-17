/* ---- Artists pane helpers --------------------------------------------*/
static int cmp_str(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static void free_str_list(char **list, int n)
{
	for (int i = 0; i < n; i++)
		free(list[i]);
	free(list);
}

static bool artist_has_token(const char *field, const char *target)
{
	size_t sl = strlen(ARTIST_SEP), tl = strlen(target);
	const char *cur = field;
	while (1) {
		const char *sep = sl ? strstr(cur, ARTIST_SEP) : NULL;
		size_t len = sep ? (size_t)(sep - cur) : strlen(cur);
		if (len == tl && !strncmp(cur, target, len))
			return true;
		if (!sep)
			return false;
		cur = sep + sl;
	}
}

static void build_artist_list(void)
{
	free_str_list(artist_list, nartists);
	artist_list = NULL;
	nartists = 0;
	size_t sl = strlen(ARTIST_SEP);
	for (int i = 0; i < ndb; i++) {
		const char *cur = DB[i].artist;
		if (!*cur)
			continue;
		while (1) {
			const char *sep = sl ? strstr(cur, ARTIST_SEP) : NULL;
			size_t len = sep ? (size_t)(sep - cur) : strlen(cur);
			if (len > 0 && len < 199) {
				char buf[200];
				memcpy(buf, cur, len);
				buf[len] = 0;
				bool dup = false;
				for (int j = 0; j < nartists; j++)
					if (!strcmp(artist_list[j], buf)) { dup = true; break; }
				if (!dup) {
					artist_list = realloc(artist_list, (size_t)(nartists + 1) * sizeof *artist_list);
					if (!artist_list) abort();
					artist_list[nartists++] = strdup(buf);
				}
			}
			if (!sep)
				break;
			cur = sep + sl;
		}
	}
	qsort(artist_list, (size_t)nartists, sizeof *artist_list, cmp_str);
}

static void build_album_list(const char *artist)
{
	free_str_list(album_list, nalbums);
	album_list = NULL;
	nalbums = 0;
	for (int i = 0; i < ndb; i++) {
		if (!artist_has_token(DB[i].artist, artist))
			continue;
		const char *al = DB[i].album[0] ? DB[i].album : "(no album)";
		bool dup = false;
		for (int j = 0; j < nalbums; j++)
			if (!strcmp(album_list[j], al)) { dup = true; break; }
		if (!dup) {
			album_list = realloc(album_list, (size_t)(nalbums + 1) * sizeof *album_list);
			if (!album_list) abort();
			album_list[nalbums++] = strdup(al);
		}
	}
	qsort(album_list, (size_t)nalbums, sizeof *album_list, cmp_str);
}

static int collect_songs(const char *artist, const char *album, int **out)
{
	int cap = 0, n = 0;
	int *arr = NULL;
	for (int i = 0; i < ndb; i++) {
		if (artist && !artist_has_token(DB[i].artist, artist))
			continue;
		if (album) {
			const char *al = DB[i].album[0] ? DB[i].album : "(no album)";
			if (strcmp(al, album))
				continue;
		}
		if (n == cap) {
			cap = cap ? cap * 2 : 32;
			arr = realloc(arr, (size_t)cap * sizeof *arr);
			if (!arr) abort();
		}
		arr[n++] = i;
	}
	*out = arr;
	return n;
}

static void artist_enter_album(void)
{
	if (!nartists)
		return;
	snprintf(cur_artist, sizeof cur_artist, "%s", artist_list[asel]);
	build_album_list(cur_artist);
	alsel = altop = 0;
	artist_level = ALVL_ALBUM;
}

static void artist_enter_song(void)
{
	if (!nalbums)
		return;
	snprintf(cur_album, sizeof cur_album, "%s", album_list[alsel]);
	free(song_idx);
	song_idx = NULL;
	nsongs = collect_songs(cur_artist, cur_album, &song_idx);
	assel = astop = 0;
	artist_level = ALVL_SONG;
}

static void artist_back(void)
{
	if (artist_level == ALVL_SONG) artist_level = ALVL_ALBUM;
	else if (artist_level == ALVL_ALBUM) artist_level = ALVL_ARTIST;
}

static void artist_queue_next(void)
{
	if (artist_level == ALVL_SONG) {
		if (nsongs) add_up_next(DB[song_idx[assel]].uri);
		return;
	}
	int *idx = NULL;
	int n = artist_level == ALVL_ARTIST
		? (nartists ? collect_songs(artist_list[asel], NULL, &idx) : 0)
		: (nalbums ? collect_songs(cur_artist, album_list[alsel], &idx) : 0);
	for (int i = 0; i < n; i++)
		add_up_next(DB[idx[i]].uri);
	free(idx);
}

static void play_uris_context(const char *const *uris, int n, int start_at)
{
	if (n <= 0)
		return;
	refresh_all();
	for (int i = nq - 1; i >= 0; i--)
		if (Q[i].prio == 0 && !mpd_run_delete_id(conn, Q[i].id))
			check();
	refresh_all();
	int keep = nq;
	for (int i = 0; i < n; i++)
		if (!mpd_run_add(conn, uris[i])) { check(); break; }
	refresh_all();
	if (nq > keep) {
		int at = keep + start_at;
		if (at >= nq) at = nq - 1;
		mpd_run_play_pos(conn, (unsigned)at);
		refresh_all();
	}
}

static void artist_play_now(void)
{
	if (artist_level == ALVL_SONG) {
		if (nsongs) set_context_single(DB[song_idx[assel]].uri);
		return;
	}
	int *idx = NULL;
	int n = artist_level == ALVL_ARTIST
		? (nartists ? collect_songs(artist_list[asel], NULL, &idx) : 0)
		: (nalbums ? collect_songs(cur_artist, album_list[alsel], &idx) : 0);
	if (n <= 0) { free(idx); return; }
	const char **uris = malloc((size_t)n * sizeof *uris);
	if (!uris) abort();
	for (int i = 0; i < n; i++)
		uris[i] = DB[idx[i]].uri;
	play_uris_context(uris, n, 0);
	free(uris);
	free(idx);
}

static void play_all_directory(int start_at)
{
	if (ndb <= 0)
		return;
	const char **uris = malloc((size_t)ndb * sizeof *uris);
	if (!uris) abort();
	for (int i = 0; i < ndb; i++)
		uris[i] = DB[i].uri;
	play_uris_context(uris, ndb, start_at);
	free(uris);
}

static void free_artist_browser(void)
{
	free_str_list(artist_list, nartists); artist_list = NULL; nartists = 0;
	free_str_list(album_list, nalbums); album_list = NULL; nalbums = 0;
	free(song_idx); song_idx = NULL; nsongs = 0;
}

/* ---- Albums pane helpers ---------------------------------------------*/
static void build_balbum_list(void)
{
	free_str_list(balbum_list, nbalbums);
	balbum_list = NULL;
	nbalbums = 0;
	for (int i = 0; i < ndb; i++) {
		const char *al = DB[i].album[0] ? DB[i].album : "(no album)";
		bool dup = false;
		for (int j = 0; j < nbalbums; j++)
			if (!strcmp(balbum_list[j], al)) { dup = true; break; }
		if (!dup) {
			balbum_list = realloc(balbum_list, (size_t)(nbalbums + 1) * sizeof *balbum_list);
			if (!balbum_list) abort();
			balbum_list[nbalbums++] = strdup(al);
		}
	}
	qsort(balbum_list, (size_t)nbalbums, sizeof *balbum_list, cmp_str);
}

static void album_enter_song(void)
{
	if (!nbalbums)
		return;
	snprintf(cur_balbum, sizeof cur_balbum, "%s", balbum_list[bsel]);
	free(bsong_idx);
	bsong_idx = NULL;
	nbsongs = collect_songs(NULL, cur_balbum, &bsong_idx);
	bssel = bstop = 0;
	album_level = BLVL_SONG;
}

static void album_back(void)
{
	if (album_level == BLVL_SONG)
		album_level = BLVL_ALBUM;
}

static void album_queue_next(void)
{
	if (album_level == BLVL_SONG) {
		if (nbsongs) add_up_next(DB[bsong_idx[bssel]].uri);
		return;
	}
	if (!nbalbums)
		return;
	int *idx = NULL;
	int n = collect_songs(NULL, balbum_list[bsel], &idx);
	for (int i = 0; i < n; i++)
		add_up_next(DB[idx[i]].uri);
	free(idx);
}

static void album_play_all(void)
{
	if (album_level == BLVL_SONG) {
		if (nbsongs) set_context_single(DB[bsong_idx[bssel]].uri);
		return;
	}
	if (!nbalbums)
		return;
	int *idx = NULL;
	int n = collect_songs(NULL, balbum_list[bsel], &idx);
	if (n <= 0) { free(idx); return; }
	const char **uris = malloc((size_t)n * sizeof *uris);
	if (!uris) abort();
	for (int i = 0; i < n; i++)
		uris[i] = DB[idx[i]].uri;
	play_uris_context(uris, n, 0);
	free(uris);
	free(idx);
}

static void free_album_browser(void)
{
	free_str_list(balbum_list, nbalbums); balbum_list = NULL; nbalbums = 0;
	free(bsong_idx); bsong_idx = NULL; nbsongs = 0;
}
