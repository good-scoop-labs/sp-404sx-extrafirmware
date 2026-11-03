/* Host test: chromatic mode per bank (menu, MIDI channels, octaves, keys, MIDI notes, releases, loops, pages,
   saving, pitch). Roland functions are replaced by mocks (mock_chr.h); vari.c is compiled for the PC. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static int prm[64], patt=1200; static const char *shs=0; static char shbuf[8];
static int mock_cur=26, mock_bank=2, mock_midich=0;
static int g(int id,int pad){ if(id==52) return mock_cur; if(id==53) return mock_bank; if(id==2) return mock_midich; return prm[id]; }
static int dv(int a,int b){return a/b;}
static int pt(void){ return patt*10; }
static void sn(int n){ shs=0; }
static void ss(const char*s,int f){ shs=s; if((unsigned long)s>=0x0C080000 && (unsigned long)s<0x0C140000) strcpy(shbuf,"(rol)"); else strncpy(shbuf,s,7); }   /* Roland's own strings: not on the PC */
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
/* a tiny card: one file per path */
static char fname[2][40]; static unsigned char fdata[2][256]; static int flen[2], fwrites;
static int slot_of(const char*p){ for(int i=0;i<2;i++) if(!strcmp(fname[i],p)) return i; for(int i=0;i<2;i++) if(!fname[i][0]){ strcpy(fname[i],p); return i; } return 0; }
static void lk(int n){}
static int cr(const char*p){ int s=slot_of(p); flen[s]=0; return s; }
static int op(const char*p,int m){ for(int i=0;i<2;i++) if(!strcmp(fname[i],p) && flen[i]) return i; return -1; }
static int rd(int f,void*b,int n){ int k=n<flen[f]?n:flen[f]; memcpy(b,fdata[f],k); return k; }
static int wr(int f,const void*b,int n){ memcpy(fdata[f],b,n); flen[f]=n; fwrites++; return n; }
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
#include "vari.c"
#include "ratio.c"
static unsigned mock_ratio_now(int pad) { return chr_ratio(pad, pad_ratio(pad)); }

static int fails=0;
#define CHECK(c, what) do { int ok_=(c); printf("%s: %s\n", ok_?"ok  ":"FAIL", what); if(!ok_) fails++; } while(0)
static int near(unsigned r, int semis){ return r && fabs(1200*log2(r/(65536*pow(2,semis/12.0)))) < 0.1; }   /* within 0.1 cent */
static int note(int kind, int pad){ return chr_request(0, kind, pad, 100); }        /* note handler's play request */
static int orig_notes; static unsigned char orig_ev[4];
static int orig_note(unsigned char *ev){ orig_notes++; memcpy(orig_ev,ev,4); return 0; }   /* Roland's note handler */
static int midi(int ch, int n, int vel){ unsigned char ev[4]={(unsigned char)n,3,(unsigned char)ch,(unsigned char)vel}; return chr_note(ev,orig_note); }
static int pevs; static int pev_orig(void *page, struct event *e){ pevs++; return 0; }
static void play_btn(int id, int val){ struct event e={0,(unsigned char)id,(signed char)val}; mock_page[0]=VT_PLAY; chr_page_event(0,&e,pev_orig); }
static struct page tp;                                      /* the TIME/BPM page */
static int edit_ev(int type, int id, int val){ struct event e={type,(unsigned char)id,(signed char)val}; mock_page[0]=VT_EDIT; return vari_ev(&tp,&e); }
static void edit_draw(void){ mock_page[0]=VT_EDIT; shs=0; vari_display(&tp); }
/* SHIFT + TIME/BPM on the play page, then the TIME/BPM page Roland opens draws for the first time */
static void open_menu(int curpad){ mock_cur=curpad; mock_shift=1; play_btn(40,1); mock_shift=0; tp.mode=5; edit_draw(); }
static void setfp(int pad, unsigned end){ *(unsigned*)(padinfo+pad*32+4)=end; mock_loaded[pad]=end>512; }
static int pgcalls; static int mock_pg(void *page){ pgcalls++; return 5; }   /* Roland's enter / refresh */
static int gate_answer, gate_calls; static int mock_gate_fn(void){ gate_calls++; return gate_answer; }   /* Roland's MIDI note gate */
static int gate(int kind, int ch, int n){ unsigned char ev[4]={(unsigned char)n,(unsigned char)kind,(unsigned char)ch,100}; return chr_gate(ev,mock_gate_fn); }
static const char *CPATH = "/ROLAND/SP-404SX/SMPL/CHROMA.BIN";
static int asked=-1; static int g2(int id,int pad){ asked=pad; return id; }   /* Roland's R_GET, for the HOLD lookup */

int main(){
  setfp(26,40000); setfp(30,50000); setfp(13,60000); setfp(70,30000);     /* C3, C7, B2, F11 have samples */
  chr_tick(0,100);                                          /* boot: loads (no CHROMA.BIN yet) */
  CHECK(chr_tab.magic==CHR_MAGIC && !chr_tab.b[2].src, "boot without CHROMA.BIN: every bank off");
  /* off: everything stock */
  mock_page[0]=VT_PLAY; note(6,30); CHECK(mock_play_pad==30 && mock_attack==UNITY, "off: C7 plays itself");
  note(6,25); CHECK(mock_play_pad==-2, "off: an empty pad plays nothing");
  CHECK(midi(5,60,100)==0 && orig_notes==1, "off: MIDI notes go to Roland");
  /* opening the menu */
  mock_shift=0; play_btn(40,1); CHECK(chr_st.want==0, "TIME/BPM alone: no menu");
  mock_shift=1; mock_fwdmode=1; play_btn(40,1); CHECK(chr_st.want==0, "FWD mode: Roland's SHIFT + TIME/BPM untouched");
  mock_fwdmode=0; pevs=0; play_btn(40,1); CHECK(chr_st.want==1 && pevs==1, "SHIFT + TIME/BPM: menu asked, Roland still opens the TIME/BPM page");
  play_btn(5,1); CHECK(chr_st.want==0, "any other button press cancels the request (page didn't open)");
  mock_shift=0; mock_midich=0; open_menu(26);
  CHECK(tp.mode==MODE_CHR && chr_tab.b[2].src==27 && chr_tab.b[2].ch==2 && chr_tab.b[2].mode==2 && chr_st.dirty,
        "menu for C3: bank C on, source C3, MIDI channel 3 (SP uses 1 and 2), mono, octave 0");
  CHECK(!strcmp(shbuf,"C 3"), "display: C 3");
  { mock_shift=1; mock_held=0; struct page q={0,7,0,0}; struct event e={0,40,1}; mock_cur=26; mock_page[0]=VT_EDIT;
    CHECK(vari_ev(&q,&e)==1 && q.mode==MODE_CHR, "SHIFT + TIME/BPM while already on the TIME/BPM page: menu"); mock_shift=0; }
  /* knobs */
  edit_ev(1,1,127); CHECK(!(chr_tab.b[2].mode&8) && !strcmp(shbuf,"MON"), "CTRL 2 right: still MON (poly not offered yet)");
  edit_ev(1,1,10); CHECK(!(chr_tab.b[2].mode&8) && !strcmp(shbuf,"MON"), "CTRL 2 left: MON");
  { int bad=0; for(int v=0;v<128;v++){ edit_ev(1,1,v); if((chr_tab.b[2].mode&8) || strcmp(shbuf,"MON")) bad=1; }
    CHECK(!bad && (chr_tab.b[2].mode&7)==2, "CTRL 2 sweep: always MON, octave untouched"); }
  edit_ev(1,2,0); CHECK((chr_tab.b[2].mode&7)==0 && !strcmp(shbuf,"o-2"), "CTRL 3 left: o-2");
  edit_ev(1,2,127); CHECK((chr_tab.b[2].mode&7)==4 && !strcmp(shbuf,"o 2"), "CTRL 3 right: o 2");
  edit_ev(1,2,64); CHECK((chr_tab.b[2].mode&7)==2 && !strcmp(shbuf,"o 0"), "CTRL 3 centre: o 0");
  edit_ev(1,0,0); CHECK(!chr_tab.b[2].src && shs==STR_OFF, "CTRL 1 fully left: OFF");
  CHECK(edit_ev(0,5,1)==0 && tp.mode==MODE_CHR, "menu with the bank OFF: a pad press goes to Roland (which leaves the page for another pad)");
  CHECK(edit_ev(0,5,0)==0, "and its release too");
  mock_page[0]=VT_PLAY; note(6,25); CHECK(mock_play_pad==-2, "OFF: bank C pads are plain pads again");
  edit_ev(1,0,127); CHECK(chr_tab.b[2].src==27 && chr_tab.b[2].ch==15 && !strcmp(shbuf,"C16"), "CTRL 1 fully right: last free channel, C16");
  edit_ev(1,0,10); CHECK(chr_tab.b[2].ch==2 && !strcmp(shbuf,"C 3"), "CTRL 1 just right of OFF: first free channel");
  { int seen=0, bad=0, last=-2; for(int v=0;v<128;v++){ edit_ev(1,0,v); int c=chr_tab.b[2].src?chr_tab.b[2].ch:-1;
      if(c==0||c==1) bad=1; if(c!=last){ seen++; last=c; } }
    CHECK(!bad && seen==15, "CTRL 1 sweep: OFF + the 14 free channels, never the SP's own"); }
  edit_ev(1,0,10);
  mock_midich=-1; { int ok=0; for(int v=0;v<128;v++){ edit_ev(1,0,v); if(chr_tab.b[2].src && chr_tab.b[2].ch==0) ok=1; }
    CHECK(ok, "SP's MIDI off: channel 1 is free too"); } mock_midich=0; edit_ev(1,0,10);
  CHECK(edit_ev(0,5,1)==1, "menu with the bank chromatic: a pad press doesn't leave the page (the key plays)");
  mock_setpage_slot=-1; CHECK(edit_ev(0,40,1)==1 && !chr_st.menu && mock_setpage_slot==1, "menu: TIME/BPM leaves for the main screen");
  open_menu(26); mock_setpage_slot=-1; mock_shift=1;
  CHECK(edit_ev(0,40,1)==1 && !chr_st.menu && mock_setpage_slot==1, "menu: SHIFT + TIME/BPM leaves too (not taken by Roland)"); mock_shift=0;
  mock_setpage_slot=-1; CHECK(edit_ev(0,40,0)==0 && mock_setpage_slot==-1, "TIME/BPM release: nothing");
  tp.mode=MODE_CHR; edit_draw(); CHECK(tp.mode==7, "a left-over mode 9 later falls back to the CTRL 1 view");
  /* keys */
  mock_page[0]=VT_PLAY;
  { int all=1; for(int k=0;k<12;k++){ note(6,24+k); if(mock_play_pad!=26 || !near(mock_attack,k) || chr_st.key[2]!=25+k) all=0; }
    CHECK(all, "bank C pads 1..12 play C3 at 0..+11 (empty pads too), from the first block on"); }
  note(6,24); CHECK(mock_attack==UNITY, "pad 1 = the sample's own pitch, exactly");
  open_menu(26); edit_ev(1,2,127); edit_ev(0,40,1);
  mock_page[0]=VT_PLAY; note(6,24); CHECK(mock_attack==UNITY*4, "octave +2: pad 1 = +24 (4x)");
  note(6,35); CHECK(near(mock_attack,35), "octave +2: pad 12 = +35");
  open_menu(26); edit_ev(1,2,0); edit_ev(0,40,1);
  mock_page[0]=VT_PLAY; note(6,24); CHECK(mock_attack==UNITY/4, "octave -2: pad 1 = -24 (1/4)");
  open_menu(26); edit_ev(1,2,64); edit_ev(0,40,1);
  mock_page[0]=VT_PLAY;
  note(6,30); CHECK(mock_play_pad==26 && near(mock_attack,6), "C7 (with its own sample) is a key too: +6");
  note(6,70); CHECK(mock_play_pad==70 && mock_attack==UNITY, "other banks play as stock");
  note(1,30); CHECK(mock_play_pad==30, "pattern playback is never remapped");
  note(6,31); chr_play(6,26,100); CHECK(mock_play_pad==26 && near(mock_attack,7), "Roland restarting the source (MARK, panel kind, no key): keeps the note (+7)");
  chr_play(2,26,100); CHECK(mock_attack==UNITY, "pattern playback of the source: its own pitch");
  note(6,31); chr_play(1,26,100); CHECK(mock_attack==UNITY, "any other kind of start: its own pitch");
  note(6,31); mock_page[0]=0x0C12EF24u; chr_play(6,26,100); CHECK(mock_attack==UNITY, "a panel start of the source on another page: its own pitch");
  mock_page[0]=VT_PLAY;
  note(3,28); CHECK(mock_play_pad==26 && near(mock_attack,4), "MIDI on the SP's own channel hitting a key: +4");
  /* TIME/BPM and START/END/LEVEL views: keys keep the page; other banks as stock */
  mock_bank=2; tp.mode=7; CHECK(edit_ev(0,5,1)==1, "TIME/BPM view: a key of the chromatic bank keeps the page");
  tp.mode=1; CHECK(edit_ev(0,9,1)==1, "START/END/LEVEL view: a key keeps the page");
  CHECK(edit_ev(0,9,0)==0, "its release goes to Roland");
  mock_bank=5; tp.mode=7; CHECK(edit_ev(0,5,1)==0, "a pad of a normal bank: to Roland (stock: leaves for that pad)");
  mock_bank=2; mock_page[0]=VT_PLAY;
  /* HOLD: GATE is asked for the pressed pad; keys answer with the source's */
  { mock_page[0]=VT_PLAY; chr_holdget(38,29,g2); CHECK(asked==26, "HOLD's GATE lookup for a key: the source's (C3)");
    chr_holdget(38,70,g2); CHECK(asked==70, "for a normal pad: its own");
    mock_page[0]=0x0C12EFE0u; chr_holdget(38,29,g2); CHECK(asked==29, "pattern mode: its own (stock)");
    mock_page[0]=VT_PLAY; }
  /* pages */
  mock_page[0]=0x0C12EFE0u; note(6,30); CHECK(mock_play_pad==30, "pattern mode (pattern select page): stock pads");
  mock_page[0]=VT_RESMP; mock_page[2]=1; note(6,30); CHECK(mock_play_pad==30, "resample, choosing destination: stock pads");
  mock_page[2]=5; note(6,30); CHECK(mock_play_pad==26, "resample REC: keys play");
  mock_page[0]=VT_EDIT; note(6,31); CHECK(mock_play_pad==26 && near(mock_attack,7), "TIME/BPM page: keys play");
  mock_page[0]=VT_PLAY; mock_page[2]=0;
  /* releases (gate) */
  note(6,31); mock_relpad=-2;
  chr_release(28); CHECK(mock_relpad==-2, "releasing another key does nothing");
  chr_release(31); CHECK(mock_relpad==26, "releasing the sounding key releases the source");
  chr_release(70); CHECK(mock_relpad==70, "other bank: stock release");
  /* MIDI on the bank's channel */
  orig_notes=0;
  midi(2,60,100); CHECK(mock_play_pad==26 && mock_attack==UNITY && orig_notes==0, "MIDI ch 3 note 60 (middle C): own pitch");
  midi(2,72,100); CHECK(near(mock_attack,12), "note 72: +12");
  midi(2,95,100); CHECK(near(mock_attack,35), "note 95 (B6): +35");
  mock_play_pad=-9; midi(2,96,100); CHECK(mock_play_pad==-9 && orig_notes==0, "note 96 and above: silent");
  midi(2,127,100); CHECK(mock_play_pad==-9, "note 127: silent");
  midi(2,0,100); CHECK(near(mock_attack,-60), "note 0: -60 (1/32 speed)");
  midi(2,48,100); mock_relpad=-2;
  midi(2,50,0); CHECK(mock_relpad==-2, "note off of another note: nothing");
  midi(2,48,0); CHECK(mock_relpad==26, "note off of the sounding note: release");
  midi(2,48,100); mock_relpad=-2; { unsigned char ev[4]={48,3,2,255}; chr_note(ev,orig_note); } CHECK(mock_relpad==26, "release marker 255 = note off");
  midi(2,48,100); mock_relpad=-2; chr_release(24); CHECK(mock_relpad==-2, "a pad release doesn't stop a MIDI note");
  midi(4,60,100); CHECK(orig_notes==1 && orig_ev[2]==4, "MIDI on another channel: to Roland");
  { unsigned char ev[4]={60,6,2,100}; orig_notes=0; chr_note(ev,orig_note); CHECK(orig_notes==1, "panel events: to Roland"); }
  mock_page[0]=0x0C12EFE0u; midi(2,62,100); CHECK(mock_play_pad==26 && near(mock_attack,2), "MIDI chromatic works on any page");
  mock_page[0]=VT_PLAY;
  /* MIDI while resampling: Roland's gate drops every MIDI note while the sampler records */
  gate_answer=0; mock_page[0]=VT_RESMP; mock_page[2]=5;
  CHECK(gate(3,2,60)==1, "resample REC: a note on bank C's channel gets through Roland's gate");
  mock_page[2]=4; CHECK(gate(3,2,60)==1, "resample RDY: gets through");
  gate_calls=0; CHECK(gate(3,4,60)==1 && gate(3,0,60)==1 && gate_calls==0, "resample REC: any MIDI note gets through (other channels, the SP's own)");
  mock_page[2]=1; CHECK(gate(3,2,60)==0, "choosing the resample destination: Roland's answer (dropped)");
  mock_page[0]=0x0C12F4F4u; mock_page[2]=5; CHECK(gate(3,2,60)==0, "sampling (not resampling): Roland's answer");
  mock_page[0]=VT_PLAY; gate_answer=1; CHECK(gate(3,2,60)==1 && gate(3,4,60)==1, "not sampling: Roland's answer (allowed)");
  gate_answer=0; CHECK(gate(3,2,60)==0, "main screen: never forced open");
  chr_tab.b[2].src=0; mock_page[0]=VT_RESMP; mock_page[2]=5; CHECK(gate(3,2,60)==1, "resample REC: notes pass even with no chromatic bank");
  CHECK(gate(6,2,60)==0, "resample REC: panel events never come this way (Roland's answer)");
  mock_page[2]=4; gate_calls=0; CHECK(chr_gate2(mock_gate_fn)==1 && gate_calls==0, "note handlers' gate at RDY: open (the note plays and starts the recording)");
  mock_page[2]=5; CHECK(chr_gate2(mock_gate_fn)==1, "note handlers' gate at REC: open");
  mock_page[2]=1; gate_calls=0; CHECK(chr_gate2(mock_gate_fn)==0 && gate_calls==1, "note handlers' gate choosing the destination: Roland's answer");
  mock_page[0]=0x0C12F4F4u; mock_page[2]=5; CHECK(chr_gate2(mock_gate_fn)==0, "note handlers' gate while sampling: Roland's answer");
  mock_page[0]=VT_PLAY; gate_answer=1; CHECK(chr_gate2(mock_gate_fn)==1, "note handlers' gate elsewhere: Roland's answer");
  gate_answer=0;
  chr_tab.b[2].src=27; mock_page[0]=VT_RESMP; mock_page[2]=5;
  midi(2,64,100); CHECK(mock_play_pad==26 && near(mock_attack,4), "resample REC: the MIDI note then plays the source at +4");
  mock_page[2]=4; mock_kicks=0; midi(2,60,100); CHECK(mock_kicks==1 && mock_play_pad==26, "resample RDY: a chromatic MIDI note first lets the armed sampler start (as a pad does)");
  mock_kicks=0; midi(2,60,0); CHECK(mock_kicks==1, "every chromatic MIDI event does (like Roland's handler)");
  mock_kicks=0; midi(4,60,100); CHECK(mock_kicks==0 && orig_notes>0, "other channels: Roland's own handler does it");
  mock_page[0]=VT_PLAY; mock_page[2]=0;
  /* a second chromatic bank */
  open_menu(13); edit_ev(0,40,1);
  CHECK(chr_tab.b[1].src==14 && chr_tab.b[1].ch==3 && chr_tab.b[2].ch==2, "menu for B2: bank B on, next free channel (4)");
  mock_page[0]=VT_PLAY;
  note(6,17); CHECK(mock_play_pad==13 && near(mock_attack,5), "bank B keys play B2");
  note(6,27); CHECK(mock_play_pad==26 && near(mock_attack,3), "bank C keys still play C3");
  CHECK(near(chr_ratio(13,UNITY),5) && near(chr_ratio(26,UNITY),3), "each source keeps its own pitch");
  midi(3,67,100); CHECK(mock_play_pad==13 && near(mock_attack,7), "MIDI ch 4 plays bank B");
  open_menu(13); { int bad=0; for(int v=0;v<128;v++){ edit_ev(1,0,v); if(chr_tab.b[1].src && chr_tab.b[1].ch==2) bad=1; }
    CHECK(!bad, "bank B's CTRL 1 never offers bank C's channel"); edit_ev(1,0,10); edit_ev(0,40,1); }
  open_menu(26); CHECK(chr_tab.b[2].src==27 && chr_tab.b[2].ch==2, "reopening bank C's menu keeps its settings"); edit_ev(0,40,1);
  open_menu(30); CHECK(chr_tab.b[2].src==31, "menu opened on C7: C7 is bank C's new source"); edit_ev(0,40,1);
  open_menu(26); edit_ev(0,40,1);
  /* loops (mono): another key switches note, the sounding one stops it */
  mock_loop=1; mock_gate=0; mock_playing=-1; mock_page[0]=VT_PLAY; note(6,24);
  mock_stops=0; mock_vfree=0; note(6,29); CHECK(mock_playing==26 && near(mock_attack,5) && mock_stops==1 && mock_vfree==11, "loop: another key switches note");
  mock_stops=0; note(6,29); CHECK(mock_play_pad==-3 && mock_playing==-1 && mock_stops==0, "loop: same key again stops it");
  mock_loop=0; mock_playing=-1;
  /* pitch: every semitone, any speed, clamps */
  { int ok=1; for(int k=4;k<=S0+S_MAX;k++){ chr_st.cur[2]=(unsigned char)k; for(u32 r=0x4000;r<=R_MAX;r+=0x1357){ u32 x=chr_ratio(26,r);
      double w=r*pow(2,(k-S0)/12.0); if(w>V_MAX) w=V_MAX; if(x==0||x>V_MAX||fabs(x-w)>w/4000+2) ok=0; } }
    CHECK(ok, "every pitch x every speed: right ratio, never 0, never 8x or more"); }
  { int ok=1; for(int k=0;k<256;k++){ chr_st.cur[2]=(unsigned char)k; u32 x=chr_ratio(26,UNITY); if(x==0||x>V_MAX) ok=0; }
    CHECK(ok, "any stored byte: a safe speed"); }
  chr_st.cur[2]=0;
  /* saving */
  fwrites=0; chr_st.dirty=1; chr_tick(0,600); CHECK(fwrites==0, "not written within a second of the change");
  chr_tick(0,600); CHECK(fwrites==1 && flen[slot_of(CPATH)]==(int)sizeof chr_tab, "written 1 s after the change");
  struct ctab saved=chr_tab;
  memset(&chr_tab,0,sizeof chr_tab); vari_need_load=1; chr_tick(0,100);
  CHECK(!memcmp(&chr_tab,&saved,sizeof saved), "power-on: settings come back from CHROMA.BIN");
  CHECK(chr_st.cur[2]==0, "and every source starts at its own pitch");
  setfp(13,61000); chr_tick(0,100); CHECK(!chr_tab.b[1].src && chr_st.dirty, "B2's sample replaced: bank B turns off (saved later)");
  setfp(13,60000);
  fdata[slot_of(CPATH)][4+4*0]=200; vari_need_load=1; chr_tick(0,100);
  CHECK(!chr_tab.b[0].src, "invalid entry in the file: that bank off");
  { int ok=1; srand(7); for(int i=0;i<300;i++){ unsigned char *f=fdata[slot_of(CPATH)]; for(int j=4;j<44;j++) f[j]=(unsigned char)rand();
      vari_need_load=1; chr_tick(0,100);
      for(int b=0;b<10;b++){ struct cbank *c=&chr_tab.b[b]; if(c->src && (c->src>120 || (c->src-1)/12!=b || c->ch>15 || ((c->mode&7)>4 || c->mode>15))) ok=0; } }
    CHECK(ok, "random file contents: only valid banks survive"); }
  memcpy(&chr_tab,&saved,sizeof saved); chr_tab.b[1].src=0;
  /* C.ON on the play page */
  mock_bank=2; mock_msgs=0; pgcalls=0; chr_page_enter(0,mock_pg); chr_page_refresh(0,mock_pg);
  CHECK(mock_msgs==1 && mock_msg==chr_on_s && pgcalls==2, "main screen on a chromatic bank: C.ON (after Roland's enter and refresh)");
  chr_page_refresh(0,mock_pg); CHECK(mock_msgs==1, "drawn once, not every refresh");
  note(6,25); chr_page_refresh(0,mock_pg); CHECK(mock_msgs==2, "after a chromatic note: C.ON again");
  mock_bank=5; chr_page_enter(0,mock_pg); mock_msgs=0; chr_page_refresh(0,mock_pg); CHECK(mock_msgs==0, "on a normal bank: nothing drawn");
  printf(fails?"%d FAILED\n":"all ok\n",fails); return fails!=0;
}
