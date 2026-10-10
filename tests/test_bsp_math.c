/* Real fixed-point/BSP code, linked with section GC; no ROM or renderer needed. */
#include "doomdef.h"
#include "r_local.h"
#include <assert.h>
#include <stdio.h>

fixed_t viewx, viewy, viewz, viewcos, viewsin;
int R_PCFovInvScale;
byte solidcols[320];
fixed_t *finecosine = &finesine[FINEANGLES / 4];
boolean R_CheckBBox(fixed_t box[4]);
fixed_t __real_FixedDiv2(fixed_t a, fixed_t b);
fixed_t __wrap_FixedDiv2(fixed_t a, fixed_t b)
{
    if (!b) {
        fprintf(stderr, "BSP divided by zero: view=%d,%d sin/cos=%d,%d scale=%d a=%d\n",
                viewx, viewy, viewsin, viewcos, R_PCFovInvScale, a);
        assert(b != 0);
    }
    return __real_FixedDiv2(a, b);
}
static uint32_t rng = 64;
static int random_coord(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (int)(rng % 1025) - 512;
}
int main(void)
{
    int i, a, n;
    fixed_t box[4];
    assert(__real_FixedDiv2(FRACUNIT, 2 * FRACUNIT) == FRACUNIT / 2);
    assert(__real_FixedDiv2(-FRACUNIT, 2 * FRACUNIT) == -FRACUNIT / 2);
    assert(__real_FixedDiv2(1, 0) == MAXINT);
    assert(__real_FixedDiv2(-1, 0) == MININT);
    assert(__real_FixedDiv2(0, 0) == MAXINT);
    assert(FixedDiv(MININT, MININT) == FRACUNIT);
    assert(FixedDiv(FRACUNIT, MININT) == -2);
    /* Edge-aligned camera views, including narrow lateral culling scales. */
    box[BOXLEFT] = -128 * FRACUNIT; box[BOXRIGHT] = 128 * FRACUNIT;
    box[BOXBOTTOM] = -128 * FRACUNIT; box[BOXTOP] = 128 * FRACUNIT;
    for (n = 0; n < 3; n++) {
        R_PCFovInvScale = n == 0 ? FRACUNIT : n == 1 ? 49152 : 6144;
        for (a = 0; a < FINEANGLES; a++) {
            viewx = -256 * FRACUNIT; viewy = 128 * FRACUNIT;
            viewsin = finesine[a]; viewcos = finecosine[a];
            R_CheckBBox(box);
        }
    }
    for (i = -3; i <= 3; i++) {
        for (n = 0; n < 3; n++) {
            R_PCFovInvScale = n == 0 ? FRACUNIT : n == 1 ? 49152 : 6144;
            for (a = 0; a < FINEANGLES; a++) {
                viewx = -128 * FRACUNIT + i;
                viewy = 128 * FRACUNIT + 1;
                viewsin = finesine[a]; viewcos = finecosine[a];
                R_CheckBBox(box);
            }
        }
    }
    /* Realistic map-sized boxes, edge/corner views, wide and pitched FOVs. */
    for (i = 0; i < 200000; i++) {
        int x = random_coord(), y = random_coord();
        box[BOXLEFT] = x * FRACUNIT; box[BOXRIGHT] = (x + 128) * FRACUNIT;
        box[BOXBOTTOM] = y * FRACUNIT; box[BOXTOP] = (y + 128) * FRACUNIT;
        viewx = random_coord() * FRACUNIT; viewy = random_coord() * FRACUNIT;
        a = (rng >> 8) & FINEMASK;
        viewsin = finesine[a]; viewcos = finecosine[a];
        R_PCFovInvScale = i % 3 == 0 ? FRACUNIT : i % 3 == 1 ? 49152 : 6144;
        memset(solidcols, i & 1, sizeof(solidcols));
        R_CheckBBox(box);
    }
    puts("PASS: fixed-point boundaries and 396608 BSP projections");
    return 0;
}
