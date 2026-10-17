/* ====================================================================
 * Terminal
 * ==================================================================== */
static struct termios orig_tio;
static bool tty_on;
static volatile sig_atomic_t sig_winch, sig_quit;

static void on_winch(int s) { (void)s; sig_winch = 1; }
static void on_quit(int s) { (void)s; sig_quit = 1; }

static void term_restore(void)
{
	if (!tty_on)
		return;
	tty_on = false;
	write_all("\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l", 26);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_tio);
}

static void term_init(void)
{
	struct termios raw;
	tcgetattr(STDIN_FILENO, &orig_tio);
	raw = orig_tio;
	cfmakeraw(&raw);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	write_all("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J", 24);
	tty_on = true;
	atexit(term_restore);

	struct sigaction sa = {0};
	sa.sa_handler = on_winch;
	sigaction(SIGWINCH, &sa, NULL);
	sa.sa_handler = on_quit;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
}

static void term_size(void)
{
	struct winsize ws;
	int w = 80, h = 24;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
		w = ws.ws_col;
		h = ws.ws_row;
	}
	canvas_resize(w, h);
}

/* ---- keys ---------------------------------------------------------- */
enum { K_NONE = 0, K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT, K_PGUP, K_PGDN, K_HOME, K_END, K_ESC };

static int read_byte(int timeout_ms)
{
	struct pollfd p = {STDIN_FILENO, POLLIN, 0};
	unsigned char c;
	if (poll(&p, 1, timeout_ms) <= 0)
		return -1;
	if (read(STDIN_FILENO, &c, 1) != 1)
		return -1;
	return c;
}

static int read_key(void)
{
	int c = read_byte(0);
	if (c < 0)
		return K_NONE;
	if (c != 27)
		return c;
	int d = read_byte(25);
	if (d < 0)
		return K_ESC;
	if (d != '[' && d != 'O')
		return K_ESC;
	int num = 0, e;
	while ((e = read_byte(25)) >= 0) {
		if (e >= '0' && e <= '9') { num = num * 10 + (e - '0'); continue; }
		if (e == ';') { num = 0; continue; }
		switch (e) {
		case 'A': return K_UP;
		case 'B': return K_DOWN;
		case 'C': return K_RIGHT;
		case 'D': return K_LEFT;
		case 'H': return K_HOME;
		case 'F': return K_END;
		case '~':
			switch (num) {
			case 5: return K_PGUP;
			case 6: return K_PGDN;
			case 1: case 7: return K_HOME;
			case 4: case 8: return K_END;
			}
			return K_NONE;
		}
		return K_NONE;
	}
	return K_NONE;
}

static void query_sixel_support(void)
{
	char buf[80];
	int n = 0, c;
	write_all("\x1b[c", 3);
	while (n < (int)sizeof buf - 1 && (c = read_byte(150)) >= 0) {
		buf[n++] = (char)c;
		if (c == 'c')
			break;
	}
	buf[n] = 0;
	snprintf(da1_raw, sizeof da1_raw, "%s", n ? (buf[0] == 27 ? buf + 1 : buf) : "(no reply)");
	da1_sixel = 0;
	const char *q = strchr(buf, '?');
	if (q) {
		const char *p = q + 1;
		while (*p) {
			int v = 0;
			bool have = false;
			while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; have = true; }
			if (have && v == 4)
				da1_sixel = 1;
			if (*p == ';') p++; else break;
		}
	}
}

static void query_cell_pixels(void)
{
	char buf[64];
	int n = 0, c, h = 0, w = 0;

	write_all("\x1b[16t", 5);
	while (n < (int)sizeof buf - 1 && (c = read_byte(150)) >= 0) {
		buf[n++] = (char)c;
		if (c == 't')
			break;
	}
	buf[n] = 0;
	if (sscanf(buf, "\x1b[6;%d;%dt", &h, &w) == 2 && h > 0 && w > 0) {
		px_per_row = h;
		px_per_col = w;
		return;
	}

	n = 0;
	write_all("\x1b[14t", 5);
	while (n < (int)sizeof buf - 1 && (c = read_byte(150)) >= 0) {
		buf[n++] = (char)c;
		if (c == 't')
			break;
	}
	buf[n] = 0;
	if (sscanf(buf, "\x1b[4;%d;%dt", &h, &w) == 2 && h > 0 && w > 0 && H > 0 && W > 0) {
		px_per_row = h / H;
		px_per_col = w / W;
	}
}
