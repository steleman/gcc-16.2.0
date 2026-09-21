/* The GOT base is computed once per function.  */
/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-require-effective-target fpic } */
/* { dg-options "-O2 -mcmodel=large -fPIC" } */

extern int a, b, c;
extern void g (void);

int
f (void)
{
  g ();
  return a + b + c;
}

/* { dg-final { scan-assembler-times "#:prel_g3:_GLOBAL_OFFSET_TABLE_\\+4" 1 } } */
/* { dg-final { scan-assembler-times "#:gotoff_g3:" 3 } } */
/* { dg-final { scan-assembler "bl\tg\n" } } */
