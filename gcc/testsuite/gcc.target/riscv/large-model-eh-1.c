/* The large code model makes no assumptions about the distance between code
   and data, so FDE and LSDA pointers use 8-byte PC-relative encodings.  */
/* { dg-do compile } */
/* { dg-options "-march=rv64gc -mabi=lp64d -mcmodel=large -fno-pie -fasynchronous-unwind-tables -fexceptions" } */
/* { dg-skip-if "" { *-*-* } { "-flto" } } */

extern void g (void);

void
f (void)
{
  g ();
}

/* { dg-final { scan-assembler "\\.8byte\t\\.LFB\[0-9\]+-\\." } } */
/* { dg-final { scan-assembler-not "\\.4byte\t\\.LFB\[0-9\]+-\\." } } */
/* { dg-final { scan-assembler-not "\\.cfi_startproc" } } */
