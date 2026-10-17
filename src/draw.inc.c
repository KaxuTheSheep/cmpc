/* ====================================================================
 * Drawing
 * ==================================================================== */
static int list_rows(void) { return H - 2; }

static void draw_queue(int x, int w, int h)
{
	int ups = 0;
	for (int i = 0; i < nq; i++)
		ups += Q[i].prio > 0 && i != g.pos;
	char rt[64];
	snprintf(rt, sizeof rt, " %d songs, %d up next ", nq, ups);
	box(x, 0, w, h, ST_BORDER_HI, " queue ", rt);

	int rows = h - 2;
	if (rows < 1)
		return;
	if (sel < top_row) top_row = sel;
	if (sel >= top_row + rows) top_row = sel - rows + 1;
	if (top_row < 0) top_row = 0;

	if (nq == 0) {
		put_str(x + 3, 1, w - 6, ST_DIM, "queue is empty (start with --dir PATH or --playlist NAME)");
		return;
	}

	int ix = x + 1, iw = w - 2;
	int dw = 8; /* fixed width for m:ss / h:mm:ss */
	int gap = 2;
	int prefix = 5;
	int rest = iw - prefix - dw - gap * 2;
	if (rest < 12) rest = 12;
	int track_w = rest * 40 / 100;
	int artist_w = rest * 25 / 100;
	int album_w = rest - track_w - artist_w;
	int col_track = ix + prefix;
	int col_artist = col_track + track_w + gap;
	int col_album = col_artist + artist_w + gap;

	for (int r = 0; r < rows && top_row + r < nq; r++) {
		int i = top_row + r, y = 1 + r;
		const struct song *s = &Q[i];
		struct style st = ST_TEXT;
		bool cur = (i == g.pos);
		if (cur) st = ST_CURRENT;
		else if (i == sel) st = ST_CURSOR;
		if (cur || i == sel)
			fill(ix, y, iw, st);
		if (cur)
			put_str(ix + 1, y, 1, st, PLAY_MARK);
		if (sel_has(s->uri))
			put_str(ix + 2, y, 1, cur ? st : ST_SELECT, SELECT_MARK);
		if (s->prio > 0)
			put_str(ix + 3, y, 1, cur ? st : ST_UPNEXT, UPNEXT_BAR);

		char art[256], d[16];
		pretty_artist(art, sizeof art, s->artist);
		fmt_time(d, sizeof d, s->dur);

		put_str(col_track, y, track_w, st, song_title(s));
		put_str(col_artist, y, artist_w, st, art);
		put_str(col_album, y, album_w, st, s->album);
		put_str_r(ix + iw - dw, y, dw, st, d);
	}
}

static void draw_dir(int x, int w, int h)
{
	char rt[32];
	snprintf(rt, sizeof rt, " %d songs ", ndb);
	box(x, 0, w, h, ST_BORDER_HI, " directory ", rt);

	int rows = h - 2;
	if (rows < 1)
		return;
	if (dsel < dtop) dtop = dsel;
	if (dsel >= dtop + rows) dtop = dsel - rows + 1;
	if (dtop < 0) dtop = 0;

	if (!db_loaded) {
		put_str(x + 3, 1, w - 6, ST_DIM, "loading...");
		return;
	}
	if (ndb == 0) {
		put_str(x + 3, 1, w - 6, ST_DIM, "library is empty");
		return;
	}

	int ix = x + 1, iw = w - 2;
	int dw = 8, gap = 2;
	int prefix = 3; /* [select mark][gap] - directory has no play/up-next marks */
	int rest = iw - prefix - dw - gap * 2;
	if (rest < 12) rest = 12;
	int track_w = rest * 40 / 100;
	int artist_w = rest * 25 / 100;
	int album_w = rest - track_w - artist_w;
	int col_track = ix + prefix;
	int col_artist = col_track + track_w + gap;
	int col_album = col_artist + artist_w + gap;

	for (int r = 0; r < rows && dtop + r < ndb; r++) {
		int i = dtop + r, y = 1 + r;
		const struct dbsong *s = &DB[i];
		struct style st = (i == dsel) ? ST_CURSOR : ST_TEXT;
		if (i == dsel)
			fill(ix, y, iw, st);
		if (sel_has(s->uri))
			put_str(ix + 1, y, 1, ST_SELECT, SELECT_MARK);

		char art[256], d[16];
		pretty_artist(art, sizeof art, s->artist);
		fmt_time(d, sizeof d, s->dur);

		put_str(col_track, y, track_w, st, dbsong_title(s));
		put_str(col_artist, y, artist_w, st, art);
		put_str(col_album, y, album_w, st, s->album);
		put_str_r(ix + iw - dw, y, dw, st, d);
	}
}

/* placeholder for panes not built yet: Playlists, Artists, Albums, Search */
static void draw_str_list(int x, int w, int h, const char *title,
			   char *const *items, int n, int *cur, int *top)
{
	box(x, 0, w, h, ST_BORDER_HI, title, NULL);
	int rows = h - 2;
	if (rows < 1)
		return;
	if (*cur < *top) *top = *cur;
	if (*cur >= *top + rows) *top = *cur - rows + 1;
	if (*top < 0) *top = 0;
	if (n == 0) {
		put_str(x + 3, 1, w - 6, ST_DIM, "(empty)");
		return;
	}
	for (int r = 0; r < rows && *top + r < n; r++) {
		int i = *top + r, y = 1 + r;
		struct style st = (i == *cur) ? ST_CURSOR : ST_TEXT;
		if (i == *cur)
			fill(x + 1, y, w - 2, st);
		put_str(x + 2, y, w - 4, st, items[i]);
	}
}

/* Shared song-list renderer for the leaf level of Artists/Albums panes:
 * [select mark] Title ................ Duration */
static void draw_song_list_y(int x, int y0, int w, int h, struct style bst, const char *title,
			      const int *idx, int n, int *cur, int *top)
{
	box(x, y0, w, h, bst, title, NULL);
	int rows = h - 2;
	if (rows < 1)
		return;
	if (*cur < *top) *top = *cur;
	if (*cur >= *top + rows) *top = *cur - rows + 1;
	if (*top < 0) *top = 0;
	if (n == 0) {
		put_str(x + 3, y0 + 1, w - 6, ST_DIM, "(empty)");
		return;
	}
	int ix = x + 1, iw = w - 2, dw = 8;
	for (int r = 0; r < rows && *top + r < n; r++) {
		int i = *top + r, y = y0 + 1 + r;
		const struct dbsong *s = &DB[idx[i]];
		struct style st = (i == *cur) ? ST_CURSOR : ST_TEXT;
		if (i == *cur)
			fill(ix, y, iw, st);
		if (sel_has(s->uri))
			put_str(ix + 1, y, 1, ST_SELECT, SELECT_MARK);
		char d[16];
		fmt_time(d, sizeof d, s->dur);
		put_str(ix + 3, y, iw - dw - 4, st, dbsong_title(s));
		put_str_r(ix + iw - dw, y, dw, st, d);
	}
}

static void draw_song_list(int x, int w, int h, const char *title,
			    const int *idx, int n, int *cur, int *top)
{
	draw_song_list_y(x, 0, w, h, ST_BORDER_HI, title, idx, n, cur, top);
}

static void draw_artist_pane(int x, int w, int h)
{
	char title[620];
	if (artist_level == ALVL_ARTIST) {
		draw_str_list(x, w, h, " artists ", artist_list, nartists, &asel, &atop);
		return;
	}
	if (artist_level == ALVL_ALBUM) {
		snprintf(title, sizeof title, " artists > %s ", cur_artist);
		draw_str_list(x, w, h, title, album_list, nalbums, &alsel, &altop);
		return;
	}
	snprintf(title, sizeof title, " artists > %s > %s ", cur_artist, cur_album);
	draw_song_list(x, w, h, title, song_idx, nsongs, &assel, &astop);
}

static void draw_album_pane(int x, int w, int h)
{
	char title[620];
	if (album_level == BLVL_ALBUM) {
		draw_str_list(x, w, h, " albums ", balbum_list, nbalbums, &bsel, &btop);
		return;
	}
	snprintf(title, sizeof title, " albums > %s ", cur_balbum);
	draw_song_list(x, w, h, title, bsong_idx, nbsongs, &bssel, &bstop);
}

static void draw_song_list_direct(int x, int w, int h, const char *title,
				   const struct dbsong *arr, int n, int *cur, int *top)
{
	box(x, 0, w, h, ST_BORDER_HI, title, NULL);
	int rows = h - 2;
	if (rows < 1)
		return;
	if (*cur < *top) *top = *cur;
	if (*cur >= *top + rows) *top = *cur - rows + 1;
	if (*top < 0) *top = 0;
	if (n == 0) {
		put_str(x + 3, 1, w - 6, ST_DIM, "(empty)");
		return;
	}
	int ix = x + 1, iw = w - 2, dw = 8;
	for (int r = 0; r < rows && *top + r < n; r++) {
		int i = *top + r, y = 1 + r;
		const struct dbsong *s = &arr[i];
		struct style st = (i == *cur) ? ST_CURSOR : ST_TEXT;
		if (i == *cur)
			fill(ix, y, iw, st);
		if (sel_has(s->uri))
			put_str(ix + 1, y, 1, ST_SELECT, SELECT_MARK);
		char d[16];
		fmt_time(d, sizeof d, s->dur);
		put_str(ix + 3, y, iw - dw - 4, st, dbsong_title(s));
		put_str_r(ix + iw - dw, y, dw, st, d);
	}
}

static void draw_playlist_pane(int x, int w, int h)
{
	if (playlist_level == PLVL_LIST) {
		draw_str_list(x, w, h, " playlists ", playlist_names, nplaylists, &psel, &ptop);
		return;
	}
	char title[400];
	snprintf(title, sizeof title, " playlists > %s ", cur_playlist);
	draw_song_list_direct(x, w, h, title, PLSONGS, nplsongs, &plsel, &pltop);
}

/* Height of the form box. It shrinks (and scrolls) on short terminals so
 * the results always keep at least a few rows. */
static int sform_h(int h)
{
	int fh = SF_COUNT + 3;           /* border, one row per field, buttons, border */
	if (fh > h - 6) fh = h - 6;
	return fh < 5 ? 5 : fh;
}

static int sresult_rows(void)
{
	int r = H - sform_h(H) - 2;
	return r > 0 ? r : 1;
}

/* the tail of s that fits in `w` columns, so the end being typed stays visible */
static const char *tail_fit(const char *s, int w)
{
	uint32_t cp;
	int n;
	while (str_width(s) > w && (n = u8dec(s, &cp)) > 0)
		s += n;
	return s;
}

static void draw_search_pane(int x, int w, int h)
{
	int fh = sform_h(h);
	bool ff = !s_results_focus;
	box(x, 0, w, fh, ff ? ST_BORDER_HI : ST_BORDER, " search ", s_insert ? " INSERT " : " NORMAL ");

	int rows = fh - 2;                       /* visible form rows */
	int cur_row = fsel < SF_COUNT ? fsel : SF_COUNT;
	if (cur_row < ftop) ftop = cur_row;
	if (cur_row >= ftop + rows) ftop = cur_row - rows + 1;
	if (ftop < 0) ftop = 0;

	int ix = x + 1, iw = w - 2;
	int jw = 5, lw = 13, fw = 8;             /* join, label, flags columns */
	int in_x = ix + jw + lw, in_w = iw - jw - lw - fw;
	if (in_w < 8) in_w = 8;
	for (int r = 0; r < rows && ftop + r <= SF_COUNT; r++) {
		int f = ftop + r, y = 1 + r;
		if (f == SF_COUNT) {                 /* button row */
			struct style s1 = (ff && fsel == SF_COUNT) ? ST_CURSOR : ST_TEXT;
			struct style s2 = (ff && fsel == SF_COUNT + 1) ? ST_CURSOR : ST_TEXT;
			put_str(ix + jw, y, 12, s1, "[ Search ]");
			put_str(ix + jw + 12, y, 11, s2, "[ Clear ]");
			continue;
		}
		const struct srow *sr = &SR[f];
		bool focus = ff && fsel == f;
		bool empty = sr->len == 0;
		static const char *const JN[] = {"AND", "OR", "NOT"};
		put_str(ix + 1, y, jw - 1, empty ? ST_DIM : (sr->join == J_AND ? ST_TEXT : ST_ACCENT), JN[sr->join]);
		put_str(ix + jw, y, lw - 1, focus ? ST_CURSOR : ST_TEXT, SFIELD[f].label);

		/* [ text ] */
		struct style ts = (focus && s_insert) ? ST_ACCENT : ST_TEXT;
		put_cell(in_x, y, '[', focus ? ST_BORDER_HI : ST_BORDER);
		put_cell(in_x + in_w - 1, y, ']', focus ? ST_BORDER_HI : ST_BORDER);
		int tw = in_w - 3;                   /* room for text (+ cursor) */
		if (empty && !(focus && s_insert)) {
			put_str(in_x + 1, y, in_w - 2, ST_DIM, SFIELD[f].hint);
		} else {
			const char *t = tail_fit(sr->text, tw);
			int used = put_str(in_x + 1, y, tw, ts, t);
			if (focus && s_insert)
				put_cell(in_x + 1 + used, y, '_', ST_ACCENT);
		}
		/* flags */
		int fx = in_x + in_w + 1;
		if (SFIELD[f].kind == SK_TEXT) {
			put_str(fx, y, 2, sr->cs ? ST_ACCENT : ST_DIM, sr->cs ? "Aa" : "aa");
			put_str(fx + 3, y, 1, sr->fuzzy ? ST_ACCENT : ST_DIM, sr->fuzzy ? "~" : "\xc2\xb7");
			put_str(fx + 5, y, 1, sr->exact ? ST_ACCENT : ST_DIM, sr->exact ? "=" : "\xc2\xb7");
		} else {
			put_str(fx, y, 4, ST_DIM, SFIELD[f].kind == SK_NUM ? "#" : "ext");
		}
	}
	/* bottom border: a message if there is one, else the keys for this mode */
	const char *hint = s_insert
		? " esc/enter done   tab next field   ^U clear "
		: " i edit  / run  o join  c case  f fuzzy  e exact  x clear ";
	if (msgbuf[0] && now_ms() < msg_until) {
		char mb[400];
		snprintf(mb, sizeof mb, " %s ", msgbuf);
		put_str(x + 2, fh - 1, w - 4, ST_ACCENT, mb);
	} else {
		put_str(x + 2, fh - 1, w - 4, ST_DIM, hint);
	}

	/* results */
	struct style rb = s_results_focus ? ST_BORDER_HI : ST_BORDER;
	if (!s_ran) {
		box(x, fh, w, h - fh, rb, " results ", NULL);
		put_str(x + 3, fh + 1, w - 6, ST_DIM, "fill in the form, then / or [ Search ]");
		return;
	}
	char title[64];
	snprintf(title, sizeof title, nsresults ? " results (%d) " : " no matches ", nsresults);
	draw_song_list_y(x, fh, w, h - fh, rb, title, sresults, nsresults, &ssel, &stop);
}

static void draw_metadata_pane(int x, int w, int h)
{
	char title[640];
	const char *base = title_of("", meta_uri);
	snprintf(title, sizeof title, " tags: %s ", base);
	box(x, 0, w, h, ST_BORDER_HI, title, NULL);

	if (!meta_supported) {
		put_str(x + 3, 1, w - 6, ST_DIM, "unsupported file type");
		return;
	}
	int rows = h - 2;
	int count = nmeta_tags + 1; /* + "+ new tag" row */
	if (rows < 1)
		return;
	if (meta_sel < meta_top) meta_top = meta_sel;
	if (meta_sel >= meta_top + rows) meta_top = meta_sel - rows + 1;
	if (meta_top < 0) meta_top = 0;

	int ix = x + 1, iw = w - 2;
	int keyw = iw * 30 / 100;
	for (int r = 0; r < rows && meta_top + r < count; r++) {
		int i = meta_top + r, y = 1 + r;
		bool isnew = (i == nmeta_tags);
		struct style st = (i == meta_sel) ? ST_CURSOR : (isnew ? ST_ACCENT : ST_TEXT);
		if (i == meta_sel)
			fill(ix, y, iw, st);
		if (isnew) {
			put_str(ix + 1, y, iw - 2, st, "+ new tag");
		} else {
			put_str(ix + 1, y, keyw, st, META_TAGS[i].key);
			put_str(ix + 1 + keyw + 1, y, iw - keyw - 3, st, META_TAGS[i].val);
		}
	}

	char pb[2400];
	switch (medit_state) {
	case MEDIT_VALUE:
		snprintf(pb, sizeof pb, "new value for %s: %s", META_TAGS[meta_sel].key, medit_buf);
		put_str(x + 1, h - 1, w - 2, ST_ACCENT, pb);
		break;
	case MEDIT_NEWKEY:
		snprintf(pb, sizeof pb, "new tag name: %s", medit_buf);
		put_str(x + 1, h - 1, w - 2, ST_ACCENT, pb);
		break;
	case MEDIT_NEWVAL:
		snprintf(pb, sizeof pb, "value for %s: %s", medit_newkey, medit_buf);
		put_str(x + 1, h - 1, w - 2, ST_ACCENT, pb);
		break;
	case MEDIT_CONFIRM:
		snprintf(pb, sizeof pb, "run: %s  -  y/n", pending_cmd);
		put_str(x + 1, h - 1, w - 2, ST_ACCENT, pb);
		break;
	default:
		if (msgbuf[0] && now_ms() < msg_until)
			put_str(x + 1, h - 1, w - 2, ST_ACCENT, msgbuf);
		else
			put_str(x + 1, h - 1, w - 2, ST_DIM, "i edit  h/esc back");
	}
}

static void draw_winamp_bar(int x, int y, int w)
{
	const bool on4[4] = {g.repeat, shuffle_loop, g.consume, g.single};
	const char L[4] = {'z', 'x', 'c', 'v'};
	int ci = find_id(g.id);
	const struct song *s = ci >= 0 ? &Q[ci] : NULL;
	char lbl[8], b[512], t1[32], t2[32];

	for (int r = 0; r < 4; r++) {
		snprintf(lbl, sizeof lbl, "[%c]", L[r]);
		put_str(x, y + r, 3, on4[r] ? ST_ACCENT : ST_DIM, lbl);
	}

	int c1w = w >= 38 ? 15 : (w > 10 ? (w - 4) / 2 - 1 : 0);
	int c1 = x + 4, c2 = x + 4 + c1w + 2, c2w = w - 4 - c1w - 2;
	if (c2w < 0) c2w = 0;

	fmt_time(t1, sizeof t1, g.elapsed_ms / 1000);
	fmt_time(t2, sizeof t2, g.total);
	snprintf(b, sizeof b, "%s/%s", t1, t2);
	put_str(c1, y, c1w, ST_TEXT, b);

	put_str(c1, y + 1, c1w, ST_DIM, PANE_NAMES[current_pane]);

	const char *state = g.state == MPD_STATE_PLAY ? "[playing]"
			   : g.state == MPD_STATE_PAUSE ? "[paused]" : "[stopped]";
	put_str(c1, y + 2, c1w, ST_ACCENT, state);

	if (g.vol >= 0) {
		snprintf(b, sizeof b, "vol %d%%", g.vol);
		put_str(c1, y + 3, c1w, ST_DIM, b);
	}

	put_str(c2, y, c2w, ST_TITLE, s ? song_title(s) : "No song");

	if (s) {
		char art[256];
		pretty_artist(art, sizeof art, s->artist);
		snprintf(b, sizeof b, "%s%s%s", art, art[0] && s->album[0] ? " - " : "", s->album);
		put_str(c2, y + 1, c2w, ST_ACCENT, b[0] ? b : "");
	}

	if (is_lossless(s ? s->uri : "")) {
		char up[16];
		size_t i = 0;
		for (const char *e = ext_of(s->uri); *e && i < sizeof up - 1; e++)
			up[i++] = (char)toupper((unsigned char)*e);
		up[i] = 0;
		if (g.rate && g.bits && g.bits <= 32)
			snprintf(b, sizeof b, "%s lossless  %g kHz / %u bit", up, g.rate / 1000.0, g.bits);
		else
			snprintf(b, sizeof b, "%s lossless", up);
	} else if (g.kbps) {
		snprintf(b, sizeof b, "%u kbps  %g kHz", g.kbps, g.rate / 1000.0);
	} else {
		b[0] = '-'; b[1] = 0;
	}
	put_str(c2, y + 2, c2w, ST_TEXT, b);

	if (!conn)
		put_str(c2, y + 3, c2w, ST_ACCENT, "disconnected - retrying");
	else if (search_mode) {
		char pb[80];
		snprintf(pb, sizeof pb, "/%s", search_q);
		put_str(c2, y + 3, c2w, ST_ACCENT, pb);
	} else if (msgbuf[0] && now_ms() < msg_until)
		put_str(c2, y + 3, c2w, ST_ACCENT, msgbuf);
}

static void draw_playlist_overlay(void)
{
	if (W < 40 || H < 16) {
		put_str(0, 0, W, ST_TEXT, "terminal too small");
		return;
	}
	int selh = H / 2, targeth = H - selh;

	box(0, 0, W, selh, ov_focus == 0 ? ST_BORDER_HI : ST_BORDER,
	    " selected ", NULL);
	int rows = selh - 2;
	if (rows > 0) {
		if (ov_sel_cur < ov_sel_top) ov_sel_top = ov_sel_cur;
		if (ov_sel_cur >= ov_sel_top + rows) ov_sel_top = ov_sel_cur - rows + 1;
		if (ov_sel_top < 0) ov_sel_top = 0;
		for (int r = 0; r < rows && ov_sel_top + r < nselected; r++) {
			int i = ov_sel_top + r, y = 1 + r;
			bool cur = (i == ov_sel_cur && ov_focus == 0);
			struct style st = cur ? ST_CURSOR : ST_TEXT;
			if (cur) fill(1, y, W - 2, st);
			char buf[300];
			snprintf(buf, sizeof buf, "%d. %s", i + 1, label_for_uri(SELECTED[i]));
			put_str(2, y, W - 4, st, buf);
		}
	}

	box(0, selh, W, targeth, ov_focus == 1 ? ST_BORDER_HI : ST_BORDER,
	    " add to playlist ", NULL);
	int rows2 = targeth - 2, total = nplaylists + 1;
	if (rows2 > 0) {
		if (ov_target_cur < ov_target_top) ov_target_top = ov_target_cur;
		if (ov_target_cur >= ov_target_top + rows2) ov_target_top = ov_target_cur - rows2 + 1;
		if (ov_target_top < 0) ov_target_top = 0;
		for (int r = 0; r < rows2 && ov_target_top + r < total; r++) {
			int i = ov_target_top + r, y = selh + 1 + r;
			bool isnew = (i == nplaylists);
			bool cur = (i == ov_target_cur && ov_focus == 1);
			struct style st = cur ? ST_CURSOR : (isnew ? ST_ACCENT : ST_TEXT);
			if (cur) fill(1, y, W - 2, st);
			put_str(2, y, W - 4, st, isnew ? "[ new playlist ]" : playlist_names[i]);
		}
	}

	if (ov_naming) {
		char pb[160];
		snprintf(pb, sizeof pb, "new playlist name: %s", ov_name);
		put_str(1, H - 1, W - 2, ST_ACCENT, pb);
	} else if (msgbuf[0] && now_ms() < msg_until) {
		put_str(1, H - 1, W - 2, ST_ACCENT, msgbuf);
	} else {
		put_str(1, H - 1, W - 2, ST_DIM,
			"tab switch pane  j/k move  J/K reorder  d remove  enter choose/confirm  esc cancel");
	}
}

