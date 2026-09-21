/* Jump tables are addressed PC-relatively and switch dispatch is
   unchanged.  */
/* { dg-do compile } */
/* { dg-require-effective-target lp64 } */
/* { dg-require-effective-target fpic } */
/* { dg-options "-O2 -mcmodel=large -fPIC -fno-section-anchors" } */

extern void g0 (void), g1 (void), g2 (void), g3 (void), g4 (void), g5 (void);
extern void g6 (void), g7 (void), g8 (void), g9 (void), g10 (void);

void
f (int i)
{
  switch (i)
    {
    case 0: g0 (); break;
    case 1: g1 (); break;
    case 2: g2 (); break;
    case 3: g3 (); break;
    case 4: g4 (); break;
    case 5: g5 (); break;
    case 6: g6 (); break;
    case 7: g7 (); break;
    case 8: g8 (); break;
    case 9: g9 (); break;
    case 10: g10 (); break;
    }
}

/* { dg-final { scan-assembler "#:prel_g3:\\.L\[0-9\]+\\+4" } } */
/* { dg-final { scan-assembler-not "_GLOBAL_OFFSET_TABLE_" } } */
/* { dg-final { scan-assembler-not "adrp" } } */
