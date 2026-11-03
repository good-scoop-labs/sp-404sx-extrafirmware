/* Mocks for the Roland functions and RAM used by chromatic mode (include before vari.c).
   They model what matters on hardware: Roland's play request drops pads with no sample loaded before starting
   anything, starting a voice already renders its first audio blocks (at the pitch vari_fill computes then), and a
   playing loop pad (gate off) only stops when it is pressed again. */
struct event;
static unsigned mock_page[3] = { 0x0C12E044u, 0, 0 };        /* page shown: vtable, -, state */
static int mock_fwdmode, mock_held, mock_shift, mock_relpad = -2, mock_msgs;
static unsigned char mock_loaded[120];                       /* pads with a sample (play request check) */
static int mock_play_kind, mock_play_pad = -2, mock_reqs, mock_req_kind;
static unsigned mock_attack = 0;                             /* speed of the first block of the last voice */
static const char *mock_msg;
static unsigned mock_ratio_now(int pad);                     /* vari_fill's speed for a new voice of `pad` */
static int chr_play(int kind, int pad, int vel);
static int mock_playing = -1, mock_loop, mock_gate, mock_stops, mock_vfree; /* the pad sounding; source loop/gate */
static int mock_playpad(int kind, int pad, int vel) {        /* Roland: stop the pad if playing (a loop pad with gate
                                                                off only stops), else start it and fill it */
  mock_play_kind = kind;
  if (mock_playing == pad) { mock_playing = -1; if (mock_loop && !mock_gate) { mock_play_pad = -3; return 1; } }
  mock_play_pad = pad; mock_playing = pad; mock_attack = mock_ratio_now(pad); return 1;
}
static int mock_isplaying(int pad) { return mock_playing == pad; }
static void mock_stoppad(int pad, int how) { if (mock_playing == pad) mock_playing = -1; mock_stops++; }
static void mock_vfree_fn(int n) { mock_vfree += n == 7; }
static void mock_vfree2_fn(void) { mock_vfree += 10; }
static int mock_playreq(void *q, int kind, int pad, int vel) {   /* Roland: loaded check, then start via its pointer */
  mock_reqs++; mock_req_kind = kind; mock_play_pad = -2;
  if (pad < 0 || pad >= 120 || !mock_loaded[pad]) return 0;
  return chr_play(kind, pad, vel);
}
static void mock_rel(int pad) { mock_relpad = pad; }
static int mock_held_fn(int id) { return id == 40 ? mock_held : id == 20 ? mock_shift : 0; }   /* TIME/BPM, SHIFT */
static void mock_msg_fn(const char *s) { mock_msg = s; mock_msgs++; }
#define R_PLAYREQ mock_playreq
#define R_PLAYPAD mock_playpad
#define R_PLAYING mock_isplaying
#define R_STOPPAD mock_stoppad
#define R_VFREE   mock_vfree_fn
#define R_VFREE2  mock_vfree2_fn
#define R_RELPAD  mock_rel
#define R_HELD    mock_held_fn
#define R_MSG     mock_msg_fn
#define CURPAGE   ((const void *)mock_page)
#define FWDMODE   mock_fwdmode
#define REQOBJ    ((void *)0)
static int mock_kicks; static int mock_kick(void) { mock_kicks++; return 0; }   /* Roland: armed sampler starts */
#define R_SMPKICK mock_kick
static int mock_setpage_slot = -1; static void mock_setpage(void *mgr, int slot) { mock_setpage_slot = slot; }
#define R_SETPAGE mock_setpage
#define CHR(n)    __attribute__((noinline))
