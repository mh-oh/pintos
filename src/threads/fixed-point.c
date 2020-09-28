#include "threads/fixed-point.h"
#include <stdint.h>

/* Convert integer to fixed‐point */
int
i2f (int n)
{
  return n * FIXED_POINT_F;
}

/* Convert x to integer (rounding toward zero). */
int
f2i_round_zero (int x)
{
  return x / FIXED_POINT_F;
}

/* Convert x to integer (rounding to nearest). */
int
f2i_round_nearest (int x)
{
  return (x >= 0)
          ? (x + FIXED_POINT_F / 2) / FIXED_POINT_F
          : (x - FIXED_POINT_F / 2) / FIXED_POINT_F;
}

/* x + y */
int
add_ff (int x, int y)
{
  return x + y;
}

/* x ‐ y */
int
sub_ff (int x, int y)
{
  return x - y;
}

/* x + n */
int
add_fi (int x, int n)
{
  return x + n * FIXED_POINT_F;
}

/* x ‐ n */
int
sub_fi (int x, int n)
{
  return x - n * FIXED_POINT_F;
}

/* x * y */
int
mul_ff (int x, int y)
{
  return ((int64_t)x) * y / FIXED_POINT_F;
}

/* x * n */
int
mul_fi (int x, int n)
{
  return x * n;
}

/* x / y */
int
div_ff (int x, int y)
{
  return ((int64_t)x) * FIXED_POINT_F / y;
}

/* x / n */
int
div_fi (int x, int n)
{
  return x / n;
}
