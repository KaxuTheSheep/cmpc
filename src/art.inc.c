/* ---- cover art: extract -> encode -> blit ------------------------- */

static void art_log_reset(void)
{
	FILE *f = fopen(ART_LOG, "w");
	if (f)
		fclose(f);
}

static void art_log(const char *fmt, ...)
{
	FILE *f = fopen(ART_LOG, "a");
	if (!f)
		return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fclose(f);
}

static void set_status(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(art_status, sizeof art_status, fmt, ap);
	va_end(ap);
}

/* ---- background cover build ---------------------------------------- */
static struct {
	pid_t pid;                 /* > 0 while the child runs */
	bool img_loaded;
	char png[112], six[112], sixt[112], meta[112], metat[112];
} job;
static unsigned job_serial;

static bool art_busy(void) { return job.pid > 0; }

static void art_job_cleanup(void)
{
	if (job.png[0]) unlink(job.png);
	if (job.six[0]) unlink(job.six);
	if (job.sixt[0]) unlink(job.sixt);
	if (job.meta[0]) unlink(job.meta);
	if (job.metat[0]) unlink(job.metat);
	memset(&job, 0, sizeof job);
}

static void art_job_kill(void)
{
	if (job.pid > 0) {
		kill(-job.pid, SIGKILL); /* child leads its own process group */
		kill(job.pid, SIGKILL);
		while (waitpid(job.pid, NULL, 0) < 0 && errno == EINTR)
			;
	}
	art_job_cleanup();
}

static bool sh_add(char *sh, size_t cap, size_t *o, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(sh + *o, cap - *o, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= cap - *o)
		return false;
	*o += (size_t)n;
	return true;
}

static const char *self_exe(void)
{
	static char p[512];
	static bool init;
	if (!init) {
		init = true;
		ssize_t n = readlink("/proc/self/exe", p, sizeof p - 1);
		if (n > 0) {
			p[n] = 0;
			if (access(p, X_OK) != 0)
				p[0] = 0;
		} else {
			p[0] = 0;
		}
	}
	return p[0] ? p : NULL;
}

static int art_colours(void)
{
	const char *e = getenv("CMPC_SIXEL_COLOURS");
	int v = e && *e ? atoi(e) : ART_SIXEL_COLOURS;
	return v < 0 ? 0 : v > 1024 ? 1024 : v == 1 ? 2 : v;
}

static int art_dither(void)
{
	const char *e = getenv("CMPC_SIXEL_DITHER");
	return e && *e ? atoi(e) != 0 : ART_SIXEL_DITHER != 0;
}

static int art_cap(void)
{
	const char *e = getenv("CMPC_ART_MAX");
	int v = e ? atoi(e) : 0;
	return v >= 64 ? v : ART_MAX_PX;
}

static long tmux_sixel_limit(void)
{
	FILE *p = popen("tmux show-options -sv input-buffer-size </dev/null 2>/dev/null", "r");
	if (!p)
		return 0;
	char line[64] = "";
	if (!fgets(line, sizeof line, p))
		line[0] = 0;
	pclose(p);
	long v = atol(line);
	if (v < 1048576)
		return 0;
	long pow2 = 1048576;
	while (pow2 <= LONG_MAX / 4 && pow2 * 2 <= v)
		pow2 *= 2;
	return pow2 - pow2 / 20;
}

static long art_budget(void)
{
	const char *e = getenv("CMPC_ART_BYTES");
	if (e && *e)
		return atol(e) < 0 ? 0 : atol(e);
	if (!getenv("TMUX"))
		return 0;
	static long cached = -1;
	if (cached < 0) {
		cached = tmux_sixel_limit();
		if (cached <= 0)
			cached = ART_TMUX_BYTES;
	}
	return cached;
}

static const char *art_sixel_args(void)
{
	const char *e = getenv("CMPC_SIXEL_ARGS");
	return e && *e ? e : ART_SIXEL_ARGS;
}

static void art_start(const char *uri, int bw, int bh)
{
	art_job_kill();
	vis_stop();
	free(art_buf);
	art_buf = NULL;
	art_len = 0;
	art_none = true;
	art_cache_set = true;
	snprintf(art_cache_uri, sizeof art_cache_uri, "%s", uri);
	art_cache_w = bw;
	art_cache_h = bh;
	art_log_reset();
	if (!*uri) {
		set_status("nothing playing");
		return;
	}
	int img_rows = bh > 4 ? bh - 1 : bh;
	int pw = bw * px_per_col, ph = img_rows * px_per_row;
	int cap = art_cap();
	if (pw > cap) pw = cap;
	if (ph > cap) ph = cap;
	ph -= ph % 6; /* sixel bands are 6px tall */
	art_img_w = art_target_w = pw;
	art_img_h = art_target_h = ph;
	int ncol = art_colours();
	const char *self = ncol > 0 ? self_exe() : NULL;
	bool builtin = self != NULL; /* else fall back to img2sixel */
	if (builtin)
		snprintf(art_enc_label, sizeof art_enc_label, "%dc%s", ncol, art_dither() ? "+d" : "");
	else
		snprintf(art_enc_label, sizeof art_enc_label, "img2sixel");
	art_log("uri: %s\ninterior: %dx%d cells, cell: %dx%d px, target: %dx%d px (cap %d)\n"
		"encoder: %s%s%s  byte budget: %ld (0 = unlimited)\nDA1 reply: %s (sixel %s)\n",
		uri, bw, bh, px_per_col, px_per_row, pw, ph, cap,
		builtin ? "built-in " : "img2sixel ", art_enc_label, builtin ? "" : art_sixel_args(),
		art_budget(), da1_raw,
		da1_sixel < 0 ? "unknown" : da1_sixel ? "yes" : "NO");
	if (pw < 12 || ph < 12) {
		set_status("box too small (%dx%d px)", pw, ph);
		return;
	}

	char song[800];
	snprintf(song, sizeof song, "%s/%s", MUSIC_DIR, uri);
	struct stat sst;
	if (stat(song, &sst) != 0) {
		art_log("stat failed: %s\n", song);
		set_status("file not found - check MUSIC_DIR in config.h");
		return;
	}

	char base[96];
	snprintf(base, sizeof base, "%s.%d.%u", ART_TMP, (int)getpid(), ++job_serial);
	snprintf(job.png, sizeof job.png, "%s.png", base);
	snprintf(job.six, sizeof job.six, "%s.six", base);
	snprintf(job.sixt, sizeof job.sixt, "%s.six.tmp", base);
	snprintf(job.meta, sizeof job.meta, "%s.meta", base);
	snprintf(job.metat, sizeof job.metat, "%s.meta.tmp", base);

	char sh[6000], q[1000], qp[300], qs[300], qst[300], qm[300], qmt[300];
	size_t o = 0;
	bool ok = true;
	shquote_into(qp, sizeof qp, job.png);
	shquote_into(qs, sizeof qs, job.six);
	shquote_into(qst, sizeof qst, job.sixt);
	shquote_into(qm, sizeof qm, job.meta);
	shquote_into(qmt, sizeof qmt, job.metat);

	ok &= sh_add(sh, sizeof sh, &o,
		     "command -v ffmpeg >/dev/null || exit 12\n");
	if (!builtin)
		ok &= sh_add(sh, sizeof sh, &o, "command -v img2sixel >/dev/null || exit 13\n");
	ok &= sh_add(sh, sizeof sh, &o, "ok=\nfor src in ");
	shquote_into(q, sizeof q, song);
	ok &= sh_add(sh, sizeof sh, &o, "%s ", q);
	char dir[800];
	snprintf(dir, sizeof dir, "%s", song);
	char *sl = strrchr(dir, '/');
	if (sl) {
		*sl = 0;
		static const char *const names[] = ART_FALLBACK_NAMES;
		for (int i = 0; names[i]; i++) {
			char cand[900];
			struct stat st;
			snprintf(cand, sizeof cand, "%s/%s", dir, names[i]);
			if (stat(cand, &st) != 0 || !S_ISREG(st.st_mode))
				continue;
			shquote_into(q, sizeof q, cand);
			ok &= sh_add(sh, sizeof sh, &o, "%s ", q);
		}
	}
	char step[1400], qexe[600];
	if (builtin) {
		shquote_into(qexe, sizeof qexe, self);
		snprintf(step, sizeof step,
			 "  W=$(( %d * $pct / 100 )); H=$(( %d * $pct / 100 ))\n"
			 "  ffmpeg -nostdin -y -v error -i %s -vf \"scale=$W:$H:flags=lanczos\" "
			 "-f rawvideo -pix_fmt rgb24 - | %s --sixel $W $H %d %d > %s || exit 11\n",
			 pw, ph, qp, qexe, ncol, art_dither() ? 1 : 0, qst);
	} else {
		snprintf(step, sizeof step,
			 "  w=; [ $pct -ne 100 ] && w=\"-w $(( %d * $pct / 100 ))\"\n"
			 "  img2sixel %s $w -r lanczos3 %s > %s || exit 11\n",
			 pw, art_sixel_args(), qp, qst);
	}
	ok &= sh_add(sh, sizeof sh, &o,
		     "; do\n"
		     "  if ffmpeg -nostdin -y -v error -i \"$src\" -an -frames:v 1 -vf "
		     "'format=rgb24,scale=%d:%d:force_original_aspect_ratio=decrease:flags=lanczos,"
		     "pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=%s' %s; then ok=1; break; fi\n"
		     "done\n"
		     "[ -n \"$ok\" ] || exit 10\n"
		     "pct=100\nn=0\n"
		     "while :; do\n"
		     "%s"
		     "  [ -s %s ] || exit 11\n"
		     "  [ %ld -le 0 ] && break\n"
		     "  sz=$(wc -c < %s)\n"
		     "  [ $sz -le %ld ] && break\n"
		     "  n=$((n+1)); [ $n -ge 6 ] && exit 14\n"
		     "  p2=$(awk -v b=%ld -v s=$sz -v p=$pct "
		     "'BEGIN{q=int(p*sqrt(b/s)*0.98); if (q>=p) q=p-3; print q}') || p2=$((pct*85/100))\n"
		     "  pct=$p2\n"
		     "  [ \"$pct\" -ge 20 ] 2>/dev/null || exit 14\n"
		     "done\n"
		     "mv %s %s\n"
		     "ffprobe -v error -select_streams v:0 -show_entries stream=width,height "
		     "-of csv=s=x:p=0 \"$src\" > %s 2>/dev/null && mv %s %s\n"
		     "exit 0\n",
		     pw, ph, pw, ph, ART_PAD_COLOR, qp, step, qst, art_budget(), qst, art_budget(),
		     art_budget(), qst, qs, qmt, qmt, qm);
	if (!ok) {
		art_job_cleanup();
		set_status("path too long");
		return;
	}
	art_log("script:\n%s", sh);

	pid_t pid = fork();
	if (pid < 0) {
		art_job_cleanup();
		set_status("fork failed");
		return;
	}
	if (pid == 0) {
		setpgid(0, 0);
		int dn = open("/dev/null", O_RDWR);
		int lg = open(ART_LOG, O_WRONLY | O_APPEND | O_CREAT, 0644);
		if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); }
		if (lg >= 0) dup2(lg, 2);
		for (int fd = 3; fd < 256; fd++)
			close(fd);
		execl("/bin/sh", "sh", "-c", sh, (char *)NULL);
		_exit(127);
	}
	setpgid(pid, pid);
	job.pid = pid;
	job.img_loaded = false;
	set_status("loading...");
}

/* Reads the finished sixel into art_buf. False if it isn't there yet. */
static bool art_load_six(void)
{
	FILE *f = fopen(job.six, "rb");
	if (!f)
		return false;
	struct stat st;
	if (fstat(fileno(f), &st) != 0 || st.st_size < 16) {
		fclose(f);
		return false;
	}
	size_t len = (size_t)st.st_size;
	char *buf = malloc(len);
	if (!buf || fread(buf, 1, len, f) != len) {
		free(buf);
		fclose(f);
		return false;
	}
	fclose(f);
	for (size_t i = len; i >= 2; i--) {
		if (buf[i - 2] == 0x1b && buf[i - 1] == '\\') {
			len = i;
			break;
		}
	}
	{ /* the ladder may have shrunk it: take the real size from the
	   * raster attributes, ESC P .. q " Pan ; Pad ; Ph(width) ; Pv */
		char head[96];
		size_t hn = len < sizeof head - 1 ? len : sizeof head - 1;
		memcpy(head, buf, hn);
		head[hn] = 0;
		char *q = strchr(head, 'q');
		int a, b, w, hh;
		if (q && sscanf(q + 1, "\"%d;%d;%d;%d", &a, &b, &w, &hh) == 4 && w > 0 && hh > 0) {
			art_img_w = w;
			art_img_h = hh;
		}
	}
	free(art_buf);
	art_buf = buf;
	art_len = len;
	art_none = false;
	rename(job.six, ART_DUMP); /* keep it for `cat` debugging; cheap */
	job.six[0] = 0;
	job.img_loaded = true;
	art_log("sixel loaded: %zu bytes\n", len);
	return true;
}

static void art_refresh(void)
{
	art_shown = false;
	art_self_refresh = true; /* the repaint this causes must not re-trigger us */
	art_refresh_due = 0;
	invalidate_all();
}

static int art_refresh_rows(void)
{
	const char *e = getenv("CMPC_ART_REFRESH_ROWS");
	return e && *e ? atoi(e) : ART_REFRESH_ROWS;
}

static void art_wipe(void);

static void art_autorefresh(void)
{
	if (art_self_refresh) {
		art_self_refresh = false; /* that flush was our own full repaint */
		return;
	}
	long now = now_ms();
	int need = art_refresh_rows();
	if (need > 0 && art_shown && flush_right_rows >= need)
		art_refresh_due = now + ART_REFRESH_MS;
	if (art_refresh_due && now >= art_refresh_due) {
		art_refresh_due = 0;
		if (art_shown) {
			art_wipe();
			flush_right_rows = 0;
		}
	}
}

static void art_blit(int bx, int by, int bw, int bh)
{
	int ox = (bw - (art_img_w + px_per_col - 1) / px_per_col) / 2;
	int oy = (bh - (art_img_h + px_per_row - 1) / px_per_row) / 2;
	if (ox < 0) ox = 0;
	if (oy < 0) oy = 0;
	char pos[48];
	int n = snprintf(pos, sizeof pos, "\x1b[0m\x1b[%d;%dH", by + oy + 1, bx + ox + 1);
	write_all(pos, (size_t)n);
	write_all(art_buf, art_len);
	art_shown = true;
	art_sx = bx; art_sy = by; art_sw = bw; art_sh = bh;
}

static void art_wipe(void)
{
	art_shown = false;
	invalidate_all();
	draw();
	flush();
}

static void art_update(void)
{
	if (!ART_ENABLE)
		return;
	if (art_box_h < 3 || art_box_w - 2 < 4) {
		if (art_shown)
			art_wipe();
		return;
	}
	int bx = art_box_x + 1, by = art_box_y + 1;
	int bw = art_box_w - 2, bh = art_box_h - 2;
	int ci = find_id(g.id);
	const char *uri = ci >= 0 ? Q[ci].uri : "";

	bool key_ok = art_cache_set && !strcmp(art_cache_uri, uri) &&
		      art_cache_w == bw && art_cache_h == bh;
	bool same_pos = art_shown && art_sx == bx && art_sy == by &&
			art_sw == bw && art_sh == bh;

	if (!key_ok) {
		art_start(uri, bw, bh);
		if (art_shown && (!same_pos || (art_none && !art_busy())))
			art_wipe();
		else if (art_none && !art_busy()) {
			draw();
			flush();
		}
		if (!art_busy())
			return;
	} else if (!art_busy()) {
		if (art_none || !art_len || same_pos)
			return;
		if (art_shown)
			art_wipe();
		art_blit(bx, by, bw, bh);
		return;
	}

	int rc = 0;
	bool finished = false;
	int st;
	pid_t r = waitpid(job.pid, &st, WNOHANG);
	if (r == job.pid) {
		finished = true;
		rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
		job.pid = 0;
	} else if (r < 0 && errno != EINTR) {
		finished = true;
		rc = -1;
		job.pid = 0;
	}

	if (!job.img_loaded && art_load_six()) {
		if (!art_shown) {
			draw();
			flush();
		}
		art_blit(bx, by, bw, bh);
	}

	if (!finished)
		return;
	if (job.img_loaded) {
		char line[64] = "";
		FILE *f = fopen(job.meta, "r");
		if (f) {
			if (fgets(line, sizeof line, f))
				line[strcspn(line, "\r\n")] = 0;
			fclose(f);
		}
		/* "640x640 -> 944x1026 2.8MB": source, what is displayed, sixel size.
		 * Shows at a glance if the cover is upscaled (soft) or was shrunk
		 * to fit tmux's input-buffer-size. */
		bool shrunk = art_img_w < art_target_w - 2 || art_img_h < art_target_h - 2;
		set_status("%s -> %dx%d %.1fMB %s%s%s", line[0] ? line : "cover",
			   art_img_w, art_img_h, (double)art_len / 1048576.0, art_enc_label,
			   shrunk ? (getenv("TMUX") ? " (shrunk to fit tmux)" : " (shrunk)") : "",
			   da1_sixel == 0 ? " (DA1: no sixel!)" : "");
	} else {
		switch (rc) {
		case 10: set_status("no embedded art, no cover file in folder"); break;
		case 11: set_status("sixel encoding failed (see " ART_LOG ".fail)"); break;
		case 12: set_status("ffmpeg not found on PATH"); break;
		case 13: set_status("img2sixel not found on PATH"); break;
		case 14: set_status("cover too detailed for the sixel byte budget"); break;
		default: set_status("art build failed (exit %d; see " ART_LOG ".fail)", rc); break;
		}
		if (rc != 10 && rc != 12 && rc != 13 && rc != 14)
			rename(ART_LOG, ART_LOG ".fail"); /* keep evidence */
		art_none = true;
		if (art_shown)
			art_wipe(); /* old cover must not outlive its song */
	}
	art_job_cleanup();
}
