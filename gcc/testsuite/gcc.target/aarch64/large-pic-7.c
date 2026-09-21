/* { dg-do preprocess } */
/* { dg-require-effective-target lp64 } */
/* { dg-require-effective-target fpic } */
/* { dg-options "-mcmodel=large -fPIC" } */

#ifndef __AARCH64_CMODEL_LARGE__
#error __AARCH64_CMODEL_LARGE__ not defined
#endif
