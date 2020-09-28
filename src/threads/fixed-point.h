#ifndef THREADS_FIXEDPOINT_H
#define THREADS_FIXEDPOINT_H

/* 17.14 fixed point representation */

#define FIXED_POINT_F (1 << 14)   /* 2^14 */

/* Fixed‐point arithmetic functions; x and y are fixed‐point
   numbers, n is an integer */

int i2f (int n);                  /* Convert integer to fixed‐point */
int f2i_round_zero (int x);
int f2i_round_nearest (int x);
int add_ff (int x, int y);        /* x + y */
int sub_ff (int x, int y);        /* x ‐ y */
int add_fi (int x, int n);        /* x + n */
int sub_fi (int x, int n);        /* x ‐ n */
int mul_ff (int x, int y);        /* x * y */
int mul_fi (int x, int n);        /* x * n */
int div_ff (int x, int y);        /* x / y */
int div_fi (int x, int n);        /* x / n */

#endif /* threads/fixed-point.h */
