
/* m_fixed.c -- fixed point implementation */

#include "i_main.h"
#include "doomdef.h"
#include "p_spec.h"
#include "r_local.h"

/*
===============
=
= FixedDiv
=
===============
*/

fixed_t FixedDiv(fixed_t a, fixed_t b) // 80002BF8
{
#ifdef D64_PC
    /* [PC] Magnitudes must also handle INT_MIN without signed negation. */
    uint32_t aa = a < 0 ? (uint32_t)(-(int64_t)a) : (uint32_t)a;
    uint32_t bb = b < 0 ? (uint32_t)(-(int64_t)b) : (uint32_t)b;
    if ((aa >> 14) >= bb)
        return (a ^ b) < 0 ? MININT : MAXINT;
    return FixedDiv2(a, b);
#else
    fixed_t     aa, bb;
    unsigned    c;
    int         sign;

    sign = a^b;

    if (a < 0)
        aa = -a;
    else
        aa = a;

    if (b < 0)
        bb = -b;
    else
        bb = b;

    if ((unsigned)(aa >> 14) >= bb)
    {
        if (sign < 0)
            c = MININT;
        else
            c = MAXINT;
    }
    else
        c = (fixed_t) FixedDiv2(a, b);

    return c;
#endif
}

/*
===============
=
= FixedMul
=
===============
*/

fixed_t FixedMul(fixed_t a, fixed_t b) // 800044D0
{
    s64 result = ((s64) a * (s64) b) >> 16;

    return (fixed_t) result;
}

#if 0
s64 FixedMul2(s64 a, s64 b) // 800044D0
{
    register s64 flo;

    //asm(".set noreorder");
    asm("dmult  $4, $5");
    asm("mflo   $3");
    asm("dsra   $3, 16");
    asm("move   %0, $3":"=r" (flo):);

    return (fixed_t) flo;

    /*
    dmult   $4, $5
    mflo    $2
    dsra    $2, $2, 16
    jr      $31
    nop
    */
}
#endif // 0

/*
===============
=
= FixedDiv2
=
===============
*/

fixed_t FixedDiv2(fixed_t a, fixed_t b) // 800044E4
{
#ifdef D64_PC
    /* [PC] BSP clipping can reach zero depth. Avoid host IDIV faults and
     * undefined left shifts of negative numerators; keep ordinary quotients. */
    if (!b)
        return a < 0 ? MININT : MAXINT;
    s64 result = ((s64)a * FRACUNIT) / (s64)b;
#else
    s64 result = ((s64) a << 16) / (s64)b;
#endif

    return (fixed_t) result;
}
