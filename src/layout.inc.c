static void draw(void)
{
	canvas_clear();
	art_box_h = 0; /* stays 0 unless the art box is actually drawn below */
	vis_w = vis_h = 0; /* ditto for the visualiser */
	if (playlist_overlay) {
		draw_playlist_overlay();
		return;
	}
	if (W < 58 || H < 14) {
		put_str(0, 0, W, ST_TEXT, "terminal too small");
		return;
	}
	int mainh = H;
	int lw = W * LEFT_PCT / 100;
	if (lw < LEFT_MIN) lw = LEFT_MIN;
	if (lw > W / 2) lw = W / 2;
	right_x0 = lw;

	int ih = NOWPLAY_H;
	int vh_min = VIS_H < 3 ? 3 : VIS_H;
	int ah = ((lw - 2) * px_per_col + px_per_row - 1) / px_per_row + 3;
	if (mainh - ih - ah < vh_min)
		ah = mainh - ih - vh_min; /* short terminal: art gives way, letterboxed */
	if (ah < ART_MIN) ah = ART_MIN;
	int vh = mainh - ih - ah;
	if (vh < 3) vh = 3;

	box(0, 0, lw, ih, ST_BORDER_HI, " current ", NULL);
	draw_winamp_bar(2, 1, lw - 4);
	if (vh >= 3) {
		box(0, ih, lw, vh, ST_BORDER, " visualiser ", NULL);
		if (vh > 2) {
			vis_x = 1; vis_y = ih + 1; vis_w = lw - 2; vis_h = vh - 2;
			draw_vis();
		}
	}
	if (ah >= 3) {
		char at[260];
		if (ART_ENABLE && art_status[0])
			snprintf(at, sizeof at, " art: %s ", art_status);
		else
			snprintf(at, sizeof at, " art ");
		box(0, ih + vh, lw, ah, ST_BORDER, at, NULL);
		art_box_x = 0; art_box_y = ih + vh; art_box_w = lw; art_box_h = ah;
		if (!ART_ENABLE)
			put_str(2, ih + vh + 1, lw - 4, ST_DIM, "(cover art disabled)");
		else if (art_none)
			put_str(2, ih + vh + 1, lw - 4, ST_DIM, art_status[0] ? art_status : "(no cover found)");
	}
	if (meta_mode) {
		draw_metadata_pane(lw, W - lw, mainh);
		return;
	}
	switch (current_pane) {
	case PANE_QUEUE: draw_queue(lw, W - lw, mainh); break;
	case PANE_DIR: draw_dir(lw, W - lw, mainh); break;
	case PANE_ARTIST: draw_artist_pane(lw, W - lw, mainh); break;
	case PANE_ALBUM: draw_album_pane(lw, W - lw, mainh); break;
	case PANE_PLAYLIST: draw_playlist_pane(lw, W - lw, mainh); break;
	case PANE_SEARCH: draw_search_pane(lw, W - lw, mainh); break;
	}
}

