/* Host test: varispeed on a long sample that streams from the card (issue #1).
   Models Roland's feeder (forward fill 0x0C0914B8 and its loads): the sample's first 64 KB play from the RAM head,
   the rest from two 24 KB card buffers in turn. The init loads buffer 0. Fill flags a finished buffer in *flag,
   stops the voice when the buffer it needs has no data yet (its end pointer is behind), and reads whatever a buffer
   holds when only the data is missing. During playback the service refills a flagged buffer right after the fill
   (0x0C0911B4). The voice start (0x0C0963AC) renders 4 or 5 blocks at once: after block 2 it queues buffer 1's first
   load (0x0C091464) and refills a finished buffer itself, after blocks 3..5 it queues the refill; the sample task
   runs the queues after the start. Checked for every speed from -12 to +7 semitones and chromatic speeds up to
   just under 8x, stereo and mono, started cold: the note must play to its end, every frame from the right chunk,
   no buffer reloaded while still in use; at normal speed and slower vari_fill must leave streaming to Roland. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int prm[64], patt=1200; static int g(int id,int pad){return id==52?7:prm[id];} static int dv(int a,int b){return a/b;}
static int pt(void){ return patt*10; }
static void st(int id,int v,int pad,int f){ prm[id]=v; }
struct page; struct event;
static int evo(struct page *p, struct event *e){ return 0; }
static void dsp(struct page *p){}
#define R_SET st
#define R_EVORIG evo
#define R_DISP dsp
static short knobs[3];
#define KNOBPOS knobs
#define R_TEMPO pt
#define DEV
static void sn(int n){} static void ss(const char*s,int f){}
#define R_GET g
#define R_DIV dv
#define R_SHOWN sn
#define R_SHOWS ss
#define SECT(x)
#define CAVE
#define PERSIST
#define section(x) unused
static unsigned char padinfo[120*32];
#define PADINFO ((const unsigned char*)padinfo)
static void lk(int n){} static int cr(const char*p){return -1;} static int op(const char*p,int m){return -1;} static int rd(int f,void*b,int n){return 0;} static int wr(int f,const void*b,int n){return n;} static void cl(int f){} static int cardok(void){return 1;}
#define R_LOCK lk
#define R_UNLOCK lk
#define R_CREATE cr
#define R_OPEN op
#define R_READ rd
#define R_WRITE wr
#define R_CLOSE cl
#define R_CARDOK cardok

/* ---- Roland's streaming feeder, modelled (positions in frames) */
struct obj { char pad0[8]; int chunks; int f12; int cursor; int f20; int voice; int pad; char pad1[16]; int load1; char rest[64]; };
static struct obj o;
static int H, C, NCH, LEN;               /* head frames, chunk frames, chunks, sample length */
static int loaded[2], endk[2];           /* chunk each buffer holds (-1 none) / its end pointer covers (as chunk) */
static int target, nextc;                /* buffer the next refill writes, next chunk to read from the card */
static int qrefill, qload;               /* sample task queues (one voice here) */
static int underrun, stale, clobber, dbl, released;
static int playing = 1;
static void refill(void) {               /* 0x0C0912C8: next chunk into the target buffer */
  if (nextc >= NCH) { released++; return; }        /* Roland: -1, the task releases the pad */
  int b = target;
  if (loaded[b] >= 0 && o.cursor < H + (loaded[b] + 1) * C) clobber++;   /* chunk still being played */
  loaded[b] = nextc; endk[b] = nextc; nextc++; target ^= 1;
}
static void load1(void) {                /* 0x0C091464: buffer 1's first load */
  if (!o.load1) dbl++;                   /* already loaded (by a refill): this would overwrite it */
  if (loaded[1] >= 0 && o.cursor < H + (loaded[1] + 1) * C) clobber++;
  loaded[1] = nextc; nextc++; o.load1 = 0; target = 0;
}
static void m_runrefill(void){ while (qrefill) { qrefill--; refill(); } }   /* sample task, event 32   */
static void m_runload(void){ while (qload) { qload--; if (playing) load1(); } }  /* sample task, event 4096 */
static int ours;                         /* refills vari_fill made itself */
static int m_refill(void *feeder){ if (feeder != &o) { printf("bad feeder\n"); exit(1); } ours++; refill(); return 1; }
#define R_REFILL m_refill
#include "mock_chr.h"
#include "vari.c"
static unsigned mock_ratio_now(int pad) { return chr_ratio(pad, pad_ratio(pad)); }
#include "ratio.c"

static s16 val(int pos) { return (s16)(pos * 7 + 3); }
static int rfill(void *o_, s16 *L, s16 *R, int *flag, int *frames) {
  int n = *frames, i;
  for (i = 0; i < n; i++) {
    int p = o.cursor;
    if (p >= LEN) break;
    s16 v = val(p);
    if (p >= H) {
      int k = (p - H) / C, b = k & 1;
      if (endk[b] < k) { underrun++; *frames = i; for (; i < n; i++) L[i] = R[i] = 0; return 0; }   /* 0x0C084B9C */
      if (loaded[b] != k) { stale++; v = 0x5A5A; }
    }
    L[i] = R[i] = v; o.cursor++;
    if (o.cursor > H && (o.cursor - H) % C == 0 && NCH > 2) *flag = 1;      /* a buffer finished */
  }
  if (i < n) { *frames = i; for (; i < n; i++) L[i] = R[i] = 0; return 0; }
  return 1;
}

/* One note, started cold, played to its end. Returns 1 if it played through cleanly. */
static int play(int stereo, int nstart, u32 *ratio) {
  H = stereo ? 16384 : 32768; C = stereo ? 6144 : 12288; NCH = 72; LEN = H + NCH * C;
  memset(&o, 0, sizeof o); o.voice = 4; o.pad = 7; o.chunks = NCH; memset(vari_vs, 0, sizeof vari_vs);
  loaded[0] = 0; endk[0] = 0; loaded[1] = -1; endk[1] = 1; target = 1; nextc = 1; o.load1 = 1;   /* init 0x0C0913A0 */
  qrefill = qload = underrun = stale = clobber = dbl = released = ours = 0; playing = 1;
  *ratio = chr_ratio(7, pad_ratio(7));
  static s16 L[4096], R[4096];
  int flag = 0, blocks = 0, fr, rv = 1;
  for (int b = 0; b < nstart && rv; b++) {           /* voice start: 4 or 5 blocks inside play_pad */
    if (b != 1) flag = 0;                            /* blocks 1 and 2 share one flag */
    fr = 4096; rv = vari_fill(&o, L, R, &flag, &fr, rfill); blocks++;
    if (b == 1) { if (o.load1) qload++; if (flag) refill(); }
    else if (b > 1 && flag) qrefill++;
  }
  m_runrefill(); m_runload();                        /* the start buffered ~0.46 s: the task runs its queues first */
  while (rv) {                                       /* playback: fill a block, refill a flagged buffer */
    flag = 0; fr = 4096; rv = vari_fill(&o, L, R, &flag, &fr, rfill); blocks++;
    if (flag) refill();
    m_runrefill(); m_runload();
    if (blocks > 100000) { printf("runaway\n"); exit(1); }
  }
  if (*ratio <= UNITY && ours) { printf("x%.3f: vari_fill refilled itself (%d)\n", *ratio / 65536.0, ours); exit(1); }
  return !underrun && !stale && !clobber && !dbl && o.cursor >= LEN;
}

int main() {
  int fails = 0, cases = 0;
  prm[42] = 1200; prm[43] = 0;
  for (int nstart = 4; nstart <= 5; nstart++)
  for (int stereo = 0; stereo < 2; stereo++) {
    for (int knob = 3; knob <= 124; knob++) {        /* every CTRL 3 position, -12 .. +7 semitones */
      vari_tab.e[7].pos = (s8)(knob - 64); chr_tab.b[0].src = 0; chr_st.cur[0] = 0;
      u32 r; int ok = play(stereo, nstart, &r); cases++;
      if (!ok) { fails++; printf("%d-block start, %s knob %d (x%.3f): underrun %d stale %d clobber %d double %d, played %d of %d\n",
                                 nstart, stereo ? "stereo" : "mono", knob, r / 65536.0, underrun, stale, clobber, dbl, o.cursor, LEN); }
    }
    for (int key = 1; key <= 35; key++) {            /* chromatic notes on a pad at +7: up to just under 8x */
      vari_tab.e[7].pos = (s8)(key & 1 ? 60 : 0); chr_tab.b[0].src = 8; chr_st.cur[0] = (unsigned char)(key + S0);
      u32 r; int ok = play(stereo, nstart, &r); cases++;
      if (!ok) { fails++; printf("%d-block start, %s key +%d (x%.3f): underrun %d stale %d clobber %d double %d, played %d of %d\n",
                                 nstart, stereo ? "stereo" : "mono", key, r / 65536.0, underrun, stale, clobber, dbl, o.cursor, LEN); }
    }
  }
  printf("%d cases, %d failures\n", cases, fails);
  return fails != 0;
}
