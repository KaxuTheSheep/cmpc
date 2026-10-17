/* TODO RPLACE CAVA WITH A SMALLER VERSION */


/* ====================================================================
 * Visualiser (cava, raw ASCII over a pipe)
 * ==================================================================== */
static int vis_nbars(int w)
{
	int n = (w + VIS_BAR_GAP) / (VIS_BAR_W + VIS_BAR_GAP);
	if (n > 256) n = 256;
	return n & ~1; /* cava gives an even count for stereo */
}

static void vis_stop(void)
{
	if (vis_pid > 0) {
		kill(vis_pid, SIGKILL);
		while (waitpid(vis_pid, NULL, 0) < 0 && errno == EINTR)
			;
	}
	vis_pid = 0;
	if (vis_fd >= 0)
		close(vis_fd);
	vis_fd = -1;
	if (vis_cfg[0])
		unlink(vis_cfg);
	vis_cfg[0] = 0;
	vis_blen = 0;
	vis_nval = 0;
}

/* The fifo cava should read (MPD-only audio), or NULL for global audio. */
static const char *vis_fifo_path(void)
{
	const char *e = getenv("CMPC_VIS_FIFO");
	const char *p = e ? e : VIS_MPD_FIFO;
	return p && *p ? p : NULL;
}

/* Switch on any MPD output of type "fifo" that is defined but disabled. */
static void vis_enable_fifo_output(void)
{
	if (!conn || !mpd_send_outputs(conn))
		return;
	unsigned ids[8];
	int n = 0;
	struct mpd_output *o;
	while ((o = mpd_recv_output(conn))) {
		const char *pl = mpd_output_get_plugin(o);
		if (pl && !strcmp(pl, "fifo") && !mpd_output_get_enabled(o) && n < 8)
			ids[n++] = mpd_output_get_id(o);
		mpd_output_free(o);
	}
	mpd_response_finish(conn);
	check();
	for (int i = 0; i < n; i++)
		mpd_run_enable_output(conn, ids[i]);
	if (n)
		check();
}

static void vis_fail(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(vis_msg, sizeof vis_msg, fmt, ap);
	va_end(ap);
	vis_failed = true;
	vis_next_try = now_ms() + 2000;
}

static void vis_start(int nb)
{
	vis_bars = nb;
	vis_failed = false;
	vis_msg[0] = 0;
	const char *fifo = vis_fifo_path();
	if (fifo) {
		struct stat fst;
		vis_enable_fifo_output();
		if (stat(fifo, &fst) != 0 || !S_ISFIFO(fst.st_mode)) {
			vis_fail("no MPD fifo at %s - add the fifo output to mpd.conf (see config.h)", fifo);
			return;
		}
	}
	snprintf(vis_cfg, sizeof vis_cfg, "/tmp/cmpc-cava.%d.conf", (int)getpid());
	FILE *f = fopen(vis_cfg, "w");
	if (!f) {
		vis_cfg[0] = 0;
		vis_failed = true;
		snprintf(vis_msg, sizeof vis_msg, "cava: can't write its config");
		return;
	}
	fprintf(f, "[general]\nbars = %d\nframerate = %d\n", nb, VIS_FPS);
	if (fifo) /* must match the fifo output's format in mpd.conf */
		fprintf(f, "[input]\nmethod = fifo\nsource = %s\n"
			   "sample_rate = 44100\nsample_bits = 16\nchannels = 2\n", fifo);
	else
		fprintf(f, "[input]\nmethod = %s\nsource = %s\n", CAVA_METHOD, CAVA_SOURCE);
	fprintf(f, "[output]\nmethod = raw\nraw_target = /dev/stdout\n"
		   "data_format = ascii\nascii_max_range = 1000\n"
		   "bar_delimiter = 59\nframe_delimiter = 10\n");
	fclose(f);

	int pf[2];
	if (pipe(pf) != 0) {
		vis_failed = true;
		snprintf(vis_msg, sizeof vis_msg, "cava: pipe failed");
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(pf[0]); close(pf[1]);
		vis_failed = true;
		snprintf(vis_msg, sizeof vis_msg, "cava: fork failed");
		return;
	}
	if (pid == 0) {
		int dn = open("/dev/null", O_RDWR);
		dup2(pf[1], 1);
		if (dn >= 0) { dup2(dn, 0); dup2(dn, 2); }
		for (int fd = 3; fd < 256; fd++)
			close(fd);
		execlp(CAVA_CMD, "cava", "-p", vis_cfg, (char *)NULL);
		_exit(127);
	}
	close(pf[1]);
	fcntl(pf[0], F_SETFL, fcntl(pf[0], F_GETFL) | O_NONBLOCK);
	fcntl(pf[0], F_SETFD, FD_CLOEXEC);
	vis_pid = pid;
	vis_fd = pf[0];
}

/* Parses one "a;b;c;...;" frame into vis_val. */
static void vis_parse(const char *s, const char *e)
{
	int n = 0;
	while (s < e && n < 256) {
		char *end;
		long v = strtol(s, &end, 10);
		if (end == s) {
			s++;
			continue;
		}
		vis_val[n++] = v < 0 ? 0 : v > 1000 ? 1000 : (int)v;
		s = end;
	}
	if (n)
		vis_nval = n;
}

/* last '\n' in buf[0..n), or NULL (the libc version is GNU-only) */
static char *last_nl(char *buf, size_t n)
{
	while (n--)
		if (buf[n] == '\n')
			return buf + n;
	return NULL;
}

/* Drains the pipe; keeps only the newest complete frame. */
static void vis_read(void)
{
	if (vis_fd < 0)
		return;
	for (;;) {
		ssize_t r = read(vis_fd, vis_buf + vis_blen, sizeof vis_buf - 1 - vis_blen);
		if (r > 0) {
			vis_blen += (size_t)r;
			if (vis_blen >= sizeof vis_buf - 1)
				break; /* parse what we have; a frame can't be this big */
			continue;
		}
		if (r < 0 && errno == EINTR)
			continue;
		if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			break;
		int st = 0, rc = -1;
		for (int tries = 0; vis_pid > 0 && tries < 20; tries++) {
			if (waitpid(vis_pid, &st, WNOHANG) == vis_pid) {
				vis_pid = 0;
				rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
				break;
			}
			usleep(5000);
		}
		if (rc == 127)
			snprintf(vis_msg, sizeof vis_msg, "cava not found on PATH");
		else
			snprintf(vis_msg, sizeof vis_msg,
				 "cava exited (%d) - check CAVA_METHOD in config.h", rc);
		vis_stop();
		vis_failed = true;
		vis_next_try = now_ms() + 3000;
		return;
	}
	vis_buf[vis_blen] = 0;
	char *last = last_nl(vis_buf, vis_blen);
	if (last) {
		char *prev = last > vis_buf ? last_nl(vis_buf, (size_t)(last - vis_buf)) : NULL;
		vis_parse(prev ? prev + 1 : vis_buf, last);
		size_t rest = vis_blen - (size_t)(last + 1 - vis_buf);
		memmove(vis_buf, last + 1, rest);
		vis_blen = rest;
	} else if (vis_blen >= sizeof vis_buf - 1) {
		vis_blen = 0;
	}
}

/* Starts, restarts (width changed) or stops cava to match the box. */
static void vis_update(void)
{
	if (!VIS_ENABLE)
		return;
	int nb = vis_h >= 1 ? vis_nbars(vis_w) : 0;
	if (nb < 1) {
		if (vis_pid > 0)
			vis_stop();
		return;
	}
	if (vis_bars == nb && (vis_pid > 0 || vis_failed)) {
		if (!(vis_failed && vis_fifo_path() && now_ms() >= vis_next_try))
			return;
	}
	vis_stop();
	vis_start(nb);
}

static void draw_vis(void)
{
	if (!VIS_ENABLE) {
		put_str(vis_x + 1, vis_y, vis_w - 2, ST_DIM, "(visualiser disabled)");
		return;
	}
	if (vis_failed) {
		put_str(vis_x + 1, vis_y, vis_w - 2, ST_DIM, vis_msg);
		return;
	}
	if (!vis_nval) {
		put_str(vis_x + 1, vis_y, vis_w - 2, ST_DIM, "...");
		return;
	}
	int nb = vis_nbars(vis_w);
	if (vis_nval < nb)
		nb = vis_nval; /* mid-restart frame from the old width */
	int used = nb * VIS_BAR_W + (nb - 1) * VIS_BAR_GAP;
	int off = (vis_w - used) / 2;
	for (int i = 0; i < nb && i < vis_nval; i++) {
		int eighths = vis_val[i] * vis_h * 8 / 1000;
		int full = eighths / 8, part = eighths % 8;
		for (int k = 0; k < vis_h; k++) {
			uint32_t ch;
			if (k < full) ch = 0x2588;
			else if (k == full && part) ch = 0x2580 + (uint32_t)part;
			else break;
			struct style st = k * 3 < vis_h ? ST_VIS_LO
					: k * 3 < vis_h * 2 ? ST_VIS_MID : ST_VIS_HI;
			for (int j = 0; j < VIS_BAR_W; j++)
				put_cell(vis_x + off + i * (VIS_BAR_W + VIS_BAR_GAP) + j,
					 vis_y + vis_h - 1 - k, ch, st);
		}
	}
}

