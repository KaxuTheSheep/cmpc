/* ====================================================================
 * Canvas: a cell grid, diffed against what is on screen
 * ==================================================================== */
struct cell { uint32_t cp; int16_t fg, bg; uint8_t attr, pad[3]; };

static struct cell *cv, *shown;
static int W, H;

static void canvas_resize(int w, int h)
{
	free(cv);
	free(shown);
	W = w;
	H = h;
	cv = calloc((size_t)w * h, sizeof *cv);
	shown = calloc((size_t)w * h, sizeof *shown);
	if (!cv || !shown)
		abort();
	for (int i = 0; i < w * h; i++)
		shown[i].cp = 0xFFFFFFFFu; /* force a full repaint */
}

static void canvas_clear(void)
{
	for (int i = 0; i < W * H; i++)
		cv[i] = (struct cell){' ', -1, -1, 0, {0}};
}

static void put_cell(int x, int y, uint32_t cp, struct style s)
{
	if (x < 0 || y < 0 || x >= W || y >= H)
		return;
	cv[y * W + x] = (struct cell){cp, (int16_t)s.fg, (int16_t)s.bg, (uint8_t)s.attr, {0}};
}

static int u8dec(const char *s, uint32_t *cp)
{
	const unsigned char *p = (const unsigned char *)s;
	if (p[0] < 0x80) {
		*cp = p[0];
		return p[0] ? 1 : 0;
	}
	int n = p[0] >= 0xF0 ? 4 : p[0] >= 0xE0 ? 3 : p[0] >= 0xC0 ? 2 : 0;
	if (!n) {
		*cp = 0xFFFD;
		return 1;
	}
	uint32_t c = p[0] & (0xFFu >> (n + 1));
	for (int i = 1; i < n; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			*cp = 0xFFFD;
			return i;
		}
		c = (c << 6) | (p[i] & 0x3F);
	}
	*cp = c;
	return n;
}

static int u8enc(uint32_t c, char *o)
{
	if (c < 0x80) { o[0] = c; return 1; }
	if (c < 0x800) { o[0] = 0xC0 | (c >> 6); o[1] = 0x80 | (c & 63); return 2; }
	if (c < 0x10000) {
		o[0] = 0xE0 | (c >> 12); o[1] = 0x80 | ((c >> 6) & 63); o[2] = 0x80 | (c & 63);
		return 3;
	}
	o[0] = 0xF0 | (c >> 18); o[1] = 0x80 | ((c >> 12) & 63);
	o[2] = 0x80 | ((c >> 6) & 63); o[3] = 0x80 | (c & 63);
	return 4;
}

static int cpw(uint32_t cp)
{
	if (cp < 32 || cp == 0x7f)
		return 1;
	int w = wcwidth((wchar_t)cp);
	return w < 0 ? 1 : w;
}

static int str_width(const char *s)
{
	int w = 0;
	uint32_t cp;
	int n;
	while ((n = u8dec(s, &cp)) > 0) {
		s += n;
		w += cpw(cp);
	}
	return w;
}

/* draw s at (x,y), at most maxw columns, with an ellipsis if cut */
static int put_str(int x, int y, int maxw, struct style st, const char *s)
{
	if (maxw <= 0 || !s)
		return 0;
	int ell = str_width(s) > maxw;
	int limit = ell ? maxw - 1 : maxw, used = 0;
	uint32_t cp;
	int n;
	while ((n = u8dec(s, &cp)) > 0) {
		s += n;
		if (cp < 32 || cp == 0x7f)
			cp = ' ';
		int w = cpw(cp);
		if (w == 0)
			continue;
		if (used + w > limit)
			break;
		put_cell(x + used, y, cp, st);
		if (w == 2)
			put_cell(x + used + 1, y, 0, st);
		used += w;
	}
	if (ell)
		put_cell(x + used++, y, 0x2026, st);
	return used;
}

static void put_str_r(int x, int y, int w, struct style st, const char *s)
{
	int sw = str_width(s);
	if (sw > w)
		put_str(x, y, w, st, s);
	else
		put_str(x + w - sw, y, sw, st, s);
}

static void fill(int x, int y, int w, struct style st)
{
	for (int i = 0; i < w; i++)
		put_cell(x + i, y, ' ', st);
}

static void box(int x, int y, int w, int h, struct style st, const char *title, const char *rtitle)
{
	if (w < 2 || h < 2)
		return;
	put_cell(x, y, 0x250C, st);
	put_cell(x + w - 1, y, 0x2510, st);
	put_cell(x, y + h - 1, 0x2514, st);
	put_cell(x + w - 1, y + h - 1, 0x2518, st);
	for (int i = 1; i < w - 1; i++) {
		put_cell(x + i, y, 0x2500, st);
		put_cell(x + i, y + h - 1, 0x2500, st);
	}
	for (int j = 1; j < h - 1; j++) {
		put_cell(x, y + j, 0x2502, st);
		put_cell(x + w - 1, y + j, 0x2502, st);
	}
	if (title && *title)
		put_str(x + 2, y, w - 4, st, title);
	if (rtitle && *rtitle)
		put_str_r(x + 2, y, w - 4, st, rtitle);
}

/* ---- output buffer + flush ---------------------------------------- */
static char *ob;
static size_t on, ocap;

static void ob_put(const char *s, size_t n)
{
	if (on + n > ocap) {
		ocap = (on + n) * 2 + 4096;
		ob = realloc(ob, ocap);
		if (!ob)
			abort();
	}
	memcpy(ob + on, s, n);
	on += n;
}

static void ob_fmt(const char *f, ...)
{
	char t[64];
	va_list ap;
	va_start(ap, f);
	int n = vsnprintf(t, sizeof t, f, ap);
	va_end(ap);
	if (n > 0)
		ob_put(t, (size_t)n);
}

static void write_all(const char *p, size_t n)
{
	while (n) {
		ssize_t r = write(STDOUT_FILENO, p, n);
		if (r < 0 && errno == EINTR)
			continue;
		if (r < 0 && errno == EAGAIN) {
			struct pollfd pf = {STDOUT_FILENO, POLLOUT, 0};
			poll(&pf, 1, 50);
			continue;
		}
		if (r <= 0)
			return;
		p += r;
		n -= (size_t)r;
	}
}

static void flush(void)
{
	on = 0;
	flush_right_rows = 0;
	struct cell last = {0xFFFFFFFEu, -2, -2, 255, {0}};
	for (int y = 0; y < H; y++) {
		struct cell *a = cv + y * W, *b = shown + y * W;
		int cx = -1; /* cursor column we know we're at on this row */
		bool right_hit = false;
		for (int x = 0; x < W; x++) {
			/* a sixel is on screen here; repainting these cells (even
			 * with identical blanks) would erase it, so leave them. */
			if (art_shown && y >= art_sy && y < art_sy + art_sh &&
			    x >= art_sx && x < art_sx + art_sw) {
				b[x] = a[x];
				continue;
			}
			struct cell c = a[x];
			if (c.cp == 0)
				continue; /* second half of a wide char; lead cell covers it */
			int cw = cpw(c.cp);
			bool dirty = memcmp(&a[x], &b[x], sizeof c) != 0;
			if (!dirty && cw == 2 && x + 1 < W)
				dirty = memcmp(&a[x + 1], &b[x + 1], sizeof c) != 0;
			if (!dirty)
				continue;
			if (right_x0 > 0 && x >= right_x0)
				right_hit = true;
			if (cx != x)
				ob_fmt("\x1b[%d;%dH", y + 1, x + 1);
			if (c.fg != last.fg || c.bg != last.bg || c.attr != last.attr) {
				ob_put("\x1b[0", 3);
				if (c.attr & ATTR_BOLD) ob_put(";1", 2);
				if (c.attr & ATTR_DIM) ob_put(";2", 2);
				if (c.attr & ATTR_ITAL) ob_put(";3", 2);
				if (c.attr & ATTR_REV) ob_put(";7", 2);
				if (c.fg >= 0) ob_fmt(";38;5;%d", c.fg);
				if (c.bg >= 0) ob_fmt(";48;5;%d", c.bg);
				ob_put("m", 1);
				last = c;
			}
			char e[4];
			ob_put(e, (size_t)u8enc(c.cp, e));
			cx = x + cw;
			b[x] = a[x];
			if (cw == 2 && x + 1 < W)
				b[x + 1] = a[x + 1];
		}
		if (right_hit)
			flush_right_rows++;
	}
	ob_put("\x1b[0m", 4);
	write_all(ob, on);
}

/* Marks every cell as unknown, so the next flush() rewrites the whole screen. */
static void invalidate_all(void)
{
	for (int i = 0; i < W * H; i++)
		shown[i].cp = 0xFFFFFFFFu;
}
