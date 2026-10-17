/* ---- metadata editor ----------------------------------------------- */

/* Wraps s in single quotes for /bin/sh, escaping embedded quotes. */
static void shquote_into(char *dst, size_t dstsz, const char *s)
{
	size_t o = 0;
	if (dstsz < 3) { if (dstsz) dst[0] = 0; return; }
	dst[o++] = '\'';
	for (; *s && o + 5 < dstsz; s++) {
		if (*s == '\'') {
			dst[o++] = '\''; dst[o++] = '\\'; dst[o++] = '\''; dst[o++] = '\'';
		} else {
			dst[o++] = *s;
		}
	}
	dst[o++] = '\'';
	dst[o] = 0;
}

static void free_meta_tags(void)
{
	free(META_TAGS);
	META_TAGS = NULL;
	nmeta_tags = 0;
}

static void add_meta_tag(const char *key, const char *val)
{
	META_TAGS = realloc(META_TAGS, (size_t)(nmeta_tags + 1) * sizeof *META_TAGS);
	if (!META_TAGS) abort();
	snprintf(META_TAGS[nmeta_tags].key, sizeof META_TAGS[0].key, "%s", key);
	snprintf(META_TAGS[nmeta_tags].val, sizeof META_TAGS[0].val, "%s", val);
	nmeta_tags++;
}

static void load_meta_tags(void)
{
	free_meta_tags();
	if (!meta_supported)
		return;
	char qpath[900], cmd[1200];
	shquote_into(qpath, sizeof qpath, meta_path);
	if (meta_is_flac)
		snprintf(cmd, sizeof cmd, "metaflac --list --block-type=VORBIS_COMMENT %s 2>/dev/null", qpath);
	else
		snprintf(cmd, sizeof cmd, "mid3v2 -l %s 2>/dev/null", qpath);
	FILE *fp = popen(cmd, "r");
	if (!fp) {
		msg("couldn't run %s", meta_is_flac ? "metaflac" : "mid3v2");
		return;
	}
	char line[700];
	while (fgets(line, sizeof line, fp)) {
		char *p = line;
		if (meta_is_flac) {
			char *c = strstr(p, "comment[");
			if (!c) continue;
			char *colon = strchr(c, ':');
			if (!colon) continue;
			p = colon + 1;
			while (*p == ' ') p++;
		}
		char *eq = strchr(p, '=');
		if (!eq) continue;
		*eq = 0;
		char *key = p, *val = eq + 1;
		while (*key == ' ') key++;
		if (strlen(key) >= sizeof META_TAGS[0].key)
			key[sizeof META_TAGS[0].key - 1] = 0; /* absurdly long tag name; trim it */
		size_t vl = strlen(val);
		while (vl && (val[vl - 1] == '\n' || val[vl - 1] == '\r' || val[vl - 1] == ' '))
			val[--vl] = 0;
		if (*key)
			add_meta_tag(key, val);
	}
	pclose(fp);
}

static void open_metadata(const char *uri)
{
	snprintf(meta_uri, sizeof meta_uri, "%s", uri);
	snprintf(meta_path, sizeof meta_path, "%s/%s", MUSIC_DIR, uri);
	const char *ext = ext_of(uri);
	meta_is_flac = !strcasecmp(ext, "flac");
	bool is_mp3 = !strcasecmp(ext, "mp3");
	meta_supported = meta_is_flac || is_mp3;
	if (!meta_supported) {
		msg("tag editing not supported for .%s yet", ext);
		return;
	}
	load_meta_tags();
	meta_mode = true;
	meta_sel = meta_top = 0;
	medit_state = MEDIT_NONE;
}

static const char *mid3v2_flag(const char *key)
{
	struct { const char *id, *flag; } map[] = {
		{"TIT2", "-t"}, {"TPE1", "-a"}, {"TALB", "-A"},
		{"TRCK", "-T"}, {"TCON", "-g"}, {"COMM", "-c"},
		{"TDRC", "-y"}, {"TYER", "-y"},
	};
	for (size_t i = 0; i < sizeof map / sizeof *map; i++)
		if (!strcasecmp(key, map[i].id))
			return map[i].flag;
	return NULL;
}

static void stage_write(const char *key, const char *val)
{
	char qpath[900], qval[600];
	shquote_into(qpath, sizeof qpath, meta_path);
	shquote_into(qval, sizeof qval, val);
	if (meta_is_flac) {
		char kv[560], qkv[700];
		snprintf(kv, sizeof kv, "%s=%s", key, val);
		shquote_into(qkv, sizeof qkv, kv);
		snprintf(pending_cmd, sizeof pending_cmd,
			 "metaflac --remove-tag=%s --set-tag=%s %s", key, qkv, qpath);
	} else {
		const char *flag = mid3v2_flag(key);
		if (flag) {
			snprintf(pending_cmd, sizeof pending_cmd, "mid3v2 %s %s %s", flag, qval, qpath);
		} else {
			char kv[560], qkv[700];
			snprintf(kv, sizeof kv, "%s=%s", key, val);
			shquote_into(qkv, sizeof qkv, kv);
			snprintf(pending_cmd, sizeof pending_cmd, "mid3v2 --TXXX %s %s", qkv, qpath);
		}
	}
	snprintf(pending_label, sizeof pending_label, "%s = %s", key, val);
	medit_state = MEDIT_CONFIRM;
}

static void free_metadata(void)
{
	free_meta_tags();
}
