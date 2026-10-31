/* Host test: VARISPD.BIN persistence (boot order, missing / corrupt / short file, copy, exchange,
   delete, record, edits, reload, moved samples, any stored byte gives a safe ratio). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned char padinfo[120*32];
static unsigned char card[1024]; static int card_len=-1, writes=0, locks=0;
static int g(int id,int pad){ return id==52?7:0; } static int pt(void){return 12000;}
static int dv(int a,int b){return a/b;}
static void sn(int n){} static void ss(const char*s,int f){}
static void lk(int n){ locks++; } static void ulk(int n){ locks--; }
static int fd_mode=-1, fd_pos=0;
static int cr(const char*p){ if(strcmp(p,"/ROLAND/SP-404SX/SMPL/VARISPD.BIN")) abort(); fd_mode=1; card_len=0; return 3; }
static int op(const char*p,int m){ if(card_len<0) return -1; fd_mode=0; fd_pos=0; return 3; }
static int rd(int fd,void*b,int n){ int k=card_len-fd_pos<n?card_len-fd_pos:n; memcpy(b,card+fd_pos,k); fd_pos+=k; return k; }
static int wr(int fd,const void*b,int n){ memcpy(card,b,n); card_len=n; writes++; return n; }
static void cl(int fd){ fd_mode=-1; } static int cardok(void){return 1;}
static void st(int id,int v,int pad,int f){} static int evo(void*p,void*e){return 0;} static void dsp(void*p){}
#define R_SET st
#define R_EVORIG(p,e) evo(p,e)
#define R_DISP(p) dsp(p)
static short knobs[3];
#define KNOBPOS knobs
#define R_TEMPO pt
#define DEV
#define R_GET g
#define R_DIV dv
#define R_SHOWN sn
#define R_SHOWS ss
#define R_LOCK lk
#define R_UNLOCK ulk
#define R_CREATE cr
#define R_OPEN op
#define R_READ rd
#define R_WRITE wr
#define R_CLOSE cl
#define R_CARDOK cardok
#define PADINFO ((const unsigned char*)padinfo)
#define SECT(x)
#define CAVE
#define PERSIST
#define section(x) unused
#include "vari.c"
#include "ratio.c"
static void setpad(int p,unsigned len,int ch){ memset(padinfo+p*32,0,32); *(unsigned*)(padinfo+p*32+4)=len; padinfo[p*32+22]=ch; padinfo[p*32+21]=1; }
static void ticks(int n){ for(int i=0;i<n;i++) vari_tick(1,100); }
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg); fails++;} else printf("ok:   %s\n",msg);}while(0)
int main(){ vari_need_load=1;
  for(int p=0;p<120;p++) setpad(p, p<20? 100000+p*977 : 0, 2);
  /* boot order hazard: card already has a file with speeds; ticks run before PAD_INFO is loaded */
  {
    unsigned char saved[120*32]; memcpy(saved,padinfo,sizeof saved);
    struct vtab f; memset(&f,0,sizeof f); f.magic=0x56535031;
    for(int p=0;p<120;p++) f.e[p].fp=pad_fp(p);
    f.e[2].pos=-40; memcpy(card,&f,sizeof f); card_len=sizeof f;
    memset(padinfo,0,sizeof padinfo);                       /* PAD_INFO not loaded yet */
    ticks(25);                                               /* first tick loads (need_load starts at 1) */
    CHECK(writes==0, "boot: ticks before PAD_INFO load never write");
    memcpy(padinfo,saved,sizeof saved);                      /* Roland fills PAD_INFO ...           */
    ticks(3);                                                /* ... a few ticks before the hook flag */
    vari_need_load=1; ticks(25);                             /* load hook fires                      */
    CHECK(writes==0 && vari_tab.e[2].pos==-40, "boot: speeds restored after load, card file untouched");
    card_len=-1; vari_need_load=1; ticks(1);                 /* now continue with no file on the card */
  }
  /* load with no file: all normal speed, nothing written */
  vari_need_load=1; ticks(30);
  CHECK(writes==0 && vari_tab.e[3].pos==0 && vari_tab.e[3].fp==pad_fp(3), "missing file: start fresh, no write at startup");
  /* user sets speeds on pads 3 and 5 */
  vari_tab.e[3].pos=-30; vari_tab.e[5].pos=20; vari_dirty_ms=1;
  ticks(5); CHECK(writes==0, "no write before 1 s");
  ticks(8); CHECK(writes==1 && card_len==244, "written ~1 s after the change");
  ticks(30); CHECK(writes==1, "no repeated writes when nothing changes");
  /* exchange pads 3 and 5 */
  unsigned char tmp[32]; memcpy(tmp,padinfo+3*32,32); memcpy(padinfo+3*32,padinfo+5*32,32); memcpy(padinfo+5*32,tmp,32);
  ticks(1); CHECK(vari_tab.e[3].pos==20 && vari_tab.e[5].pos==-30, "exchange: speeds follow the samples");
  /* copy pad 5 -> pad 40 (empty) */
  memcpy(padinfo+40*32,padinfo+5*32,32); ticks(1);
  CHECK(vari_tab.e[40].pos==-30 && vari_tab.e[5].pos==-30, "copy: new pad gets the source speed");
  /* delete pad 3 */
  setpad(3,0,0); ticks(1); CHECK(vari_tab.e[3].pos==0 && vari_tab.e[3].fp==0, "delete: speed reset");
  /* record a new sample onto pad 40 */
  setpad(40,555555,1); ticks(1); CHECK(vari_tab.e[40].pos==0, "new recording: speed reset");
  /* edit marks on pad 5 (loop start/end) must not reset it */
  *(unsigned*)(padinfo+5*32+8)=1234; padinfo[5*32+16]=99; ticks(1); CHECK(vari_tab.e[5].pos==-30, "mark/level edits keep the speed");
  ticks(15); int w=writes;
  /* power cycle: RAM lost, reload from card */
  memset(&vari_tab,0x5a,sizeof vari_tab); vari_need_load=1; ticks(1);
  CHECK(vari_tab.e[5].pos==-30 && vari_tab.e[3].pos==0 && vari_tab.e[40].pos==0 && writes==w, "power cycle: speeds restored, no write");
  /* card used on a stock unit that exchanged pads 5 and 6: speed follows */
  memcpy(tmp,padinfo+5*32,32); memcpy(padinfo+5*32,padinfo+6*32,32); memcpy(padinfo+6*32,tmp,32);
  vari_need_load=1; ticks(1); CHECK(vari_tab.e[6].pos==-30 && vari_tab.e[5].pos==0, "reload after stock unit moved samples: speed follows");
  /* corrupt file */
  card[0]^=0xff; vari_need_load=1; ticks(1);
  CHECK(vari_tab.e[6].pos==0 && vari_tab.magic==0x56535031, "corrupt file: start fresh");
  /* short file */
  card_len=100; vari_need_load=1; ticks(1); CHECK(vari_tab.e[6].pos==0, "short file: start fresh");
  CHECK(locks==0, "lock/unlock balanced");
  /* any stored byte is a safe ratio index */
  for(int v=-128;v<128;v++){ vari_tab.e[0].pos=(s8)v; u32 r=pad_ratio(0); if(r<0x8000||r>0x18000){fails++;printf("bad ratio %d\n",v);} }
  printf(fails?"%d FAILURES\n":"all ok\n",fails); return fails;
}
