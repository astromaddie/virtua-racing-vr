/* VRDE's optional interpreter observer. Full tracing remains the default. */
#ifndef VRDE_TRACE_H
#define VRDE_TRACE_H

enum { VRDE_SH2_TRACE_ALL, VRDE_SH2_TRACE_HUD };
extern int vrde_sh2_access_filter;
extern void (*vrde_sh2_access_hook)(int is_slave, unsigned int pc, unsigned int address,
  unsigned int value, int size, int is_write);

static inline unsigned int vrde_sh2_report(int slave, unsigned int pc, unsigned int address,
  unsigned int value, int size, int write)
{
  /* hud_note_sh2_access consumes only master HUD-list reads and framebuffer
     writes. Reject the other accesses before an indirect call on every SH2
     instruction fetch/load/store. Preserve the original address and value. */
  unsigned int a = address & ~0x20000000u;
  if (vrde_sh2_access_hook &&
      (vrde_sh2_access_filter != VRDE_SH2_TRACE_HUD ||
       (!slave && (write ? a >= 0x04000000u && a < 0x04040000u
                         : a >= 0x0600c800u && a < 0x0600ca00u))))
    vrde_sh2_access_hook(slave, pc, address, value, size, write);
  return value;
}
#endif
