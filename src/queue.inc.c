/* ====================================================================
 * Queue logic
 * ==================================================================== */

/* first index after the current song's up-next block */
static int ctx_start(int cur)
{
	int i = cur + 1;
	while (i < nq && Q[i].prio > 0)
		i++;
	return i;
}

/* Shuffle the tail of the context. Uses fresh Q / g.pos. `min_tail` is the
 * number of songs that were just pushed to the bottom: they are always
 * included, even past SHUFFLE_CAP, so a long jump is shuffled in full. */
static void shuffle_tail(int min_tail)
{
	if (g.pos < 0 || g.pos >= nq)
		return;
	int ctx = ctx_start(g.pos);
	int n = nq - ctx;
	if (n <= PROTECT_HEAD + 1)
		return;
	int t = n * SHUFFLE_PCT / 100;
	if (t < SHUFFLE_MIN) t = SHUFFLE_MIN;
	if (t > SHUFFLE_CAP) t = SHUFFLE_CAP;
	if (t < min_tail) t = min_tail;
	int start = nq - t;
	if (start < ctx + PROTECT_HEAD)
		start = ctx + PROTECT_HEAD;
	if (nq - start < 2)
		return;
	if (!mpd_run_shuffle_range(conn, (unsigned)start, (unsigned)nq))
		check();
}

/* One-off full shuffle of the context (everything after the current
 * song's up-next block), triggered by capital X. Ignores the tail/
 * protect-head limits above since it's an explicit user action, not
 * the automatic recycle-shuffle. */
static void shuffle_now(void)
{
	if (g.pos < 0 || g.pos >= nq)
		return;
	int ctx = ctx_start(g.pos);
	if (nq - ctx < 2) {
		msg("nothing to shuffle");
		return;
	}
	if (mpd_run_shuffle_range(conn, (unsigned)ctx, (unsigned)nq))
		msg("shuffled");
	else
		check();
	refresh_all();
}

/* Make every up-next song (except the playing one) sit right after the
 * current song, keeping their relative order. */
static void reconcile(void)
{
	int cur = g.pos;
	if (cur < 0 || cur >= nq)
		return;
	int *sim = malloc((size_t)nq * sizeof *sim);
	int *ups = malloc((size_t)nq * sizeof *ups);
	if (!sim || !ups)
		abort();
	int nu = 0;
	for (int i = 0; i < nq; i++) {
		sim[i] = i;
		if (Q[i].prio > 0 && i != cur)
			ups[nu++] = i;
	}
	for (int k = 0; k < nu && conn; k++) {
		int p = -1, c = -1;
		for (int i = 0; i < nq; i++) {
			if (sim[i] == ups[k]) p = i;
			if (sim[i] == cur) c = i;
		}
		int dest = p < c ? c + k : c + 1 + k;
		if (p == dest)
			continue;
		if (!mpd_run_move(conn, (unsigned)p, (unsigned)dest)) {
			check();
			break;
		}
		int v = sim[p];
		if (p < dest)
			memmove(&sim[p], &sim[p + 1], (size_t)(dest - p) * sizeof *sim);
		else
			memmove(&sim[dest + 1], &sim[dest], (size_t)(p - dest) * sizeof *sim);
		sim[dest] = v;
	}
	free(sim);
	free(ups);
}

static bool rotate_to_top(bool shuf)
{
	int n = g.pos;
	if (!conn || n <= 0 || n >= nq)
		return false;
	bool ok = mpd_command_list_begin(conn, false);
	if (n <= nq - n)
		for (int i = 0; ok && i < n; i++)          /* top song -> bottom */
			ok = mpd_send_move(conn, 0, (unsigned)(nq - 1));
	else
		for (int i = 0; ok && i < nq - n; i++)     /* bottom song -> top */
			ok = mpd_send_move(conn, (unsigned)(nq - 1), 0);
	if (ok)
		ok = mpd_command_list_end(conn);
	if (ok)
		ok = mpd_response_finish(conn);
	if (!ok) {
		check();
		return false;
	}
	bool wrapped = n == nq - 1;
	refresh_all();
	if (shuf && !wrapped) {
		shuffle_tail(n);
		refresh_all();
	}
	return true;
}

/* Called once when the playing song changes from old_id to something else. */
static void handle_song_change(int old_id)
{
	if (!conn)
		return;
	int oi = find_id(old_id);
	/* the cursor follows its song across the reshuffle below - unless it
	 * was sitting on the playing song, in which case it follows playback */
	int sel_id = sel >= 0 && sel < nq ? (int)Q[sel].id : -1;
	bool sel_on_play = sel == g.pos || (oi >= 0 && sel == oi);

	if (g.consume) {
		/* MPD removes the played song itself, but up-next still needs
		 * to sit right after wherever we ended up - e.g. after jumping
		 * to an arbitrary track in the context rather than advancing
		 * naturally, up-next would otherwise be left behind. */
		refresh_all();
		reconcile();
		refresh_all();
		return;
	}
	/* An up-next song that has been played (or skipped) is consumed. */
	if (oi >= 0 && Q[oi].prio > 0 && !mpd_run_delete_id(conn, (unsigned)old_id))
		check();
	refresh_all();
	reconcile();
	refresh_all();
	/* Looping: the playing song heads the queue, whatever sat above it
	 * (the song that just finished, or everything skipped by a jump) is
	 * recycled to the bottom, shuffled if shuffle loop is on. */
	if (g.repeat && rotate_to_top(shuffle_loop)) {
		if (sel_on_play)
			sel = g.pos >= 0 ? g.pos : 0;
		else if (sel_id >= 0 && find_id(sel_id) >= 0)
			sel = find_id(sel_id);
	}
}

static bool in_upnext(const char *uri)
{
	for (int i = 0; i < nq; i++)
		if (Q[i].prio > 0 && i != g.pos && !strcmp(Q[i].uri, uri))
			return true;
	return false;
}

/* add a file to the up-next block (end of the block) */
static void add_up_next(const char *uri)
{
	if (in_upnext(uri)) {
		msg("already in up next");
		return;
	}
	int base = g.pos >= 0 ? g.pos + 1 : 0;
	while (base < nq && Q[base].prio > 0)
		base++;
	int id = mpd_run_add_id_to(conn, uri, (unsigned)base);
	if (id < 0) {
		check();
		return;
	}
	if (!mpd_run_prio_id(conn, UP_NEXT_PRIO, (unsigned)id))
		check();
	msg("queued next");
	refresh_all();
}

/* Replace the context (all prio-0 songs) with a directory or a playlist.
 * Up-next survives; the new context's first song starts playing. */
static void set_context(bool is_playlist, const char *arg)
{
	refresh_all();
	for (int i = nq - 1; i >= 0; i--)
		if (Q[i].prio == 0 && !mpd_run_delete_id(conn, Q[i].id))
			check();
	refresh_all();
	int keep = nq;
	bool ok = is_playlist ? mpd_run_load(conn, arg) : mpd_run_add(conn, arg);
	if (!ok) {
		check();
		return;
	}
	refresh_all();
	if (nq > keep) {
		mpd_run_move(conn, (unsigned)keep, 0);
		mpd_run_play_pos(conn, 0);
		refresh_all();
	}
}

/* Same idea, but for a single song rather than a whole directory/playlist -
 * used by the Directory pane's "play this one" action (Enter / A). */
static void set_context_single(const char *uri)
{
	refresh_all();
	for (int i = nq - 1; i >= 0; i--)
		if (Q[i].prio == 0 && !mpd_run_delete_id(conn, Q[i].id))
			check();
	refresh_all();
	int keep = nq;
	int id = mpd_run_add_id(conn, uri);
	if (id < 0) {
		check();
		return;
	}
	refresh_all();
	if (nq > keep) {
		int idx = find_id(id);
		if (idx > 0)
			mpd_run_move(conn, (unsigned)idx, (unsigned)keep);
		mpd_run_play_id(conn, (unsigned)id);
		refresh_all();
	}
}
