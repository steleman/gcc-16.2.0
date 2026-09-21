/* Initial-exec TLS uses the MOVZ/MOVK GOTTPREL relocations indexing the GOT
   base; local-exec is unchanged.  */
/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-require-effective-target fpic } */
/* { dg-require-effective-target tls_native } */
/* { dg-options "-O2 -mcmodel=large -fPIC" } */

extern __thread int ie_var __attribute__ ((tls_model ("initial-exec")));
__thread int le_var __attribute__ ((tls_model ("local-exec"))) = 1;

int
f (void)
{
  return ie_var + le_var;
}

/* { dg-final { scan-assembler "movz\tx\[0-9\]+, #:gottprel_g1:ie_var\n" } } */
/* { dg-final { scan-assembler "movk\tx\[0-9\]+, #:gottprel_g0_nc:ie_var\n" } } */
/* { dg-final { scan-assembler "#:prel_g3:_GLOBAL_OFFSET_TABLE_\\+4" } } */
/* { dg-final { scan-assembler ":tprel_(hi12|g2):le_var" } } */
/* { dg-final { scan-assembler-not ":gottprel:" } } */
/* { dg-final { scan-assembler-not "adrp" } } */
