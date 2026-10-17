#ifndef CONFIG_H
#define CONFIG_H

enum { ATTR_BOLD = 1, ATTR_DIM = 2, ATTR_ITAL = 4, ATTR_REV = 8 };
struct style { int fg, bg, attr; };

/* ---- connection -------*/
#define MPD_HOST      NULL   
#define MPD_PORT      0      
#define TICK_MS       250    

/* ---- DIR ----------------------------------- */
#ifndef MUSIC_DIR   
#define MUSIC_DIR     "/home/kaxuthesheep/Music"
#endif
/* ---- ART --------------------------------------------------------------------------- */
#define ART_ENABLE       1
#define ART_TMP          "/tmp/cmpc-art-src"    /* extracted image, no extension */
#define ART_FALLBACK_NAMES { "cover.jpg", "cover.png", "folder.jpg", "folder.png", NULL }
#define ART_MAX_PX       4000
#define ART_SIXEL_COLOURS 1024   /* 2..1024, or 0 for img2sixel */
#define ART_SIXEL_DITHER  1      /* Floyd-Steinberg; 0 = off     */
#define ART_SIXEL_ARGS    "-7 -p 256 -d none"   /* only when COLOURS is 0 */
#define ART_TMUX_BYTES   1000000
#define ART_PAD_COLOR    "0x04040A"
#define ART_POLL_MS      30
#define ART_REFRESH_ROWS 4
#define ART_REFRESH_MS   200
#define ART_LOG          "/tmp/cmpc-art.log"
#define ART_DUMP         "/tmp/cmpc-art.sixel"
#define ART_PX_PER_COL_FALLBACK  9
#define ART_PX_PER_ROW_FALLBACK  18

/* ---- visualiser (cava) ---------------------------------------------- */
#define VIS_ENABLE    1
#define VIS_MPD_FIFO  "/tmp/mpd.fifo"
#define CAVA_CMD      "cava"
#define CAVA_METHOD   "pipewire"   
#define CAVA_SOURCE   "auto"
#define VIS_FPS       30
#define VIS_BAR_W     2      /* cells per bar     */
#define VIS_BAR_GAP   1      /* cells between bars */

/* ---- colours --------------------------- --------------------------------------*/
#define ST_TEXT       ((struct style){ 7, -1, 0})            /* text_color        */
#define ST_DIM        ((struct style){ 1, -1, 0})            /* muted text        */
#define ST_BORDER     ((struct style){ 8, -1, 0})            /* borders_style     */
#define ST_BORDER_HI  ((struct style){ 3, -1, 0})            /* highlight_border  */
#define ST_ACCENT     ((struct style){ 3, -1, ATTR_BOLD})    /* highlighted_item  */
#define ST_TITLE      ((struct style){ 7, -1, ATTR_BOLD})
#define ST_CURRENT    ((struct style){ 0,  3, ATTR_BOLD})    /* current_item_style*/
#define ST_CURSOR     ((struct style){ 3, -1, ATTR_BOLD})
#define ST_UPNEXT     ((struct style){12, -1, 0})            /* up-next indent bar*/
#define ST_SELECT     ((struct style){ 5, -1, ATTR_BOLD})    /* multi-select mark */
#define ST_VIS_LO     ((struct style){ 1, -1, 0})            /* bottom third of a bar */
#define ST_VIS_MID    ((struct style){ 3, -1, 0})            /* middle third          */
#define ST_VIS_HI     ((struct style){12, -1, 0})            /* top third             */
#define ST_BAR_ON     ((struct style){ 3, -1, ATTR_BOLD})
#define ST_BAR_OFF    ((struct style){ 8, -1, 0})

/* ---- layout -------------------------------------------------------- */
#define LEFT_PCT      38     /* left pane width, percent of terminal   */
#define LEFT_MIN      34     /* ...but never narrower than this        */
#define NOWPLAY_H     6      /* now-playing box height (incl. borders) */
#define VIS_H         4      /* visualiser MINIMUM height (incl. borders). The art box is a square; the visualiser gets the height left over, never less than this */
#define ART_MIN       3      /* art box: never shorter than this       */
#define UPNEXT_BAR    "\u258e"
#define SELECT_MARK   "\u25c9"   /* fisheye - selected-for-playlist marker */
#define PLAY_MARK     "\u25b6"

/* ---- queue behaviour ----------------------------------------------- */
#define UP_NEXT_PRIO  1      /* MPD priority used to tag up-next songs */
#define START_MPD_REPEAT      1  /* turn MPD "repeat" on at startup    */
#define START_MPD_RANDOM_OFF  1  /* turn MPD "random" off at startup   */
#define START_SHUFFLE_LOOP    0  /* shuffle-loop mode on at startup    */
#define SHUFFLE_PCT   10
#define SHUFFLE_MIN   5
#define SHUFFLE_CAP   100
#define PROTECT_HEAD  3

/* ---- display ------------------------------------------------------- */
#define ARTIST_SEP    "//"   /* separator used in your tags (because Namida) */
#define ARTIST_JOIN   ", "   /* how it is shown                        */
#define VOLUME_STEP   5
#define SEEK_STEP     5      /* seconds                                */
#define MSG_MS        3000

/* file extensions (lowercase, no dot) treated as lossless */
#define LOSSLESS_EXTS { "flac", "wav", "alac", "ape", "wv", "aiff", "aif", "tta", "dsf", "dff", NULL }

#endif
