static bool nav_list_key(int k, int *cur, int count, int page, bool *pending_g)
{
	switch (k) {
	case 'g':
		if (*pending_g) { *cur = 0; *pending_g = false; }
		else *pending_g = true;
		return true;
	case 'j': case K_DOWN: if (*cur < count - 1) (*cur)++; return true;
	case 'k': case K_UP: if (*cur > 0) (*cur)--; return true;
	case 'G': case K_END: *cur = count ? count - 1 : 0; return true;
	case K_HOME: *cur = 0; return true;
	case K_PGDN: case 6: *cur += page; if (*cur >= count) *cur = count ? count - 1 : 0; return true;
	case K_PGUP: case 2: *cur -= page; if (*cur < 0) *cur = 0; return true;
	default: return false;
	}
}

static void sform_insert_key(int k)
{
	struct srow *r = &SR[fsel];
	if (k == K_ESC || k == 3 || k == '\r' || k == '\n') {
		s_insert = false;
	} else if (k == 9) {                       /* Tab: next text row, stay inserting */
		if (fsel + 1 < SF_COUNT) fsel++;
		else s_insert = false;
	} else if (k == 127 || k == 8) {           /* backspace, one whole UTF-8 character */
		while (r->len > 0) {
			unsigned char c = (unsigned char)r->text[--r->len];
			r->text[r->len] = 0;
			if ((c & 0xC0) != 0x80)
				break;
		}
	} else if (k == 21) {                      /* ^U */
		r->len = 0;
		r->text[0] = 0;
	} else if (k >= 32 && k != 127 && k < 256 && r->len < (int)sizeof r->text - 1) {
		r->text[r->len++] = (char)k;           /* bytes >= 128 are UTF-8 pieces, kept as-is */
		r->text[r->len] = 0;
	}
}

static void sform_clear_row(struct srow *r)
{
	r->len = 0;
	r->text[0] = 0;
}

static void sform_clear_all(void)
{
	for (int f = 0; f < SF_COUNT; f++)
		sform_clear_row(&SR[f]);
	free(sresults);
	sresults = NULL;
	nsresults = 0;
	ssel = stop = 0;
	s_ran = s_results_focus = false;
}

static void handle_key(int k)
{
	static bool pending_g;

	if (k == 12) { /* Ctrl-L: repaint everything and re-draw the cover */
		art_refresh();
		vis_failed = false; /* ...and let a dead cava try again */
		return;
	}

	if (meta_mode) {
		if (medit_state == MEDIT_CONFIRM) {
			if (k == 'y' || k == 'Y') {
				int rc = system(pending_cmd);
				load_meta_tags();
				if (rc != 0)
					msg("command exited %d - check the tag", rc);
				else
					msg("tag written");
				medit_state = MEDIT_NONE;
			} else if (k == 'n' || k == 'N' || k == K_ESC || k == 3) {
				medit_state = MEDIT_NONE;
				msg("cancelled");
			}
			return;
		}
		if (medit_state == MEDIT_VALUE || medit_state == MEDIT_NEWKEY || medit_state == MEDIT_NEWVAL) {
			if (k == '\r' || k == '\n') {
				if (medit_state == MEDIT_VALUE) {
					stage_write(META_TAGS[meta_sel].key, medit_buf);
				} else if (medit_state == MEDIT_NEWKEY) {
					if (!medit_len) { msg("tag name can't be empty"); return; }
					snprintf(medit_newkey, sizeof medit_newkey, "%s", medit_buf);
					medit_buf[0] = 0; medit_len = 0;
					medit_state = MEDIT_NEWVAL;
				} else {
					stage_write(medit_newkey, medit_buf);
				}
			} else if (k == K_ESC || k == 3) {
				medit_state = MEDIT_NONE;
			} else if (k == 127 || k == 8) {
				if (medit_len) medit_buf[--medit_len] = 0;
			} else if (k >= 32 && k < 127 && medit_len < (int)sizeof medit_buf - 1) {
				medit_buf[medit_len++] = (char)k;
				medit_buf[medit_len] = 0;
			}
			return;
		}
		int count = nmeta_tags + 1;
		switch (k) {
		case 'h': case K_ESC: case 3: meta_mode = false; return;
		case 'j': case K_DOWN: if (meta_sel < count - 1) meta_sel++; return;
		case 'k': case K_UP: if (meta_sel > 0) meta_sel--; return;
		case 'g': meta_sel = 0; return;
		case 'G': meta_sel = count ? count - 1 : 0; return;
		case 'i':
			if (!meta_supported) { msg("unsupported format"); return; }
			if (meta_sel == nmeta_tags) {
				medit_state = MEDIT_NEWKEY;
				medit_buf[0] = 0; medit_len = 0;
			} else {
				medit_state = MEDIT_VALUE;
				snprintf(medit_buf, sizeof medit_buf, "%s", META_TAGS[meta_sel].val);
				medit_len = (int)strlen(medit_buf);
			}
			return;
		default: return;
		}
	}

	if (playlist_overlay) {
		if (ov_naming) {
			if (k == '\r' || k == '\n') {
				if (ov_name_len) commit_to_playlist(ov_name);
				else msg("name can't be empty");
			} else if (k == K_ESC || k == 3) {
				ov_naming = false;
			} else if (k == 127 || k == 8) {
				if (ov_name_len) ov_name[--ov_name_len] = 0;
			} else if (k >= 32 && k < 127 && ov_name_len < (int)sizeof ov_name - 1) {
				ov_name[ov_name_len++] = (char)k;
				ov_name[ov_name_len] = 0;
			}
			return;
		}
		switch (k) {
		case K_ESC: case 3: playlist_overlay = false; return;
		case '\t':
			ov_focus ^= 1;
			return;
		case 'j': case K_DOWN:
			if (ov_focus == 0) { if (ov_sel_cur < nselected - 1) ov_sel_cur++; }
			else { if (ov_target_cur < nplaylists) ov_target_cur++; }
			return;
		case 'k': case K_UP:
			if (ov_focus == 0) { if (ov_sel_cur > 0) ov_sel_cur--; }
			else { if (ov_target_cur > 0) ov_target_cur--; }
			return;
		case 'J': if (ov_focus == 0) ov_move_sel(1); return;
		case 'K': if (ov_focus == 0) ov_move_sel(-1); return;
		case 'd':
			if (ov_focus == 0 && nselected) {
				free(SELECTED[ov_sel_cur]);
				memmove(&SELECTED[ov_sel_cur], &SELECTED[ov_sel_cur + 1],
					(size_t)(nselected - ov_sel_cur - 1) * sizeof *SELECTED);
				nselected--;
				if (ov_sel_cur >= nselected) ov_sel_cur = nselected ? nselected - 1 : 0;
				if (!nselected) playlist_overlay = false;
			}
			return;
		case '\r': case '\n':
			if (ov_focus == 0) { ov_focus = 1; return; }
			if (ov_target_cur == nplaylists) {
				ov_naming = true;
				ov_name[0] = 0;
				ov_name_len = 0;
				return;
			}
			commit_to_playlist(playlist_names[ov_target_cur]);
			return;
		default: return;
		}
	}

	if (search_mode) {
		if (k == '\r' || k == '\n') {
			search_mode = false;
			if (search_len) {
				if (current_pane == PANE_DIR) dsearch_next(dsel, 1);
				else search_next(sel, 1);
			}
		} else if (k == K_ESC || k == 3) {
			search_mode = false;
		} else if (k == 127 || k == 8) {
			if (search_len) search_q[--search_len] = 0;
		} else if (k >= 32 && k < 127 && search_len < (int)sizeof search_q - 1) {
			search_q[search_len++] = (char)k;
			search_q[search_len] = 0;
		}
		return;
	}

	if (s_insert && current_pane == PANE_SEARCH) {
		sform_insert_key(k);
		return;
	}

	if (k == 'q' || k == 3)
		sig_quit = 1;
	if (!conn)
		return;

	if (k >= '1' && k <= '6') {
		current_pane = k - '1';
		art_refresh();
		if (current_pane == PANE_DIR && !db_loaded)
			load_db();
		if (current_pane == PANE_ARTIST) {
			if (!db_loaded) load_db();
			if (!artists_built) { build_artist_list(); artists_built = true; }
		}
		if (current_pane == PANE_ALBUM) {
			if (!db_loaded) load_db();
			if (!albums_built) { build_balbum_list(); albums_built = true; }
		}
		if (current_pane == PANE_SEARCH && !db_loaded)
			load_db();
		if (current_pane == PANE_PLAYLIST)
			load_playlist_names();
		return;
	}

	if (k == 'P') {
		open_playlist_overlay();
		return;
	}

	if (current_pane == PANE_ARTIST) {
		int page = list_rows() > 0 ? list_rows() : 1;
		int *curp, *topp, count;
		switch (artist_level) {
		case ALVL_ARTIST: curp = &asel; topp = &atop; count = nartists; break;
		case ALVL_ALBUM: curp = &alsel; topp = &altop; count = nalbums; break;
		default: curp = &assel; topp = &astop; count = nsongs; break;
		}
		(void)topp;
		if (nav_list_key(k, curp, count, page, &pending_g))
			return;
		switch (k) {
		case 'l': case K_RIGHT:
			/* l: drill in at a group level, open tag editor at song level. */
			if (artist_level == ALVL_ARTIST) artist_enter_album();
			else if (artist_level == ALVL_ALBUM) artist_enter_song();
			else if (nsongs) open_metadata(DB[song_idx[assel]].uri);
			break;
		case '\r': case '\n':
			/* Enter: same drill-in at group levels, but plays at song level. */
			if (artist_level == ALVL_ARTIST) artist_enter_album();
			else if (artist_level == ALVL_ALBUM) artist_enter_song();
			else artist_play_now();
			break;
		case 'h': case K_LEFT:
			artist_back();
			return;
		case ' ':
			if (artist_level == ALVL_SONG) {
				if (nsongs) sel_toggle_one(DB[song_idx[assel]].uri);
			} else if (artist_level == ALVL_ALBUM) {
				if (nalbums) {
					int *idx = NULL;
					int n = collect_songs(cur_artist, album_list[alsel], &idx);
					sel_toggle_group_idx(idx, n);
					free(idx);
				}
			} else if (nartists) {
				int *idx = NULL;
				int n = collect_songs(artist_list[asel], NULL, &idx);
				sel_toggle_group_idx(idx, n);
				free(idx);
			}
			return;
		case 'a': artist_queue_next(); break;
		case 'A': artist_play_now(); break;
		default: return;
		}
		refresh_all();
		return;
	}

	if (current_pane == PANE_ALBUM) {
		int page = list_rows() > 0 ? list_rows() : 1;
		int *curp, count;
		if (album_level == BLVL_ALBUM) { curp = &bsel; count = nbalbums; }
		else { curp = &bssel; count = nbsongs; }
		if (nav_list_key(k, curp, count, page, &pending_g))
			return;
		switch (k) {
		case 'l': case K_RIGHT:
			if (album_level == BLVL_ALBUM) album_enter_song();
			else if (nbsongs) open_metadata(DB[bsong_idx[bssel]].uri);
			break;
		case '\r': case '\n':
			if (album_level == BLVL_ALBUM) album_enter_song();
			else album_play_all();
			break;
		case 'h': case K_LEFT:
			album_back();
			return;
		case ' ':
			if (album_level == BLVL_SONG) {
				if (nbsongs) sel_toggle_one(DB[bsong_idx[bssel]].uri);
			} else if (nbalbums) {
				int *idx = NULL;
				int n = collect_songs(NULL, balbum_list[bsel], &idx);
				sel_toggle_group_idx(idx, n);
				free(idx);
			}
			return;
		case 'a': album_queue_next(); break;
		case 'A': album_play_all(); break;
		default: return;
		}
		refresh_all();
		return;
	}

	if (current_pane == PANE_PLAYLIST) {
		int page = list_rows() > 0 ? list_rows() : 1;
		int *curp, count;
		if (playlist_level == PLVL_LIST) { curp = &psel; count = nplaylists; }
		else { curp = &plsel; count = nplsongs; }
		if (nav_list_key(k, curp, count, page, &pending_g))
			return;
		switch (k) {
		case 'l': case K_RIGHT:
			if (playlist_level == PLVL_LIST) playlist_enter_songs();
			else if (nplsongs) open_metadata(PLSONGS[plsel].uri);
			break;
		case '\r': case '\n':
			if (playlist_level == PLVL_LIST) playlist_enter_songs();
			else if (nplsongs) set_context_single(PLSONGS[plsel].uri);
			break;
		case 'h': case K_LEFT:
			playlist_back();
			return;
		case ' ':
			if (playlist_level == PLVL_SONGS) {
				if (nplsongs) sel_toggle_one(PLSONGS[plsel].uri);
			} else if (nplaylists) {
				playlist_select_all(playlist_names[psel]);
			}
			return;
		case 'a':
			if (playlist_level == PLVL_SONGS) {
				if (nplsongs) add_up_next(PLSONGS[plsel].uri);
			} else if (nplaylists) {
				playlist_queue_next(playlist_names[psel]);
			}
			break;
		case 'A':
			if (playlist_level == PLVL_SONGS) {
				if (nplsongs) set_context_single(PLSONGS[plsel].uri);
			} else if (nplaylists) {
				playlist_play_all(playlist_names[psel]);
			}
			break;
		default: return;
		}
		refresh_all();
		return;
	}

	if (current_pane == PANE_SEARCH) {
		if (s_results_focus) {
			int page = sresult_rows();
			if (nav_list_key(k, &ssel, nsresults, page, &pending_g))
				return;
			switch (k) {
			case '/': case 9: case K_ESC: case 'h': case K_LEFT:
				s_results_focus = false;      /* back to the form */
				return;
			case 'i':
				s_results_focus = false;
				if (fsel < SF_COUNT) s_insert = true;
				return;
			case ' ': if (nsresults) sel_toggle_one(DB[sresults[ssel]].uri); return;
			case 'l': if (nsresults) open_metadata(DB[sresults[ssel]].uri); break;
			case 'a': if (nsresults) add_up_next(DB[sresults[ssel]].uri); break;
			case '\r': case '\n': case 'A':
				if (nsresults) set_context_single(DB[sresults[ssel]].uri);
				break;
			default: return;
			}
			refresh_all();
			return;
		}

		/* form, normal mode */
		if (nav_list_key(k, &fsel, SF_COUNT + 2, SF_COUNT + 2, &pending_g))
			return;
		struct srow *r = fsel < SF_COUNT ? &SR[fsel] : NULL;
		bool text = r && SFIELD[fsel].kind == SK_TEXT;
		switch (k) {
		case 'i':
			if (r) s_insert = true;
			return;
		case '\r': case '\n':
			if (fsel == SF_COUNT) run_search();
			else if (fsel == SF_COUNT + 1) sform_clear_all();
			else s_insert = true;
			return;
		case '/': run_search(); return;
		case 9: if (nsresults) s_results_focus = true; return;
		case 'o':
			if (r) { r->join = (r->join + 1) % 3; msg("%s: %s", SFIELD[fsel].label, r->join == J_AND ? "AND" : r->join == J_OR ? "OR" : "NOT"); }
			return;
		case 'c':
			if (text) { r->cs = !r->cs; msg("%s: %s", SFIELD[fsel].label, r->cs ? "case sensitive" : "ignore case"); }
			else if (r) msg("case doesn't apply to %s", SFIELD[fsel].label);
			return;
		case 'f':
			if (text) {
				r->fuzzy = !r->fuzzy;
				if (r->fuzzy) r->exact = false;
				msg("%s: fuzzy %s", SFIELD[fsel].label, r->fuzzy ? "on" : "off");
			} else if (r) msg("fuzzy doesn't apply to %s", SFIELD[fsel].label);
			return;
		case 'e':
			if (text) {
				r->exact = !r->exact;
				if (r->exact) r->fuzzy = false;
				msg("%s: exact %s", SFIELD[fsel].label, r->exact ? "on (whole value, or one whole artist)" : "off (contains)");
			} else if (r) msg("exact doesn't apply to %s", SFIELD[fsel].label);
			return;
		case 'x': if (r) sform_clear_row(r); return;
		case 'X':
			for (int f = 0; f < SF_COUNT; f++) sform_clear_row(&SR[f]);
			return;
		default: return;
		}
	}

	if (current_pane == PANE_DIR) {
		int page = list_rows() > 0 ? list_rows() : 1;
		if (nav_list_key(k, &dsel, ndb, page, &pending_g))
			return;
		switch (k) {
		case '/': search_mode = true; search_len = 0; search_q[0] = 0; return;
		case 'n': dsearch_next(dsel, 1); return;
		case 'N': dsearch_next(dsel, -1); return;
		case ' ': if (ndb) sel_toggle_one(DB[dsel].uri); return;
		case 'a': if (ndb) add_up_next(DB[dsel].uri); break;
		case '\r': case '\n': if (ndb) set_context_single(DB[dsel].uri); break;
		case 'l': if (ndb) open_metadata(DB[dsel].uri); break;
		case 'A': play_all_directory(dsel); break;
		default: return;
		}
		refresh_all();
		return;
	}
	int page = list_rows() > 0 ? list_rows() : 1;
	if (nav_list_key(k, &sel, nq, page, &pending_g))
		return;

	switch (k) {
	case 4: sel += page / 2; if (sel >= nq) sel = nq ? nq - 1 : 0; return;
	case 21: sel -= page / 2; if (sel < 0) sel = 0; return;
	case 'C': if (g.pos >= 0) sel = g.pos; return;
	case '/': search_mode = true; search_len = 0; search_q[0] = 0; return;
	case ' ': if (nq) sel_toggle_one(Q[sel].uri); return;
	case 'l': if (nq) open_metadata(Q[sel].uri); return;
	case 'n': search_next(sel, 1); return;
	case 'N': search_next(sel, -1); return;
	default: break;
	}

	switch (k) {
	case '\r': case '\n':
		if (nq && !mpd_run_play_id(conn, Q[sel].id)) check();
		break;
	case 'a':
		if (nq) add_up_next(Q[sel].uri);
		break;
	case 'd':
		if (nq && !mpd_run_delete_id(conn, Q[sel].id)) check();
		break;
	case 'D':
		for (int i = nq - 1; i >= 0; i--)
			if (Q[i].prio > 0 && i != g.pos)
				mpd_run_delete_id(conn, Q[i].id);
		check();
		break;
	case 'J': move_sel(1); return;
	case 'K': move_sel(-1); return;
	case 'p': toggle_pause(); break;
	case 's': if (!mpd_run_stop(conn)) check(); break;
	case '>': if (!mpd_run_next(conn)) check(); break;
	case '<': if (!mpd_run_previous(conn)) check(); break;
	case '.': if (!mpd_run_change_volume(conn, VOLUME_STEP)) check(); break;
	case ',': if (!mpd_run_change_volume(conn, -VOLUME_STEP)) check(); break;
	case 'f': case 'b': {
		char a[16];
		snprintf(a, sizeof a, "%c%d", k == 'f' ? '+' : '-', SEEK_STEP);
		if (mpd_send_command(conn, "seekcur", a, NULL))
			mpd_response_finish(conn);
		check();
		break;
	}
	case 'z': if (!mpd_run_repeat(conn, !g.repeat)) check(); break;
	case 'c': if (!mpd_run_consume(conn, !g.consume)) check(); break;
	case 'v': if (!mpd_run_single(conn, !g.single)) check(); break;
	case 'x':
		shuffle_loop = !shuffle_loop;
		msg("shuffle loop %s", shuffle_loop ? "on" : "off");
		break;
	case 'X':
		shuffle_now();
		break;
	default: return;
	}
	refresh_all();
}

