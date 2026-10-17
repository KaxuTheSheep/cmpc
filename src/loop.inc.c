/* ====================================================================
 * Main loop
 * ==================================================================== */
static void tick(void)
{
	if (!conn) {
		if (now_ms() - last_connect_try < 1000 || !connect_mpd())
			return;
		refresh_all();
		last_id = g.id;
		return;
	}
	if (!fetch_status())
		return;
	int old = last_id;
	bool changed = g.id >= 0 && last_id >= 0 && g.id != last_id;
	if (g.id >= 0)
		last_id = g.id;
	if (g.qver != qver_loaded || changed)
		load_queue();
	if (changed)
		handle_song_change(old);
}

static void usage(void)
{
	puts("usage: cmpc [--all | --dir PATH | --playlist NAME]\n"
	     "  --all / --dir / --playlist replace the looping context, keeping up-next.\n"
	     "  Host/port come from MPD_HOST / MPD_PORT. Colours and behaviour: config.h");
}

