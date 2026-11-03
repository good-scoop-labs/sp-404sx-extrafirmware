OUTPUT_FORMAT("elf32-shbig-linux")
ENTRY(_start)
MEMORY {
  cave (rwx) : ORIGIN = 0x0C080400, LENGTH = 1024   /* free: above vector 255                     */
  dump (rx)  : ORIGIN = 0x0C089EF4, LENGTH = 0x46C  /* reclaimed hidden PAD_INFO debug dump        */
  os   (rx)  : ORIGIN = 0x0C0EB5A0, LENGTH = 0x1B8  /* reclaimed unreferenced OS service routine   */
  wsp  (r)   : ORIGIN = 0x0C0A9558, LENGTH = 0xAC   /* reclaimed unreferenced effects method       */
  helper (rx): ORIGIN = 0x0C089EA4, LENGTH = 0x50   /* rest of the dead filename helper            */
  midi (rx)  : ORIGIN = 0x0C0CDDDC, LENGTH = 0xE0   /* reclaimed factory test page (CPageTestMidi) method */
  led  (rx)  : ORIGIN = 0x0C0CD838, LENGTH = 0xC4   /* reclaimed factory test page (CPageTestLED) method */
  dev  (rx)  : ORIGIN = 0x0C0CF620, LENGTH = 0x194  /* reclaimed factory test page (CPageTestDevice) method */
  /* chromatic mode: more reclaimed factory test page methods (see EXTRA in src/tools/build.py) */
  chra (rx)  : ORIGIN = 0x0C0CEEB0, LENGTH = 0x94   /* CPageTestVer method 5       */
  chrb (rx)  : ORIGIN = 0x0C0CD4D0, LENGTH = 0x8C   /* CPageTestExtract method 2   */
  chrc (rx)  : ORIGIN = 0x0C0CD238, LENGTH = 0x8C   /* CPageTestBattery method 5   */
  chrd (rx)  : ORIGIN = 0x0C0CE134, LENGTH = 0x74   /* CPageTestMute method 2      */
  chre (rx)  : ORIGIN = 0x0C0CD7BC, LENGTH = 0x6C   /* CPageTestLED method 2       */
  chrf (rx)  : ORIGIN = 0x0C0CD1BC, LENGTH = 0x6C   /* CPageTestBattery method 2   */
  chrg (rw)  : ORIGIN = 0x0C0CE444, LENGTH = 0x64   /* CPageTestPhones method 2: data only */
  chrh (rx)  : ORIGIN = 0x0C0CDC88, LENGTH = 0x64   /* CPageTestMicInt method 2    */
  chri (rx)  : ORIGIN = 0x0C0CDB20, LENGTH = 0x64   /* CPageTestMicExt method 2    */
  chrj (rx)  : ORIGIN = 0x0C0CD9D0, LENGTH = 0x64   /* CPageTestLine method 2      */
  chrk (rw)  : ORIGIN = 0x0C0CD398, LENGTH = 0x64   /* CPageTestCompleted method 2: data only */
  chrl (rx)  : ORIGIN = 0x0C0CEC38, LENGTH = 0x90   /* CPageTestVer method 0       */
  chrm (rx)  : ORIGIN = 0x0C0CF49C, LENGTH = 0x80   /* CPageTestDevice method 0    */
  chrn (rx)  : ORIGIN = 0x0C0CE1A8, LENGTH = 0x6C   /* CPageTestMute method 3      */
  chrp (rx)  : ORIGIN = 0x0C0CE720, LENGTH = 0x48   /* CPageTestSw method 0        */
  chrq (rx)  : ORIGIN = 0x0C0CD358, LENGTH = 0x3C   /* CPageTestCompleted method 0 */
  chrs (rx)  : ORIGIN = 0x0C0CD474, LENGTH = 0x48   /* CPageTestExtract method 0   */
  chru (rx)  : ORIGIN = 0x0C0CDC20, LENGTH = 0x50   /* CPageTestMicInt method 0    */
  chrv (rx)  : ORIGIN = 0x0C0CD974, LENGTH = 0x3C   /* CPageTestLine method 0      */
  chrx (rx)  : ORIGIN = 0x0C0CD56C, LENGTH = 0x1D0  /* CPageTestExtract method 5   */
  chry (rx)  : ORIGIN = 0x0C0CDEE4, LENGTH = 0x18C  /* CPageTestMidi method 5      */
  chrz (rx)  : ORIGIN = 0x0C0CE280, LENGTH = 0x120  /* CPageTestMute method 5      */
  chrw (rx)  : ORIGIN = 0x0C0CE860, LENGTH = 0x3D4  /* helpers after the CPageTestSw constructor */
}
SECTIONS {
  .entry : { *(.entry) *(.text.cavestubs) *(.text.cave) *(.text.chrok) *(.rodata.cave) . = ALIGN(4); *(.cavedata) . = ALIGN(4); } > cave
  .vbss (NOLOAD) : { *(.vbss) . = ALIGN(4); } > cave
  .text : { *(.text.fp) *(.text) *(.text.unlikely*) *(.text.startup*) *(.text.hot*) *(.rodata) *(.rodata.str*) . = ALIGN(4); } > dump
  .persist : { *(.text.persist) . = ALIGN(4); } > os
  .wsp : { *(.rodata.wsp) . = ALIGN(4); } > wsp
  .helper : { *(.text.helper) . = ALIGN(4); } > helper
  .midi : { *(.text.midi) . = ALIGN(4); } > midi
  .led : { *(.text.led) . = ALIGN(4); } > led
  .dev : { *(.text.dev) . = ALIGN(4); } > dev
  .chra : { *(.text.chrmenuev) . = ALIGN(4); } > chra
  .chrb : { *(.text.chrratio) . = ALIGN(4); } > chrb
  .chrc : { *(.text.chrtick) . = ALIGN(4); } > chrc
  .chrd : { *(.text.chrkeyof) *(.text.chrstop) . = ALIGN(4); } > chrd
  .chre : { *(.text.chrpagerefres) . = ALIGN(4); } > chre
  .chrf : { *(.text.chrpageevent) . = ALIGN(4); } > chrf
  .chrh : { *(.text.chrstubs) . = ALIGN(4); } > chrh
  .chri : { *(.text.chrrequest) . = ALIGN(4); } > chri
  .chrj : { *(.text.chrused) . = ALIGN(4); } > chrj
  .chrl : { *(.text.chrkeydown) *(.text.chrpage) . = ALIGN(4); } > chrl
  .chrm : { *(.text.vshow) . = ALIGN(4); } > chrm
  .chrn : { *(.text.chrsave) . = ALIGN(4); } > chrn
  .chrp : { *(.text.chrmenu) . = ALIGN(4); } > chrp
  .chrq : { *(.text.chrsrcbank) . = ALIGN(4); } > chrq
  .chrs : { *(.text.chrrelease) . = ALIGN(4); } > chrs
  .chru : { *(.text.chrfree) . = ALIGN(4); } > chru
  .chrv : { *(.text.chrpageenter) . = ALIGN(4); } > chrv
  .chrx : { *(.text.chropen) *(.text.chrload) . = ALIGN(4); } > chrx
  .chry : { *(.text.chrnote) *(.text.chrcheck) . = ALIGN(4); } > chry
  .chrz : { *(.text.chrknob) . = ALIGN(4); } > chrz
  .chrw : { *(.text.chrxop) *(.text.chrxset) *(.text.chrgate) *(.text.chrshow) *(.text.chrstubs2) *(.text.chrpadkey) *(.text.chrplay) *(.text.chrbankof) . = ALIGN(4); } > chrw
  .chrg : { *(.rodata.chr) . = ALIGN(4); } > chrg
  .chrk : { *(.chrdata) . = ALIGN(4); } > chrk
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.data*) *(.bss*) }
}
