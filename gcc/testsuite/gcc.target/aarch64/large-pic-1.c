/* Preemptible symbols are loaded from the GOT, indexed by a 64-bit GOT
   offset, and the GOT base is computed with a 64-bit PC-relative
   displacement.  */
/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-require-effective-target fpic } */
/* { dg-options "-O2 -mcmodel=large -fPIC" } */

extern int ext_var;

int
f (void)
{
  return ext_var;
}

/* { dg-final { scan-assembler "adr\tx\[0-9\]+, \\.\n" } } */
/* { dg-final { scan-assembler "movz\tx\[0-9\]+, #:prel_g3:_GLOBAL_OFFSET_TABLE_\\+4\n" } } */
/* { dg-final { scan-assembler "movk\tx\[0-9\]+, #:prel_g2_nc:_GLOBAL_OFFSET_TABLE_\\+8\n" } } */
/* { dg-final { scan-assembler "movk\tx\[0-9\]+, #:prel_g1_nc:_GLOBAL_OFFSET_TABLE_\\+12\n" } } */
/* { dg-final { scan-assembler "movk\tx\[0-9\]+, #:prel_g0_nc:_GLOBAL_OFFSET_TABLE_\\+16\n" } } */
/* { dg-final { scan-assembler "movz\tx\[0-9\]+, #:gotoff_g3:ext_var\n" } } */
/* { dg-final { scan-assembler "movk\tx\[0-9\]+, #:gotoff_g2_nc:ext_var\n" } } */
/* { dg-final { scan-assembler "movk\tx\[0-9\]+, #:gotoff_g1_nc:ext_var\n" } } */
/* { dg-final { scan-assembler "movk\tx\[0-9\]+, #:gotoff_g0_nc:ext_var\n" } } */
/* { dg-final { scan-assembler-not "adrp" } } */
/* { dg-final { scan-assembler-not ":got:" } } */
