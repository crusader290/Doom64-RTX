/*
 * gu.c - matrix helpers equivalent to the libultra "gu" functions the game
 * calls. Doom64-RTX PC port, GPLv3.
 */
#include <ultra64.h>
#include <string.h>

void guMtxF2L(MtxF mf, Mtx *m)
{
    int i, j;
    int32_t *w = (int32_t *)m->m;
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j += 2)
        {
            int32_t e1 = (int32_t)(mf[i][j] * 65536.0f);
            int32_t e2 = (int32_t)(mf[i][j + 1] * 65536.0f);
            w[(i * 4 + j) / 2] = (int32_t)(((uint32_t)e1 & 0xffff0000u) | (((uint32_t)e2 >> 16) & 0xffffu));
            w[8 + (i * 4 + j) / 2] = (int32_t)((((uint32_t)e1 << 16) & 0xffff0000u) | ((uint32_t)e2 & 0xffffu));
        }
    }
}

void guMtxL2F(MtxF mf, Mtx *m)
{
    int i, j;
    const int32_t *w = (const int32_t *)m->m;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
        {
            int idx = (i * 4 + j) / 2;
            uint32_t wi = (uint32_t)w[idx], wf = (uint32_t)w[8 + idx];
            int16_t ip = (j & 1) ? (int16_t)(wi & 0xffff) : (int16_t)(wi >> 16);
            uint16_t fp = (j & 1) ? (uint16_t)(wf & 0xffff) : (uint16_t)(wf >> 16);
            mf[i][j] = (float)ip + (float)fp / 65536.0f;
        }
}

void guFrustumF(MtxF mf, float l, float r, float b, float t, float n, float f, float scale)
{
    int i, j;
    memset(mf, 0, sizeof(MtxF));
    mf[0][0] = 2.0f * n / (r - l);
    mf[1][1] = 2.0f * n / (t - b);
    mf[2][0] = (r + l) / (r - l);
    mf[2][1] = (t + b) / (t - b);
    mf[2][2] = -(f + n) / (f - n);
    mf[2][3] = -1.0f;
    mf[3][2] = -2.0f * f * n / (f - n);
    mf[3][3] = 0.0f;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            mf[i][j] *= scale;
}

void guFrustum(Mtx *m, float l, float r, float b, float t, float n, float f, float scale)
{
    MtxF mf;
    guFrustumF(mf, l, r, b, t, n, f, scale);
    guMtxF2L(mf, m);
}
