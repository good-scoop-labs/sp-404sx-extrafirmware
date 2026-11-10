/* Host test: the pan menu (SHIFT + START/END/LEVEL, TIME/BPM page mode 10: opening from the play page and every
   TIME/BPM page view including the chromatic menu, not in FWD mode; CTRL 3 mapping and display; LEDs; every way of
   closing) and PAN.BIN (saved, loaded, following the sample on copy / exchange, reset when the sample changes).
   Roland functions are mocks; vari.c is compiled for the PC. Build A: no audio change. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int prm[64], patt=1200; static char shbuf[8]; static int shows;
static int mock_cur=26, mock_bank=2;
static int g(int id,int pad){ if(id==52) return mock_cur; if(id==53) return mock_bank; return prm[id]; }
static int dv(int a,int b){return a/b;}
static int pt(void){ return patt*10; }
static void sn(int n){ shbuf[0]=0; }
static void ss(const char*s,int f){ shows++; if((unsigned long)s>=0x0C080000 && (unsigned long)s<0x0C140000) strcpy(shbuf,"(rol)"); else strncpy(shbuf,s,7); }
#define R_GET g
#define R_DIV dv
#define R_SHOWN sn
#define R_SHOWS ss
static short knobs[3];
#define KNOBPOS knobs
#define R_TEMPO pt
#define DEV
#define SECT(x)
#define CAVE
#define PERSIST
#define section(x) unused
static unsigned char padinfo[120*32];
#define PADINFO ((const unsigned char*)padinfo)
/* a tiny card: a few files, each with a read / write position */
static char fname[4][40]; static unsigned char fdata[4][512]; static int flen[4], fpos[4], fwrites;
static int slot_of(const char*p){ for(int i=0;i<4;i++) if(!strcmp(fname[i],p)) return i; for(int i=0;i<4;i++) if(!fname[i][0]){ strcpy(fname[i],p); return i; } return 0; }
static void lk(int n){}
static int cr(const char*p){ int s=slot_of(p); flen[s]=0; fpos[s]=0; return s; }
static int op(const char*p,int m){ for(int i=0;i<4;i++) if(!strcmp(fname[i],p) && flen[i]) { fpos[i]=0; return i; } return -1; }
static int rd(int f,void*b,int n){ int k=flen[f]-fpos[f]; if(k>n) k=n; if(k<0) k=0; memcpy(b,fdata[f]+fpos[f],k); fpos[f]+=k; return k; }
static int wr(int f,const void*b,int n){ memcpy(fdata[f]+fpos[f],b,n); fpos[f]+=n; if(fpos[f]>flen[f]) flen[f]=fpos[f]; fwrites++; return n; }
static void cl(int f){} static int cardok(void){return 1;}
#define R_LOCK lk
#define R_UNLOCK lk
#define R_CREATE cr
#define R_OPEN op
#define R_READ rd
#define R_WRITE wr
#define R_CLOSE cl
#define R_CARDOK cardok
#include "mock_chr.h"
/* Roland's voice objects (24 B each, +8 player) and players (+4 channels the voice was started with) */
static struct { char pad0[8]; void *player; char pad1[8]; } mock_voices[12];
static int mock_players[12][4];
#define VOICES ((const unsigned char *)mock_voices)
#include "vari.c"
#include "ratio.c"
static unsigned mock_ratio_now(int pad) { return chr_ratio(pad, pad_ratio(pad)); }

static int fails=0;
#define CHECK(c, what) do { int ok_=(c); printf("%s: %s\n", ok_?"ok  ":"FAIL", what); if(!ok_) fails++; } while(0)
static struct page tp;                                      /* the TIME/BPM page */
static int pevs; static int pev_orig(void *page, struct event *e){ pevs++; return 0; }
/* an event on the TIME/BPM page (ev_stub): what pan_event returns (1 = taken, 0 = Roland's) */
static int ev(int type, int id, int val){ struct event e={type,(unsigned char)id,(signed char)val}; mock_page[0]=VT_EDIT; return pan_event(&tp,&e); }
static int btn(int id){ return ev(0,id,1); }
static int knob(int id, int val){ return ev(1,id,val); }
static void draw(void){ mock_page[0]=VT_EDIT; shbuf[0]=0; vari_display(&tp); }
/* SHIFT + START/END/LEVEL on the play page, then the TIME/BPM page Roland opens (mode 1) draws for the first time */
static void open_from_play(int pad){ struct event e={0,BTN_SEL,1}; mock_cur=pad; mock_shift=1; mock_page[0]=VT_PLAY; chr_page_event(0,&e,pev_orig); mock_shift=0; tp.mode=1; draw(); }
static int shift_btn(int id){ int r; mock_shift=1; r=btn(id); mock_shift=0; return r; }
static int roland_pg; static int mock_pg(void *page){ roland_pg++; mock_led[LED_BPM]=2; mock_led[LED_SEL]=0; return 7; }   /* Roland's enter / refresh: LED routine's else path */
static void setfp(int pad, unsigned end){ *(unsigned*)(padinfo+pad*32+4)=end; mock_loaded[pad]=end>512; }
static void ticks(int n){ while(n--) mute_tick(0,100); }
static int file_of(const char*p){ for(int i=0;i<4;i++) if(!strcmp(fname[i],p) && flen[i]) return i; return -1; }
static const char *PPATH = "/ROLAND/SP-404SX/SMPL/PAN.BIN";
static int job_src, job_dst;
static int job_copy(void){ memcpy(padinfo+job_dst*32, padinfo+job_src*32, 32); return 0; }
static int job_exch(void){ unsigned char t[32]; memcpy(t,padinfo+job_dst*32,32); memcpy(padinfo+job_dst*32,padinfo+job_src*32,32); memcpy(padinfo+job_src*32,t,32); return 0; }
static int job_fail(void){ return 1; }
/* a voice's feeder (cursor +16, voice +24, pad +28, as vari_fill reads them) and Roland's fill: distinct L / R signals */
struct sobj { char pad0[16]; int cursor; char pad1[4]; int voice; int pad; char rest[64]; };
static int src_len;
static int sfill(void *o_, s16 *L, s16 *R, int *flag, int *frames){
  struct sobj *o=o_; int n=*frames, i;
  for(i=0;i<n;i++){ if(o->cursor>=src_len) break; L[i]=(s16)(o->cursor*37-9000); R[i]=(s16)(20000-o->cursor*53); o->cursor++; }
  if(i<n){ for(int j=i;j<n;j++){ L[j]=0; R[j]=0; } *frames=i; return 0; }
  return 1;
}
/* expected display text for a pan value */
static void expect(int v, char *t){ int m=v<0?-v:v; if(!v) strcpy(t,"C"); else { t[0]=v<0?'L':'R'; t[1]=m>=10?(char)('0'+m/10):' '; t[2]=(char)('0'+m%10); t[3]=0; } }

int main(){
  for(int p=0;p<120;p++) setfp(p, 10000+p*1000);
  vari_need_load=1; ticks(1);                                    /* boot: loads (no files yet) */

  /* ---- opening */
  open_from_play(26);
  CHECK(tp.mode==MODE_PAN && chr_st.menu==2 && chr_st.mpad==26, "SHIFT + START/END/LEVEL on the play page: pan menu for the current pad");
  CHECK(!strcmp(shbuf,"C"), "  shows C for a pad at centre");
  { struct event e={0,BTN_SEL,1}; mock_cur=26; mock_page[0]=VT_PLAY; chr_page_event(0,&e,pev_orig); tp.mode=1; draw();
    CHECK(tp.mode==1 && chr_st.want==0, "START/END/LEVEL alone on the play page: the START/END/LEVEL view (Roland's)"); }
  { struct event e={0,BTN_SEL,1}; mock_fwdmode=1; mock_shift=1; mock_page[0]=VT_PLAY; chr_page_event(0,&e,pev_orig); mock_shift=0; mock_fwdmode=0;
    CHECK(chr_st.want==0, "FWD mode: SHIFT + START/END/LEVEL on the play page doesn't ask for the menu (Roland's BWD)"); }
  for(int m=1;m<=9;m++){
    chr_st.menu = m==9 ? 1 : 0; tp.mode=m; mock_cur=30;
    int r=shift_btn(BTN_SEL);
    char what[96]; snprintf(what,sizeof what,"SHIFT + START/END/LEVEL in TIME/BPM page mode %d%s: pan menu, taken", m, m==9?" (chromatic menu)":"");
    CHECK(r==1 && tp.mode==MODE_PAN && chr_st.menu==2 && chr_st.mpad==30, what);
  }
  tp.mode=5; chr_st.menu=0; mock_fwdmode=1; shift_btn(BTN_SEL); mock_fwdmode=0;
  CHECK(tp.mode!=MODE_PAN && chr_st.menu!=2, "FWD mode on the TIME/BPM page: no pan menu");
  tp.mode=5; mock_cur=-1; shift_btn(BTN_SEL);
  CHECK(tp.mode!=MODE_PAN, "no current pad: no pan menu");

  /* ---- CTRL 3 */
  tp.mode=5; mock_cur=26; shift_btn(BTN_SEL);
  int val[128], mono=1, sym=1, txt=1, ends, every=1, seen[101]={0};
  for(int k=0;k<128;k++){
    int r=knob(2,k); val[k]=pan_tab.v[26];
    char e[8]; expect(val[k],e); if(strcmp(shbuf,e)) txt=0;
    if(r!=1) { CHECK(0,"a CTRL 3 move reached Roland"); break; }
    if(k && val[k]<val[k-1]) mono=0;
    seen[val[k]+50]=1;
  }
  for(int k=0;k<128;k++) if(val[k] != -val[127-k]) sym=0;
  for(int i=0;i<101;i++) if(!seen[i]) every=0;
  ends = val[0]==-50 && val[127]==50 && val[61]==-1 && val[66]==1 && val[62]==0 && val[63]==0 && val[64]==0 && val[65]==0;
  CHECK(ends, "CTRL 3: L50 fully left, R50 fully right, centre detent 62..65, L 1 / R 1 next to it");
  CHECK(mono && sym && every, "CTRL 3: only goes right as the knob turns right, symmetric, every value -50..+50 reachable");
  CHECK(txt, "display for all 128 positions: L50 .. L 1, C, R 1 .. R50");
  knob(2,0); CHECK(!strcmp(shbuf,"L50"), "  fully left shows L50");
  knob(2,127); CHECK(!strcmp(shbuf,"R50"), "  fully right shows R50");
  knob(2,90); int v90=pan_tab.v[26];
  CHECK(knob(0,10)==1 && knob(1,10)==1 && pan_tab.v[26]==v90, "CTRL 1 / CTRL 2: taken, pan unchanged");
  /* no jump on open */
  tp.mode=5; chr_st.menu=0; shift_btn(BTN_SEL); draw();
  { char e[8]; expect(v90,e); CHECK(pan_tab.v[26]==v90 && !strcmp(shbuf,e), "opening again shows the stored value (no jump to the knob)"); }

  /* ---- LEDs */
  roland_pg=0; pan_led(&tp, mock_pg);
  CHECK(roland_pg==1 && mock_led[LED_SEL]==2 && mock_led[LED_BPM]==0, "pan menu: Roland's enter / refresh, then START/END/LEVEL blinks, TIME/BPM off");
  tp.mode=9; chr_st.menu=1; roland_pg=0; pan_led(&tp, mock_pg);
  CHECK(roland_pg==1 && mock_led[LED_BPM]==2 && mock_led[LED_SEL]==0, "chromatic menu: LEDs as Roland's routine leaves them (TIME/BPM blinks)");
  for(int m=1;m<=8;m++){ tp.mode=m; chr_st.menu=0; mock_led[LED_BPM]=7; mock_led[LED_SEL]=7; pan_led(&tp, mock_pg); }
  CHECK(mock_led[LED_BPM]==2 && mock_led[LED_SEL]==0, "modes 1..8: LEDs as Roland's routine leaves them");
  tp.mode=MODE_PAN; chr_st.menu=0; pan_led(&tp, mock_pg);
  CHECK(mock_led[LED_BPM]==2 && mock_led[LED_SEL]==0, "a mode 10 left over with the menu closed: no change");

  /* ---- closing */
  tp.mode=5; mock_cur=26; mock_bank=2; shift_btn(BTN_SEL);           /* pad 26 = bank C pad 3 */
  CHECK(ev(0,3,1)==0 && chr_st.menu==2 && tp.mode==MODE_PAN, "the edited pad: Roland's (plays, keeps the page), menu stays");
  CHECK(ev(0,3,0)==0 && ev(0,BTN_SHIFT,1)==0 && chr_st.menu==2, "releases and SHIFT: Roland's, menu stays");
  CHECK(shift_btn(BTN_SEL)==1 && chr_st.menu==2 && tp.mode==MODE_PAN, "SHIFT + START/END/LEVEL in the pan menu: stays");
  mock_setpage_slot=-1;
  CHECK(btn(BTN_SEL)==1 && chr_st.menu==0 && mock_setpage_slot==1, "START/END/LEVEL alone: closes, main screen");
  shift_btn(BTN_SEL);
  CHECK(ev(0,4,1)==0 && chr_st.menu==0, "another pad: menu closes, Roland's (leaves the page for that pad)");
  draw(); CHECK(tp.mode==1, "  a mode 10 left over: START/END/LEVEL view on the next draw");
  tp.mode=5; shift_btn(BTN_SEL);
  CHECK(btn(BTN_BPM)==0 && chr_st.menu==0, "another button (TIME/BPM): menu closes, Roland's");
  /* a chromatic bank's key */
  memset(&chr_tab,0,sizeof chr_tab); chr_tab.b[2].src=25; chr_tab.b[2].fp=pad_fp(24); chr_tab.b[2].ch=5;
  tp.mode=5; mock_cur=26; shift_btn(BTN_SEL);
  CHECK(ev(0,7,1)==1 && chr_st.menu==2, "a key of a chromatic bank: taken (plays as a key), menu stays");
  memset(&chr_tab,0,sizeof chr_tab);
  /* to and from the chromatic menu */
  tp.mode=5; mock_cur=26; shift_btn(BTN_SEL);
  CHECK(shift_btn(BTN_BPM)==1 && chr_st.menu==1 && tp.mode==MODE_CHR, "SHIFT + TIME/BPM in the pan menu: chromatic menu");
  CHECK(shift_btn(BTN_SEL)==1 && chr_st.menu==2 && tp.mode==MODE_PAN, "SHIFT + START/END/LEVEL in the chromatic menu: pan menu");
  memset(&chr_tab,0,sizeof chr_tab);
  chr_page_enter(0, mock_pg);
  CHECK(chr_st.menu==0, "play page entered: pan menu closed");

  /* ---- PAN.BIN */
  memset(pan_tab.v,0,sizeof pan_tab.v); ticks(15);
  tp.mode=5; mock_cur=10; shift_btn(BTN_SEL); knob(2,0);           /* pad 10: L50 */
  mock_cur=20; shift_btn(BTN_SEL); knob(2,127);                    /* pad 20: R50 */
  mock_cur=30; shift_btn(BTN_SEL); knob(2,40); int v30=pan_tab.v[30];
  fwrites=0; ticks(5); CHECK(fwrites==0, "not written within a second of the change");
  ticks(8); int f=file_of(PPATH);
  CHECK(f>=0 && flen[f]==244, "PAN.BIN written about a second after the change: 244 bytes");
  if(f>=0){ unsigned m; memcpy(&m,fdata[f],4);
    CHECK(m==PAN_MAGIC && (s8)fdata[f][4+10]==-50 && (s8)fdata[f][4+20]==50 && (s8)fdata[f][4+30]==v30 && fdata[f][124+10]==pad_fp(10),
          "  file: PAN1, the pans, the fingerprints"); }
  memset(pan_tab.v,0,sizeof pan_tab.v); vari_need_load=1; ticks(1);
  CHECK(pan_tab.v[10]==-50 && pan_tab.v[20]==50 && pan_tab.v[30]==v30, "loaded back");
  setfp(30,333333); vari_need_load=1; ticks(1);
  CHECK(pan_tab.v[30]==0 && pan_tab.v[10]==-50, "a sample changed while away: that pad at centre after loading");
  setfp(10,0); ticks(1); CHECK(pan_tab.v[10]==0 && pan_tab.v[20]==50, "sample deleted: centre, the others keep theirs");
  setfp(20,222222); ticks(1); CHECK(pan_tab.v[20]==0, "recorded over / imported: centre");
  /* copy / exchange */
  pan_tab.v[40]=-25; pan_tab.v[60]=33; ticks(15);
  prm[60]=40; prm[61]=50; job_src=40; job_dst=50;
  CHECK(!(vari_xop(job_copy,0)&0xFF) && pan_tab.v[50]==-25 && pan_tab.v[40]==-25, "copy: the copy gets the pan, the source keeps it");
  ticks(3); CHECK(pan_tab.v[50]==-25, "  and keeps it");
  prm[60]=40; prm[61]=60; job_src=40; job_dst=60; vari_xop(job_exch,1);
  CHECK(pan_tab.v[40]==33 && pan_tab.v[60]==-25, "exchange: the pans swap");
  pan_tab.v[50]=7; prm[60]=60; prm[61]=50; job_src=60; job_dst=50; vari_xop(job_exch,1);   /* 60 and 50: the same sample */
  CHECK(pan_tab.v[60]==7 && pan_tab.v[50]==-25, "exchange of two identical samples: pans swap too");
  prm[60]=40; prm[61]=50; int b50=pan_tab.v[50];
  CHECK((vari_xop(job_fail,0)&0xFF) && pan_tab.v[50]==b50, "a copy that fails: nothing moves");
  /* invalid files */
  ticks(15); f=file_of(PPATH); memcpy(fdata[f],"XXXX",4); vari_need_load=1; ticks(1);
  int none=1; for(int p=0;p<120;p++) if(pan_tab.v[p]) none=0;
  CHECK(none, "another magic: everything at centre");
  pan_tab.v[40]=12; pan_tab.dirty=1; ticks(15); f=file_of(PPATH);
  fdata[f][4+40]=51; vari_need_load=1; ticks(1); CHECK(pan_tab.v[40]==0, "a pan beyond 50 in the file: centre");
  pan_tab.v[40]=12; pan_tab.dirty=1; ticks(15); f=file_of(PPATH);
  flen[f]=200; vari_need_load=1; ticks(1); CHECK(pan_tab.v[40]==0, "a short file: centre");

  /* ---- sound (build B: stereo pads) */
  {
    enum { F = 512 };
    static s16 L0[F], R0[F], L1[F], R1[F];
    int ok, fr0, fr1, flag = 0;
    struct sobj o0, o1;
    #define RUN(pad_, pan_, speed_, len_) do { \
      memset(&o0,0,sizeof o0); memset(&o1,0,sizeof o1); o0.pad=o1.pad=(pad_); o0.voice=o1.voice=2; src_len=(len_); \
      vari_tab.e[(pad_)].pos=(s8)(speed_); memset(vari_vs,0,sizeof vari_vs); pan_tab.v[(pad_)]=0; \
      fr0=F; vari_fill(&o0,L0,R0,&flag,&fr0,sfill); \
      memset(vari_vs,0,sizeof vari_vs); pan_tab.v[(pad_)]=(s8)(pan_); \
      fr1=F; pan_fill(&o1,L1,R1,&flag,&fr1,sfill); } while (0)
    padinfo[44*32+22]=2; padinfo[45*32+22]=1;                       /* pad 44 stereo, pad 45 mono */
    RUN(44, 0, 0, 100000);
    CHECK(fr0==fr1 && !memcmp(L0,L1,sizeof L0) && !memcmp(R0,R1,sizeof R0), "centre: bit for bit what vari_fill makes (normal speed)");
    RUN(44, 0, 40, 100000);
    CHECK(!memcmp(L0,L1,sizeof L0) && !memcmp(R0,R1,sizeof R0), "centre with varispeed: bit for bit what vari_fill makes");
    RUN(44, -50, 0, 100000);
    ok=1; for(int i=0;i<F;i++) if(L1[i]!=L0[i] || R1[i]!=0) ok=0;
    CHECK(ok, "L50: right channel silent, left channel untouched");
    RUN(44, 50, 0, 100000);
    ok=1; for(int i=0;i<F;i++) if(R1[i]!=R0[i] || L1[i]!=0) ok=0;
    CHECK(ok, "R50: left channel silent, right channel untouched");
    RUN(44, -25, 0, 100000);
    ok=1; for(int i=0;i<F;i++) if(L1[i]!=L0[i] || R1[i]!=(s16)((R0[i]*(25*655))>>15)) ok=0;
    CHECK(ok, "L25: right channel x 25 * 655 / 32768 (about half), left untouched");
    RUN(44, 1, 40, 100000);
    ok=1; for(int i=0;i<F;i++) if(R1[i]!=R0[i] || L1[i]!=(s16)((L0[i]*(49*655))>>15)) ok=0;
    CHECK(ok, "R 1 with varispeed: left channel x 0.98 of what vari_fill makes, right untouched");
    ok=1; for(int i=0;i<F;i++) if((L0[i]<0) != (L1[i]<0) && L1[i]) ok=0;
    CHECK(ok, "  no sign flips (no overflow)");
    for(int i=0;i<12;i++){ mock_voices[i].player=mock_players[i]; mock_players[i][1]=1; }
    CHECK(sizeof mock_voices[0]==24, "  (mock voice objects are 24 bytes)");
    /* starting a voice */
    pan_tab.v[45]=-30;
    CHECK(pan_channels(1,45)==2 && pan_channels(2,45)==2, "voice start: a panned mono pad starts as 2 channels, stereo stays 2");
    pan_tab.v[45]=0;
    CHECK(pan_channels(1,45)==1 && pan_channels(1,200)==1 && pan_channels(1,-1)==1, "  centre, or no valid pad: as it is");
    /* a mono pad playing as one channel (started at centre, panned since): untouched */
    RUN(45, -50, 0, 100000);
    CHECK(!memcmp(L0,L1,sizeof L0) && !memcmp(R0,R1,sizeof R0), "mono pad on a 1-channel voice: untouched (panned from its next start)");
    /* a mono pad started panned: 2 channels, R made from L */
    mock_players[2][1]=2;
    int tq = MONO_TRIM, ok2;
    #define NEAR(x) (tq < 32768 ? (s16)(((x) * tq) >> 15) : (s16)(x))
    #define FAR(x, p) ((s16)(((x) * (tq < 32768 ? (((50 - (p)) * 655 * tq) >> 15) : (50 - (p)) * 655)) >> 15))
    RUN(45, -50, 0, 100000);
    ok2=1; for(int i=0;i<F;i++) if(L1[i]!=NEAR(L0[i]) || R1[i]!=0) ok2=0;
    CHECK(ok2, "mono L50: left = the mono signal (x trim), right silent");
    RUN(45, 50, 40, 100000);
    ok2=1; for(int i=0;i<F;i++) if(R1[i]!=NEAR(L0[i]) || L1[i]!=0) ok2=0;
    CHECK(ok2, "mono R50 with varispeed: right = the mono signal, left silent");
    RUN(45, -25, 0, 100000);
    ok2=1; for(int i=0;i<F;i++) if(L1[i]!=NEAR(L0[i]) || R1[i]!=FAR(L0[i],25)) ok2=0;
    CHECK(ok2, "mono L25: right = the mono signal x 25 * 655 / 32768 (x trim)");
    RUN(45, 1, 0, 100000);
    ok2=1; for(int i=0;i<F;i++) if(R1[i]!=NEAR(L0[i]) || L1[i]!=FAR(L0[i],1)) ok2=0;
    CHECK(ok2, "mono R 1: left x 0.98, right the mono signal");
    RUN(45, 0, 0, 100000);
    ok2=1; for(int i=0;i<F;i++) if(L1[i]!=NEAR(L0[i]) || R1[i]!=L1[i]) ok2=0;
    CHECK(ok2, "mono voice started panned, now at C: both channels the mono signal");
    RUN(45, -50, 0, 200);
    ok2 = fr1==fr0 && fr1<F;
    for(int i=0;i<fr1;i++) if(R1[i]!=0) ok2=0;
    CHECK(ok2, "mono, end of sample: the frames made");
    mock_players[2][1]=1;
    RUN(44, -50, 0, 200);                                          /* the sample ends inside the block */
    ok = fr1==fr0 && fr1<F;
    for(int i=0;i<fr1;i++) if(R1[i]!=0 || L1[i]!=L0[i]) ok=0;
    for(int i=fr1;i<F;i++) if(R1[i]!=R0[i]) ok=0;
    CHECK(ok, "end of sample: only the frames made are scaled");
    RUN(44, 0, 0, 100000);                                         /* reference block for the next check */
    { struct sobj o; memset(&o,0,sizeof o); o.pad=200; o.voice=2; src_len=100000; int fr=F;
      pan_fill(&o,L1,R1,&flag,&fr,sfill);
      CHECK(fr==F && !memcmp(L0,L1,sizeof L0) && !memcmp(R0,R1,sizeof R0), "a pad number out of range: Roland's fill as is, no pan"); }
    vari_tab.e[44].pos=0; pan_tab.v[44]=0;
  }

  printf("%d failures\n", fails);
  return fails != 0;
}
