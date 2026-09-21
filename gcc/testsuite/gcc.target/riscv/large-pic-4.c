/* { dg-do preprocess } */
/* { dg-require-effective-target fpic } */
/* { dg-options "-march=rv64gc -mabi=lp64d -mcmodel=large -fPIC" } */

#ifndef __riscv_cmodel_large
#error __riscv_cmodel_large not defined
#endif
