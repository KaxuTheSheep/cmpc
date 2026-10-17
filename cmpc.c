#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <locale.h>
#include <mpd/client.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "config.h"

#include "src/state.inc.c"
#include "src/canvas.inc.c"
#include "src/term.inc.c"
#include "src/mpd.inc.c"
#include "src/queue.inc.c"
#include "src/browse.inc.c"
#include "src/playlist.inc.c"
#include "src/meta.inc.c"
#include "src/format.inc.c"
#include "src/draw.inc.c"
#include "src/vis.inc.c"
#include "src/layout.inc.c"
#include "src/search.inc.c"
#include "src/art.inc.c"
#include "src/keys.inc.c"
#include "src/loop.inc.c"
#include "src/sixel.inc.c"

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "--sixel"))
		return sixel_cmd(argc, argv);
	setlocale(LC_ALL, "");

	const char *opt_dir = NULL, *opt_pl = NULL;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--all")) opt_dir = "/";
		else if (!strcmp(argv[i], "--dir") && i + 1 < argc) opt_dir = argv[++i];
		else if (!strcmp(argv[i], "--playlist") && i + 1 < argc) opt_pl = argv[++i];
		else { usage(); return !strcmp(argv[i], "--help") ? 0 : 1; }
	}

	if (!connect_mpd()) {
		fprintf(stderr, "cmpc: cannot connect to MPD: %s\n", msgbuf);
		return 1;
	}
	fetch_status();
	if (START_MPD_REPEAT && !g.repeat) mpd_run_repeat(conn, true);
	if (START_MPD_RANDOM_OFF && g.random) mpd_run_random(conn, false);
	if (opt_dir) set_context(false, opt_dir);
	else if (opt_pl) set_context(true, opt_pl);
	refresh_all();
	last_id = g.id;
	if (g.repeat && g.pos > 0)
		rotate_to_top(false); /* start with the playing song on top */
	sel = g.pos >= 0 ? g.pos : 0;

	term_init();
	term_size();
	if (ART_ENABLE) {
		query_cell_pixels();
		query_sixel_support();
	}

	long last_tick = 0;
	while (!sig_quit) {
		if (sig_winch) {
			sig_winch = 0;
			term_size();
			/* new size: old sixel is gone/misplaced, cell pixel size may
			 * have changed (font zoom), so rebuild the art from scratch */
			art_shown = false;
			art_cache_set = false;
			if (ART_ENABLE)
				query_cell_pixels();
		}
		draw();
		flush();
		art_autorefresh();
		art_update();
		vis_update();
		struct pollfd p[2] = {{STDIN_FILENO, POLLIN, 0}, {vis_fd, POLLIN, 0}};
		int r = poll(p, vis_fd >= 0 ? 2 : 1, art_busy() || art_refresh_due ? ART_POLL_MS : TICK_MS);
		bool key = r > 0 && (p[0].revents & POLLIN);
		if (r > 0 && vis_fd >= 0 && (p[1].revents & (POLLIN | POLLHUP | POLLERR)))
			vis_read();
		if (key) {
			int k;
			while ((k = read_key()) != K_NONE)
				handle_key(k);
		}
		if (key || now_ms() - last_tick >= TICK_MS) {
			tick();
			last_tick = now_ms();
		}
	}

	term_restore();
	free_queue();
	free(Q);
	free_db();
	free(DB);
	free_artist_browser();
	free_album_browser();
	free_selection();
	free_playlist_names();
	free_metadata();
	free_search();
	free_plsongs();
	free(art_buf);
	art_job_kill();
	drop_conn();
	return 0;
}
