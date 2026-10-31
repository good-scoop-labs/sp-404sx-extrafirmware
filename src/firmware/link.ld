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
}
SECTIONS {
  .entry : { *(.entry) *(.text.cavestubs) *(.text.cave) *(.rodata.cave) . = ALIGN(4); *(.cavedata) . = ALIGN(4); } > cave
  .vbss (NOLOAD) : { *(.vbss) . = ALIGN(4); } > cave
  .text : { *(.text.fp) *(.text) *(.text.unlikely*) *(.text.startup*) *(.text.hot*) *(.rodata) *(.rodata.str*) . = ALIGN(4); } > dump
  .persist : { *(.text.persist) . = ALIGN(4); } > os
  .wsp : { *(.rodata.wsp) . = ALIGN(4); } > wsp
  .helper : { *(.text.helper) . = ALIGN(4); } > helper
  .midi : { *(.text.midi) . = ALIGN(4); } > midi
  .led : { *(.text.led) . = ALIGN(4); } > led
  .dev : { *(.text.dev) . = ALIGN(4); } > dev
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.data*) *(.bss*) }
}
