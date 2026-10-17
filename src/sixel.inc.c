/* src/sixel.inc.c - part of cmpc.c: included in order, never compiled on its own.
 * Built-in sixel encoder (`cmpc --sixel`): runs as a helper process for the art pipeline
 * and shares nothing with the rest of the program. */

/* ====================================================================
 * Built-in sixel encoder:  cmpc --sixel W H COLOURS DITHER < rgb24 > sixel
 *
 * img2sixel/libsixel stop at 256 colours, which bands visibly on skin and
 * shadows (smooth gradients get a handful of shades). Foot and tmux both
 * take 1024 colour registers, so this median-cuts to up to 1024 and can
 * Floyd-Steinberg dither (serpentine). Input is raw rgb24, W*H*3 bytes.
 * ==================================================================== */
static int q_axis;

static int q_cmp(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;
	int cx = q_axis == 0 ? (x >> 12) & 63 : q_axis == 1 ? (x >> 6) & 63 : x & 63;
	int cy = q_axis == 0 ? (y >> 12) & 63 : q_axis == 1 ? (y >> 6) & 63 : y & 63;
	return cx - cy;
}

struct qbox { int lo, hi; uint64_t count; };

static void six_run(int n, int ch)
{
	if (n > 3)
		printf("!%d%c", n, ch);
	else
		while (n-- > 0)
			putchar(ch);
}

static int sixel_cmd(int argc, char **argv)
{
	if (argc < 6)
		return 2;
	int W = atoi(argv[2]), H = atoi(argv[3]), ncol = atoi(argv[4]), dither = atoi(argv[5]);
	if (W < 1 || H < 1 || W > 10000 || H > 10000)
		return 2;
	if (ncol < 2) ncol = 2;
	if (ncol > 1024) ncol = 1024;
	size_t npx = (size_t)W * (size_t)H;
	uint8_t *rgb = malloc(npx * 3);
	if (!rgb)
		return 3;
	size_t got = 0;
	while (got < npx * 3) {
		size_t r = fread(rgb + got, 1, npx * 3 - got, stdin);
		if (r == 0)
			break;
		got += r;
	}
	if (got != npx * 3)
		return 3; /* short input: the ffmpeg feeding us failed */

	/* histogram on 6 bits per channel (5 bits is what bands skin: a smooth
	 * face only occupies a couple of hundred 8-level cells); remember the
	 * true colour sums so palette entries are exact means */
	static uint64_t cnt[262144], sr[262144], sg[262144], sb[262144];
	for (size_t i = 0; i < npx; i++) {
		const uint8_t *p = rgb + i * 3;
		int bin = ((p[0] >> 2) << 12) | ((p[1] >> 2) << 6) | (p[2] >> 2);
		cnt[bin]++; sr[bin] += p[0]; sg[bin] += p[1]; sb[bin] += p[2];
	}
	int *bins = malloc(262144 * sizeof *bins);
	int nb = 0;
	for (int i = 0; i < 262144; i++)
		if (cnt[i])
			bins[nb++] = i;

	/* median cut: repeatedly split the most populous box on its longest
	 * (perceptually weighted) axis at the median pixel count */
	struct qbox *box = malloc((size_t)ncol * sizeof *box);
	int nbox = 1;
	box[0] = (struct qbox){0, nb, npx};
	while (nbox < ncol) {
		int best = -1;
		for (int i = 0; i < nbox; i++) {
			if (box[i].hi - box[i].lo < 2)
				continue;
			if (best < 0 || box[i].count > box[best].count)
				best = i;
		}
		if (best < 0)
			break;
		struct qbox *b = &box[best];
		int mn[3] = {63, 63, 63}, mx[3] = {0, 0, 0};
		for (int k = b->lo; k < b->hi; k++) {
			int v = bins[k], ch[3] = {(v >> 12) & 63, (v >> 6) & 63, v & 63};
			for (int a = 0; a < 3; a++) {
				if (ch[a] < mn[a]) mn[a] = ch[a];
				if (ch[a] > mx[a]) mx[a] = ch[a];
			}
		}
		static const int wgt[3] = {3, 6, 2}; /* R G B, roughly luma */
		int axis = 0;
		for (int a = 1; a < 3; a++)
			if ((mx[a] - mn[a]) * wgt[a] > (mx[axis] - mn[axis]) * wgt[axis])
				axis = a;
		q_axis = axis;
		qsort(bins + b->lo, (size_t)(b->hi - b->lo), sizeof *bins, q_cmp);
		uint64_t half = b->count / 2, acc = 0;
		int cut = b->lo + 1;
		uint64_t left = cnt[bins[b->lo]];
		for (int k = b->lo; k < b->hi - 1; k++) {
			acc += cnt[bins[k]];
			cut = k + 1;
			left = acc;
			if (acc >= half)
				break;
		}
		box[nbox++] = (struct qbox){cut, b->hi, b->count - left};
		b->hi = cut;
		b->count = left;
	}

	int npal = nbox;
	uint8_t (*pal)[3] = malloc((size_t)npal * sizeof *pal);
	for (int i = 0; i < npal; i++) {
		uint64_t n = 0, r = 0, g = 0, bl = 0;
		for (int k = box[i].lo; k < box[i].hi; k++) {
			int v = bins[k];
			n += cnt[v]; r += sr[v]; g += sg[v]; bl += sb[v];
		}
		if (!n) n = 1;
		pal[i][0] = (uint8_t)(r / n); pal[i][1] = (uint8_t)(g / n); pal[i][2] = (uint8_t)(bl / n);
	}
	free(bins);
	free(box);

	/* nearest palette entry, cached per 7-bit-per-channel cell */
	int16_t *cache = malloc(((size_t)1 << 21) * sizeof *cache);
	memset(cache, 0xff, ((size_t)1 << 21) * sizeof *cache);
#define NEAREST(R, G, B, OUT) do { \
		size_t key_ = ((size_t)((R) >> 1) << 14) | ((size_t)((G) >> 1) << 7) | (size_t)((B) >> 1); \
		if (cache[key_] >= 0) { (OUT) = cache[key_]; } else { \
			long bd_ = LONG_MAX; int bi_ = 0; \
			for (int j_ = 0; j_ < npal; j_++) { \
				long dr_ = (R) - pal[j_][0], dg_ = (G) - pal[j_][1], db_ = (B) - pal[j_][2]; \
				long d_ = dr_ * dr_ * 3 + dg_ * dg_ * 4 + db_ * db_ * 2; \
				if (d_ < bd_) { bd_ = d_; bi_ = j_; } \
			} \
			cache[key_] = (int16_t)bi_; (OUT) = bi_; } \
	} while (0)

	uint16_t *idx = malloc(npx * sizeof *idx);
	if (!dither) {
		for (size_t i = 0; i < npx; i++) {
			int id;
			NEAREST(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2], id);
			idx[i] = (uint16_t)id;
		}
	} else {
		float *e0 = calloc((size_t)(W + 2) * 3, sizeof *e0);
		float *e1 = calloc((size_t)(W + 2) * 3, sizeof *e1);
		for (int y = 0; y < H; y++) {
			bool ltr = (y & 1) == 0;
			int dir = ltr ? 1 : -1;
			memset(e1, 0, (size_t)(W + 2) * 3 * sizeof *e1);
			for (int i = 0; i < W; i++) {
				int x = ltr ? i : W - 1 - i;
				const uint8_t *p = rgb + ((size_t)y * W + x) * 3;
				int iv[3];
				float v[3];
				for (int ch = 0; ch < 3; ch++) {
					v[ch] = (float)p[ch] + e0[(x + 1) * 3 + ch];
					if (v[ch] < 0) v[ch] = 0;
					if (v[ch] > 255) v[ch] = 255;
					iv[ch] = (int)(v[ch] + 0.5f);
				}
				int id;
				NEAREST(iv[0], iv[1], iv[2], id);
				idx[(size_t)y * W + x] = (uint16_t)id;
				for (int ch = 0; ch < 3; ch++) {
					float er = v[ch] - (float)pal[id][ch];
					e0[(x + 1 + dir) * 3 + ch] += er * 7.0f / 16.0f;
					e1[(x + 1 - dir) * 3 + ch] += er * 3.0f / 16.0f;
					e1[(x + 1) * 3 + ch] += er * 5.0f / 16.0f;
					e1[(x + 1 + dir) * 3 + ch] += er * 1.0f / 16.0f;
				}
			}
			float *t = e0; e0 = e1; e1 = t;
		}
		free(e0);
		free(e1);
	}
	free(cache);
	free(rgb);

	/* emit */
	static char obuf[1 << 16];
	setvbuf(stdout, obuf, _IOFBF, sizeof obuf);
	printf("\033Pq\"1;1;%d;%d", W, H);
	for (int i = 0; i < npal; i++)
		printf("#%d;2;%d;%d;%d", i, (pal[i][0] * 100 + 127) / 255,
		       (pal[i][1] * 100 + 127) / 255, (pal[i][2] * 100 + 127) / 255);
	int bands = (H + 5) / 6;
	uint8_t *row = calloc((size_t)npal * (size_t)W, 1);
	int *used = malloc((size_t)npal * sizeof *used);
	int *xmax = malloc((size_t)npal * sizeof *xmax);
	uint8_t *seen = calloc((size_t)npal, 1);
	for (int band = 0; band < bands; band++) {
		int nused = 0;
		for (int x = 0; x < W; x++) {
			for (int k = 0; k < 6; k++) {
				int y = band * 6 + k;
				if (y >= H)
					y = H - 1; /* replicate: no transparent sliver */
				int c = idx[(size_t)y * W + x];
				if (!seen[c]) {
					seen[c] = 1;
					used[nused++] = c;
					xmax[c] = 0;
				}
				row[(size_t)c * W + x] |= (uint8_t)(1 << k);
				if (x > xmax[c])
					xmax[c] = x;
			}
		}
		for (int u = 0; u < nused; u++) {
			int c = used[u];
			uint8_t *r = row + (size_t)c * W;
			printf("#%d", c);
			for (int x = 0; x <= xmax[c];) {
				int ch = 63 + r[x], n = 1;
				while (x + n <= xmax[c] && 63 + r[x + n] == ch)
					n++;
				six_run(n, ch);
				x += n;
			}
			memset(r, 0, (size_t)xmax[c] + 1);
			seen[c] = 0;
			if (u < nused - 1)
				putchar('$');
		}
		if (band < bands - 1)
			putchar('-');
	}
	fputs("\033\\", stdout);
	fflush(stdout);
	return ferror(stdout) ? 4 : 0;
}

