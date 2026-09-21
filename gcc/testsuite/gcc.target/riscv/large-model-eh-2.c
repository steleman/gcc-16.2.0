/* As large-model-eh-1.c, with position-independent code.  */
/* { dg-do compile } */
/* { dg-require-effective-target fpic } */
/* { dg-options "-march=rv64gc -mabi=lp64d -mcmodel=large -fPIC -fasynchronous-unwind-tables -fexceptions" } */
/* { dg-skip-if "" { *-*-* } { "-flto" } } */

extern void g (void);

void
f (void)
{
  g ();
}

/* { dg-final { scan-assembler "\\.8byte\t\\.LFB\[0-9\]+-\\." } } */
/* { dg-final { scan-assembler-not "\\.4byte\t\\.LFB\[0-9\]+-\\." } } */
