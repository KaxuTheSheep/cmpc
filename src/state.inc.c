static int px_per_col = ART_PX_PER_COL_FALLBACK;
static int px_per_row = ART_PX_PER_ROW_FALLBACK;
static int art_box_x, art_box_y, art_box_w, art_box_h; /* set by draw(); outer box */
static char *art_buf;
static size_t art_len;
static char art_cache_uri[600];
static int art_cache_w, art_cache_h;
static bool art_cache_set;
static bool art_none;   /* last build found no art -> draw shows a text note */
static bool art_shown;  /* a sixel is currently on screen */
static char art_status[200];  /* last build result, shown in the art box title */
static int da1_sixel = -1;    /* terminal DA1 says sixel? -1 unknown, 0 no, 1 yes */
static char da1_raw[80];
static int art_sx, art_sy, art_sw, art_sh; /* interior rect it occupies */
static int art_img_w, art_img_h;           /* actual sixel size in px */
static int art_target_w, art_target_h;     /* size asked for (box/cap limited) */
static char art_enc_label[24];             /* "1024c" / "img2sixel", for the title */

/* ---- visualiser state (cava). vis_* box is the interior, set by draw() */
static int vis_x, vis_y, vis_w, vis_h;
static pid_t vis_pid;
static int vis_fd = -1;
static int vis_bars;               /* bar count cava was started with */
static int vis_val[256];           /* last frame, 0..1000 */
static int vis_nval;
static bool vis_failed;
static int right_x0;               /* first column of the right pane, set by draw() */
static int flush_right_rows;       /* right-pane rows rewritten by the last flush() */
static bool art_self_refresh;      /* the next flush is our own full repaint: don't count it */
static long art_refresh_due;       /* when the settled right pane should trigger a refresh */
static char vis_msg[100];
static long vis_next_try;          /* fifo mode: when to retry after a failure (ms) */
static char vis_cfg[80];
static char vis_buf[8192];
static size_t vis_blen;
