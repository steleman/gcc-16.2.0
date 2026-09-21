/* Same as large-pic-1.c and large-pic-2.c, without optimization.  */
/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-require-effective-target fpic } */
/* { dg-options "-O0 -mcmodel=large -fPIC -fno-section-anchors" } */

extern int ext_var;
static int local_var;

int
f (int i)
{
  static void *labels[] = { 0 };
  void *tbl[] = { &&l0, &&l1 };
  labels[0] = tbl[i];
  goto *tbl[i];
 l0:
  return ext_var;
 l1:
  return local_var;
}

/* { dg-final { scan-assembler "#:prel_g3:_GLOBAL_OFFSET_TABLE_\\+4" } } */
/* { dg-final { scan-assembler "#:gotoff_g3:ext_var" } } */
/* { dg-final { scan-assembler "#:prel_g3:local_var\\+4" } } */
/* { dg-final { scan-assembler "#:prel_g3:\\.L\[0-9\]+\\+4" } } */
/* { dg-final { scan-assembler-not "adrp" } } */
