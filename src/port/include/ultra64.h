/*
 * ultra64.h - clean-room PC replacement for the parts of the Nintendo 64 SDK
 * ("libultra") that the DOOM64-RE sources use.
 *
 * Doom64-RTX PC port. GPLv3 (same as DOOM64-RE).
 *
 * This is NOT Nintendo's header. Only the names the game uses are provided.
 * The display list encoding follows the F3DEX command layout where that is
 * convenient, but w1 is pointer sized (uintptr_t) so addresses survive on
 * 64-bit hosts. The only consumer of the encoding is src/gfx/gbi.c.
 */
#ifndef D64PC_ULTRA64_H
#define D64PC_ULTRA64_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Basic types                                                              */
/* ------------------------------------------------------------------------ */
typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef uint64_t u64;
typedef int64_t  s64;
typedef volatile u8  vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;
typedef volatile s8  vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;
typedef float  f32;
typedef double f64;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* ------------------------------------------------------------------------ */
/* OS: threads, messages (single threaded emulation, see src/port/os.c)     */
/* ------------------------------------------------------------------------ */
typedef void *OSMesg;
typedef s32 OSPri;
typedef s32 OSId;
typedef u64 OSTime;

typedef struct OSThread_s { int id; void (*entry)(void *); void *arg; } OSThread;

typedef struct OSMesgQueue_s {
    s32     validCount;
    s32     first;
    s32     msgCount;
    OSMesg *msg;
} OSMesgQueue;

typedef struct { u16 type; u8 pri; u8 status; OSMesgQueue *retQueue; } OSMesgHdr;
typedef struct { OSMesgHdr hdr; void *dramAddr; u32 devAddr; u32 size; } OSIoMesg;

#define OS_MESG_NOBLOCK 0
#define OS_MESG_BLOCK   1
#define OS_MESG_PRI_NORMAL 0
#define OS_MESG_PRI_HIGH   1
#define OS_READ  0
#define OS_WRITE 1

#define OS_PRIORITY_IDLE  0
#define OS_PRIORITY_PIMGR 150
#define OS_PRIORITY_VIMGR 254

#define OS_EVENT_SI 5
#define OS_EVENT_SP 4
#define OS_EVENT_DP 9
#define OS_EVENT_PRENMI 14

#define OS_K0_TO_PHYSICAL(x) ((uintptr_t)(x))
#define OS_PHYSICAL_TO_K0(x) ((void *)(uintptr_t)(x))

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count);
s32  osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flag);
s32  osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flag);
s32  osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flag);
void osYieldThread(void);
void osInvalDCache(void *p, s32 len);
void osWritebackDCacheAll(void);
uintptr_t osVirtualToPhysical(void *p);

/* "Cartridge" DMA: reads from the virtual ROM built by src/port/rom.c */
s32 osPiStartDma(OSIoMesg *mb, s32 pri, s32 direction, u32 devAddr,
                 void *dramAddr, u32 size, OSMesgQueue *mq);

/* RSP task structure (only kept so audio/gfx code that fills it compiles) */
typedef struct {
    u32 type, flags;
    u64 *ucode_boot; u32 ucode_boot_size;
    u64 *ucode;      u32 ucode_size;
    u64 *ucode_data; u32 ucode_data_size;
    u64 *dram_stack; u32 dram_stack_size;
    u64 *output_buff; u64 *output_buff_size;
    u64 *data_ptr;   u32 data_size;
    u64 *yield_data_ptr; u32 yield_data_size;
} OSTask_t;
typedef union { OSTask_t t; long long force_structure_alignment; } OSTask;
#define M_GFXTASK 1
#define M_AUDTASK 2

/* Microcode identities (never executed on PC; see src/port/os.c) */
extern u64 rspbootTextStart[], rspbootTextEnd[];
extern u64 gspF3DEX_NoN_fifoTextStart[], gspF3DEX_NoN_fifoDataStart[];
extern u64 gspL3DEX_fifoTextStart[], gspL3DEX_fifoDataStart[];
extern u64 aspMainTextStart[], aspMainDataStart[];
#define SP_UCODE_SIZE 4096
#define SP_UCODE_DATA_SIZE 2048
#define SP_DRAM_STACK_SIZE8 1024
#define SP_DRAM_STACK_SIZE64 128
#define OS_YIELD_DATA_SIZE 0xc00

/* Controllers */
#define MAXCONTROLLERS 4
typedef struct { u16 type; u8 status; u8 errno_; } OSContStatus;
typedef struct { u16 button; s8 stick_x; s8 stick_y; u8 errno_; } OSContPad;

#define CONT_A      0x8000
#define CONT_B      0x4000
#define CONT_G      0x2000
#define CONT_START  0x1000
#define CONT_UP     0x0800
#define CONT_DOWN   0x0400
#define CONT_LEFT   0x0200
#define CONT_RIGHT  0x0100
#define CONT_L      0x0020
#define CONT_R      0x0010
#define CONT_E      0x0008
#define CONT_D      0x0004
#define CONT_C      0x0002
#define CONT_F      0x0001
#define A_BUTTON    CONT_A
#define B_BUTTON    CONT_B
#define L_TRIG      CONT_L
#define R_TRIG      CONT_R
#define Z_TRIG      CONT_G
#define START_BUTTON CONT_START
#define U_JPAD      CONT_UP
#define L_JPAD      CONT_LEFT
#define R_JPAD      CONT_RIGHT
#define D_JPAD      CONT_DOWN
#define U_CBUTTONS  CONT_E
#define L_CBUTTONS  CONT_C
#define R_CBUTTONS  CONT_F
#define D_CBUTTONS  CONT_D

/* Controller pak (file system) - emulated by src/port/pak.c */
typedef struct { int status; int channel; } OSPfs;
typedef struct {
    u32 file_size;
    u32 game_code;
    u16 company_code;
    char ext_name[4];
    char game_name[16];
} OSPfsState;
#define PFS_READ  0
#define PFS_WRITE 1
#define PFS_ERR_NOPACK       1
#define PFS_ERR_NEW_PACK     2
#define PFS_ERR_INCONSISTENT 3
#define PFS_ERR_CONTRFAIL    4
#define PFS_ERR_INVALID      5
#define PFS_ERR_BAD_DATA     6
#define PFS_DATA_FULL        7
#define PFS_DIR_FULL         8
#define PFS_ERR_EXIST        9
#define PFS_ERR_ID_FATAL     10
#define PFS_ERR_DEVICE       11

/* Video interface (unused on PC beyond these few names) */
typedef struct { u32 hStart; } OSViCommonRegs;
typedef struct { u32 vStart; } OSViFieldRegs;
typedef struct { u8 type; OSViCommonRegs comRegs; OSViFieldRegs fldRegs[2]; } OSViMode;
extern OSViMode osViModeTable[];
extern s32 osTvType;
#define OS_TV_PAL  0
#define OS_TV_NTSC 1
#define OS_TV_MPAL 2
void *osViGetNextFramebuffer(void);
void *osViGetCurrentFramebuffer(void);

/* ------------------------------------------------------------------------ */
/* Graphics data types                                                      */
/* ------------------------------------------------------------------------ */
typedef struct {
    short ob[3];
    unsigned short flag;
    short tc[2];
    unsigned char cn[4];
} Vtx_t;

typedef struct {
    short ob[3];
    unsigned short flag;
    short tc[2];
    signed char n[3];
    unsigned char a;
} Vtx_tn;

typedef union {
    Vtx_t v;
    Vtx_tn n;
    long long force_structure_alignment;
} Vtx;

/* N64 fixed point matrix: words 0..7 hold the s16 integer halves
 * (packed big-endian pairs), words 8..15 the u16 fractional halves. */
typedef union {
    s32 m[4][4];
    long long force_structure_alignment[8];
} Mtx;

typedef struct { short vscale[4]; short vtrans[4]; } Vp_t;
typedef union { Vp_t vp; long long force_structure_alignment[2]; } Vp;

typedef struct {
    u32 w0;
    u32 pad_;
    uintptr_t w1;
} Gwords;

typedef union {
    Gwords words;
    long long force_structure_alignment[2];
} Gfx;

typedef float MtxF[4][4];

void guFrustum(Mtx *m, float l, float r, float b, float t, float n, float f, float scale);
void guFrustumF(MtxF mf, float l, float r, float b, float t, float n, float f, float scale);
void guMtxF2L(MtxF mf, Mtx *m);
void guMtxL2F(MtxF mf, Mtx *m);

/* ------------------------------------------------------------------------ */
/* GBI constants                                                            */
/* ------------------------------------------------------------------------ */
#define _SHIFTL(v, s, w) ((u32)(((u32)(v) & ((1u << (w)) - 1u)) << (s)))
#define _SHIFTR(v, s, w) ((u32)(((u32)(v) >> (s)) & ((1u << (w)) - 1u)))

/* RSP commands (F3DEX numbering) */
#define G_SPNOOP            0x00
#define G_MTX               0x01
#define G_MOVEMEM           0x03
#define G_VTX               0x04
#define G_DL                0x06
#define G_TRI2              0xB1
#define G_MODIFYVTX         0xB2
#define G_RDPHALF_2         0xB3
#define G_RDPHALF_1         0xB4
#define G_LINE3D            0xB5
#define G_CLEARGEOMETRYMODE 0xB6
#define G_SETGEOMETRYMODE   0xB7
#define G_ENDDL             0xB8
#define G_SETOTHERMODE_L    0xB9
#define G_SETOTHERMODE_H    0xBA
#define G_TEXTURE           0xBB
#define G_MOVEWORD          0xBC
#define G_POPMTX            0xBD
#define G_CULLDL            0xBE
#define G_TRI1              0xBF
/* RDP commands */
#define G_NOOP              0xC0
#define G_TEXRECT           0xE4
#define G_TEXRECTFLIP       0xE5
#define G_RDPLOADSYNC       0xE6
#define G_RDPPIPESYNC       0xE7
#define G_RDPTILESYNC       0xE8
#define G_RDPFULLSYNC       0xE9
#define G_SETKEYGB          0xEA
#define G_SETKEYR           0xEB
#define G_SETCONVERT        0xEC
#define G_SETSCISSOR        0xED
#define G_SETPRIMDEPTH      0xEE
#define G_RDPSETOTHERMODE   0xEF
#define G_LOADTLUT          0xF0
#define G_SETTILESIZE       0xF2
#define G_LOADBLOCK         0xF3
#define G_LOADTILE          0xF4
#define G_SETTILE           0xF5
#define G_FILLRECT          0xF6
#define G_SETFILLCOLOR      0xF7
#define G_SETFOGCOLOR       0xF8
#define G_SETBLENDCOLOR     0xF9
#define G_SETPRIMCOLOR      0xFA
#define G_SETENVCOLOR       0xFB
#define G_SETCOMBINE        0xFC
#define G_SETTIMG           0xFD
#define G_SETZIMG           0xFE
#define G_SETCIMG           0xFF

/* gSPMatrix params */
#define G_MTX_MODELVIEW  0x00
#define G_MTX_PROJECTION 0x01
#define G_MTX_MUL        0x00
#define G_MTX_LOAD       0x02
#define G_MTX_NOPUSH     0x00
#define G_MTX_PUSH       0x04

/* movemem */
#define G_MV_VIEWPORT 0x80

/* moveword indices / offsets */
#define G_MW_MATRIX     0x00
#define G_MW_NUMLIGHT   0x02
#define G_MW_CLIP       0x04
#define G_MW_SEGMENT    0x06
#define G_MW_FOG        0x08
#define G_MW_LIGHTCOL   0x0A
#define G_MW_POINTS     0x0C
#define G_MW_PERSPNORM  0x0E
#define G_MWO_SEGMENT_0 0x00
#define G_MWO_FOG       0x00
#define G_MWO_CLIP_RNX  0x04
#define G_MWO_CLIP_RNY  0x0C
#define G_MWO_CLIP_RPX  0x14
#define G_MWO_CLIP_RPY  0x1C
#define G_MWO_MATRIX_XX_XY_I 0x00

#define G_DL_PUSH   0x00
#define G_DL_NOPUSH 0x01

/* geometry mode */
#define G_ZBUFFER            0x00000001
#define G_SHADE              0x00000004
#define G_SHADING_SMOOTH     0x00000200
#define G_CULL_FRONT         0x00001000
#define G_CULL_BACK          0x00002000
#define G_CULL_BOTH          0x00003000
#define G_FOG                0x00010000
#define G_LIGHTING           0x00020000
#define G_TEXTURE_GEN        0x00040000
#define G_TEXTURE_GEN_LINEAR 0x00080000
#define G_LOD                0x00100000
#define G_CLIPPING           0x00800000

#define G_ON  1
#define G_OFF 0
#define G_MAXZ 0x03ff

/* image formats */
#define G_IM_FMT_RGBA 0
#define G_IM_FMT_YUV  1
#define G_IM_FMT_CI   2
#define G_IM_FMT_IA   3
#define G_IM_FMT_I    4
#define G_IM_SIZ_4b   0
#define G_IM_SIZ_8b   1
#define G_IM_SIZ_16b  2
#define G_IM_SIZ_32b  3

/* tiles */
#define G_TX_LOADTILE   7
#define G_TX_RENDERTILE 0
#define G_TX_NOMIRROR   0
#define G_TX_WRAP       0
#define G_TX_MIRROR     1
#define G_TX_CLAMP      2
#define G_TX_NOMASK     0
#define G_TX_NOLOD      0
#define G_TX_DXT_FRAC   11

/* othermode H shifts */
#define G_MDSFT_ALPHADITHER   4
#define G_MDSFT_RGBDITHER     6
#define G_MDSFT_COMBKEY       8
#define G_MDSFT_TEXTCONV      9
#define G_MDSFT_TEXTFILT      12
#define G_MDSFT_TEXTLUT       14
#define G_MDSFT_TEXTLOD       16
#define G_MDSFT_TEXTDETAIL    17
#define G_MDSFT_TEXTPERSP     19
#define G_MDSFT_CYCLETYPE     20
#define G_MDSFT_COLORDITHER   22
#define G_MDSFT_PIPELINE      23
/* othermode L shifts */
#define G_MDSFT_ALPHACOMPARE  0
#define G_MDSFT_ZSRCSEL       2
#define G_MDSFT_RENDERMODE    3
#define G_MDSFT_BLENDER       16

#define G_CYC_1CYCLE (0u << G_MDSFT_CYCLETYPE)
#define G_CYC_2CYCLE (1u << G_MDSFT_CYCLETYPE)
#define G_CYC_COPY   (2u << G_MDSFT_CYCLETYPE)
#define G_CYC_FILL   (3u << G_MDSFT_CYCLETYPE)
#define G_TP_NONE    (0u << G_MDSFT_TEXTPERSP)
#define G_TP_PERSP   (1u << G_MDSFT_TEXTPERSP)
#define G_TL_TILE    (0u << G_MDSFT_TEXTLOD)
#define G_TL_LOD     (1u << G_MDSFT_TEXTLOD)
#define G_TT_NONE    (0u << G_MDSFT_TEXTLUT)
#define G_TT_RGBA16  (2u << G_MDSFT_TEXTLUT)
#define G_TT_IA16    (3u << G_MDSFT_TEXTLUT)
#define G_TF_POINT   (0u << G_MDSFT_TEXTFILT)
#define G_TF_AVERAGE (3u << G_MDSFT_TEXTFILT)
#define G_TF_BILERP  (2u << G_MDSFT_TEXTFILT)
#define G_AC_NONE      (0u << G_MDSFT_ALPHACOMPARE)
#define G_AC_THRESHOLD (1u << G_MDSFT_ALPHACOMPARE)
#define G_AC_DITHER    (3u << G_MDSFT_ALPHACOMPARE)
#define G_SC_NON_INTERLACE 0

/* render mode flags */
#define AA_EN          0x8
#define Z_CMP          0x10
#define Z_UPD          0x20
#define IM_RD          0x40
#define CLR_ON_CVG     0x80
#define CVG_DST_CLAMP  0
#define CVG_DST_WRAP   0x100
#define CVG_DST_FULL   0x200
#define CVG_DST_SAVE   0x300
#define ZMODE_OPA      0
#define ZMODE_INTER    0x400
#define ZMODE_XLU      0x800
#define ZMODE_DEC      0xc00
#define CVG_X_ALPHA    0x1000
#define ALPHA_CVG_SEL  0x2000
#define FORCE_BL       0x4000
#define TEX_EDGE       0x0000

#define G_BL_CLR_IN  0
#define G_BL_CLR_MEM 1
#define G_BL_CLR_BL  2
#define G_BL_CLR_FOG 3
#define G_BL_1MA     0
#define G_BL_A_MEM   1
#define G_BL_A_IN    0
#define G_BL_A_FOG   1
#define G_BL_A_SHADE 2
#define G_BL_1       2
#define G_BL_0       3

#define GBL_c1(m1a, m1b, m2a, m2b) \
    (((u32)(m1a) << 30) | ((u32)(m1b) << 26) | ((u32)(m2a) << 22) | ((u32)(m2b) << 18))
#define GBL_c2(m1a, m1b, m2a, m2b) \
    (((u32)(m1a) << 28) | ((u32)(m1b) << 24) | ((u32)(m2a) << 20) | ((u32)(m2b) << 16))

#define RM_OPA_SURF(clk) \
    (CVG_DST_CLAMP | FORCE_BL | ZMODE_OPA | GBL_c##clk(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1))
#define RM_XLU_SURF(clk) \
    (IM_RD | CVG_DST_FULL | FORCE_BL | ZMODE_OPA | GBL_c##clk(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA))
#define RM_TEX_EDGE(clk) \
    (CVG_DST_CLAMP | CVG_X_ALPHA | ALPHA_CVG_SEL | FORCE_BL | ZMODE_OPA | TEX_EDGE | AA_EN | \
     GBL_c##clk(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_A_MEM))
#define RM_AA_XLU_LINE(clk) \
    (AA_EN | IM_RD | CVG_DST_CLAMP | CVG_X_ALPHA | ALPHA_CVG_SEL | FORCE_BL | ZMODE_OPA | \
     GBL_c##clk(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA))
#define RM_AA_OPA_SURF(clk) \
    (AA_EN | IM_RD | CVG_DST_CLAMP | ZMODE_OPA | ALPHA_CVG_SEL | \
     GBL_c##clk(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_A_MEM))
#define RM_RA_OPA_SURF(clk) \
    (AA_EN | CVG_DST_CLAMP | ZMODE_OPA | ALPHA_CVG_SEL | \
     GBL_c##clk(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_A_MEM))
#define RM_OPA_CI(clk) \
    (CVG_DST_CLAMP | ZMODE_OPA | GBL_c##clk(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1))

#define G_RM_NOOP          0
#define G_RM_NOOP2         0
#define G_RM_OPA_SURF      RM_OPA_SURF(1)
#define G_RM_OPA_SURF2     RM_OPA_SURF(2)
#define G_RM_XLU_SURF      RM_XLU_SURF(1)
#define G_RM_XLU_SURF2     RM_XLU_SURF(2)
#define G_RM_TEX_EDGE      RM_TEX_EDGE(1)
#define G_RM_TEX_EDGE2     RM_TEX_EDGE(2)
#define G_RM_AA_XLU_LINE   RM_AA_XLU_LINE(1)
#define G_RM_AA_XLU_LINE2  RM_AA_XLU_LINE(2)
#define G_RM_AA_OPA_SURF   RM_AA_OPA_SURF(1)
#define G_RM_AA_OPA_SURF2  RM_AA_OPA_SURF(2)
#define G_RM_RA_OPA_SURF   RM_RA_OPA_SURF(1)
#define G_RM_RA_OPA_SURF2  RM_RA_OPA_SURF(2)
#define G_RM_OPA_CI        RM_OPA_CI(1)
#define G_RM_OPA_CI2       RM_OPA_CI(2)
#define G_RM_FOG_SHADE_A   GBL_c1(G_BL_CLR_FOG, G_BL_A_SHADE, G_BL_CLR_IN, G_BL_1MA)
#define G_RM_FOG_PRIM_A    GBL_c1(G_BL_CLR_FOG, G_BL_A_FOG, G_BL_CLR_IN, G_BL_1MA)
#define G_RM_PASS          GBL_c1(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1)

/* color combiner inputs */
#define G_CCMUX_COMBINED        0
#define G_CCMUX_TEXEL0          1
#define G_CCMUX_TEXEL1          2
#define G_CCMUX_PRIMITIVE       3
#define G_CCMUX_SHADE           4
#define G_CCMUX_ENVIRONMENT     5
#define G_CCMUX_CENTER          6
#define G_CCMUX_SCALE           6
#define G_CCMUX_COMBINED_ALPHA  7
#define G_CCMUX_TEXEL0_ALPHA    8
#define G_CCMUX_TEXEL1_ALPHA    9
#define G_CCMUX_PRIMITIVE_ALPHA 10
#define G_CCMUX_SHADE_ALPHA     11
#define G_CCMUX_ENV_ALPHA       12
#define G_CCMUX_LOD_FRACTION    13
#define G_CCMUX_PRIM_LOD_FRAC   14
#define G_CCMUX_NOISE           7
#define G_CCMUX_K4              7
#define G_CCMUX_K5              15
#define G_CCMUX_1               6
#define G_CCMUX_0               31

#define G_ACMUX_COMBINED      0
#define G_ACMUX_TEXEL0        1
#define G_ACMUX_TEXEL1        2
#define G_ACMUX_PRIMITIVE     3
#define G_ACMUX_SHADE         4
#define G_ACMUX_ENVIRONMENT   5
#define G_ACMUX_LOD_FRACTION  0
#define G_ACMUX_PRIM_LOD_FRAC 6
#define G_ACMUX_1             6
#define G_ACMUX_0             7

#define G_CC_PRIMITIVE  0, 0, 0, PRIMITIVE, 0, 0, 0, PRIMITIVE
#define G_CC_SHADE      0, 0, 0, SHADE, 0, 0, 0, SHADE
#define G_CC_PASS2      0, 0, 0, COMBINED, 0, 0, 0, COMBINED

#define GCCc0w0(saRGB0, mRGB0, saA0, mA0) \
    (_SHIFTL((saRGB0), 20, 4) | _SHIFTL((mRGB0), 15, 5) | _SHIFTL((saA0), 12, 3) | _SHIFTL((mA0), 9, 3))
#define GCCc1w0(saRGB1, mRGB1) (_SHIFTL((saRGB1), 5, 4) | _SHIFTL((mRGB1), 0, 5))
#define GCCc0w1(sbRGB0, aRGB0, sbA0, aA0) \
    (_SHIFTL((sbRGB0), 28, 4) | _SHIFTL((aRGB0), 15, 3) | _SHIFTL((sbA0), 12, 3) | _SHIFTL((aA0), 9, 3))
#define GCCc1w1(sbRGB1, saA1, mA1, aRGB1, sbA1, aA1) \
    (_SHIFTL((sbRGB1), 24, 4) | _SHIFTL((saA1), 21, 3) | _SHIFTL((mA1), 18, 3) | \
     _SHIFTL((aRGB1), 6, 3) | _SHIFTL((sbA1), 3, 3) | _SHIFTL((aA1), 0, 3))

/* ------------------------------------------------------------------------ */
/* Display list macros                                                      */
/* ------------------------------------------------------------------------ */
#define _gW(pkt, c0, c1) do { \
    Gfx *_g = (Gfx *)(pkt); \
    _g->words.w0 = (u32)(c0); \
    _g->words.pad_ = 0; \
    _g->words.w1 = (uintptr_t)(c1); \
} while (0)

#define gDma0p(pkt, c, s, l)  _gW(pkt, _SHIFTL((c), 24, 8) | _SHIFTL((l), 0, 16), (s))

#define gSPMatrix(pkt, m, p)    _gW(pkt, _SHIFTL(G_MTX, 24, 8) | _SHIFTL((p), 0, 8), (m))
#define gSPPopMatrix(pkt, n)    _gW(pkt, _SHIFTL(G_POPMTX, 24, 8), (n))
#define gSPViewport(pkt, v)     _gW(pkt, _SHIFTL(G_MOVEMEM, 24, 8) | _SHIFTL(G_MV_VIEWPORT, 16, 8), (v))
#define gSPVertex(pkt, v, n, v0) \
    _gW(pkt, _SHIFTL(G_VTX, 24, 8) | _SHIFTL((v0), 16, 8) | _SHIFTL((n), 8, 8), (v))
#define gSPDisplayList(pkt, dl) _gW(pkt, _SHIFTL(G_DL, 24, 8) | _SHIFTL(G_DL_PUSH, 16, 8), (dl))
#define gSPBranchList(pkt, dl)  _gW(pkt, _SHIFTL(G_DL, 24, 8) | _SHIFTL(G_DL_NOPUSH, 16, 8), (dl))
#define gSPEndDisplayList(pkt)  _gW(pkt, _SHIFTL(G_ENDDL, 24, 8), 0)

#define __gTri(v0, v1, v2) (_SHIFTL((v0), 16, 8) | _SHIFTL((v1), 8, 8) | _SHIFTL((v2), 0, 8))
#define gSP1Triangle(pkt, v0, v1, v2, flag) \
    _gW(pkt, _SHIFTL(G_TRI1, 24, 8), __gTri(v0, v1, v2) | _SHIFTL((flag), 24, 8))
#define gSP2Triangles(pkt, v00, v01, v02, flag0, v10, v11, v12, flag1) \
    _gW(pkt, _SHIFTL(G_TRI2, 24, 8) | __gTri(v00, v01, v02), __gTri(v10, v11, v12))
/* Same vertex ordering as F3DEX's quadrangle expansion */
#define __gQ0(v0, v1, v2, v3, f) \
    ((f) == 0 ? __gTri(v0, v1, v2) : (f) == 1 ? __gTri(v1, v2, v3) : (f) == 2 ? __gTri(v2, v3, v0) : __gTri(v3, v0, v1))
#define __gQ1(v0, v1, v2, v3, f) \
    ((f) == 0 ? __gTri(v0, v2, v3) : (f) == 1 ? __gTri(v1, v3, v0) : (f) == 2 ? __gTri(v2, v0, v1) : __gTri(v3, v1, v2))
#define gSP1Quadrangle(pkt, v0, v1, v2, v3, flag) \
    _gW(pkt, _SHIFTL(G_TRI2, 24, 8) | __gQ0(v0, v1, v2, v3, flag), __gQ1(v0, v1, v2, v3, flag))
#define gSPLine3D(pkt, v0, v1, flag) \
    _gW(pkt, _SHIFTL(G_LINE3D, 24, 8), __gTri(v0, v1, 0) | _SHIFTL((flag), 24, 8))

#define gSPTexture(pkt, s, t, level, tile, on) \
    _gW(pkt, _SHIFTL(G_TEXTURE, 24, 8) | _SHIFTL((level), 11, 3) | _SHIFTL((tile), 8, 3) | _SHIFTL((on), 0, 8), \
        _SHIFTL((s), 16, 16) | _SHIFTL((t), 0, 16))
#define gSPSetGeometryMode(pkt, word)   _gW(pkt, _SHIFTL(G_SETGEOMETRYMODE, 24, 8), (u32)(word))
#define gSPClearGeometryMode(pkt, word) _gW(pkt, _SHIFTL(G_CLEARGEOMETRYMODE, 24, 8), (u32)(word))

#define gMoveWd(pkt, index, offset, data) \
    _gW(pkt, _SHIFTL(G_MOVEWORD, 24, 8) | _SHIFTL((offset), 8, 16) | _SHIFTL((index), 0, 8), (u32)(data))
#define gSPSegment(pkt, segment, base) gMoveWd(pkt, G_MW_SEGMENT, (segment) * 4, base)
#define gSPPerspNormalize(pkt, s)      gMoveWd(pkt, G_MW_PERSPNORM, 0, (s))
#define gSPFogFactor(pkt, fm, fo) \
    gMoveWd(pkt, G_MW_FOG, G_MWO_FOG, (_SHIFTL(fm, 16, 16) | _SHIFTL(fo, 0, 16)))

#define gSPSetOtherMode(pkt, cmd, sft, len, data) \
    _gW(pkt, _SHIFTL(cmd, 24, 8) | _SHIFTL(sft, 8, 8) | _SHIFTL(len, 0, 8), (u32)(data))
#define gDPSetCycleType(pkt, type)   gSPSetOtherMode(pkt, G_SETOTHERMODE_H, G_MDSFT_CYCLETYPE, 2, type)
#define gDPSetTexturePersp(pkt, t)   gSPSetOtherMode(pkt, G_SETOTHERMODE_H, G_MDSFT_TEXTPERSP, 1, t)
#define gDPSetTextureDetail(pkt, t)  gSPSetOtherMode(pkt, G_SETOTHERMODE_H, G_MDSFT_TEXTDETAIL, 2, t)
#define gDPSetTextureLOD(pkt, t)     gSPSetOtherMode(pkt, G_SETOTHERMODE_H, G_MDSFT_TEXTLOD, 1, t)
#define gDPSetTextureLUT(pkt, t)     gSPSetOtherMode(pkt, G_SETOTHERMODE_H, G_MDSFT_TEXTLUT, 2, t)
#define gDPSetTextureFilter(pkt, t)  gSPSetOtherMode(pkt, G_SETOTHERMODE_H, G_MDSFT_TEXTFILT, 2, t)
#define gDPSetTextureConvert(pkt, t) gSPSetOtherMode(pkt, G_SETOTHERMODE_H, G_MDSFT_TEXTCONV, 3, t)
#define gDPSetCombineKey(pkt, t)     gSPSetOtherMode(pkt, G_SETOTHERMODE_H, G_MDSFT_COMBKEY, 1, t)
#define gDPSetColorDither(pkt, t)    gSPSetOtherMode(pkt, G_SETOTHERMODE_H, G_MDSFT_RGBDITHER, 2, t)
#define gDPSetAlphaDither(pkt, t)    gSPSetOtherMode(pkt, G_SETOTHERMODE_H, G_MDSFT_ALPHADITHER, 2, t)
#define gDPPipelineMode(pkt, t)      gSPSetOtherMode(pkt, G_SETOTHERMODE_H, G_MDSFT_PIPELINE, 1, t)
#define gDPSetAlphaCompare(pkt, t)   gSPSetOtherMode(pkt, G_SETOTHERMODE_L, G_MDSFT_ALPHACOMPARE, 2, t)
#define gDPSetDepthSource(pkt, t)    gSPSetOtherMode(pkt, G_SETOTHERMODE_L, G_MDSFT_ZSRCSEL, 1, t)
#define gDPSetRenderMode(pkt, c0, c1) \
    gSPSetOtherMode(pkt, G_SETOTHERMODE_L, G_MDSFT_RENDERMODE, 29, (u32)(c0) | (u32)(c1))

#define gDPNoParam(pkt, cmd) _gW(pkt, _SHIFTL(cmd, 24, 8), 0)
#define gDPPipeSync(pkt) gDPNoParam(pkt, G_RDPPIPESYNC)
#define gDPLoadSync(pkt) gDPNoParam(pkt, G_RDPLOADSYNC)
#define gDPTileSync(pkt) gDPNoParam(pkt, G_RDPTILESYNC)
#define gDPFullSync(pkt) gDPNoParam(pkt, G_RDPFULLSYNC)
#define gDPNoOp(pkt)     gDPNoParam(pkt, G_NOOP)

#define gDPSetColor(pkt, c, d) _gW(pkt, _SHIFTL(c, 24, 8), (u32)(d))
#define DPRGBColor(pkt, cmd, r, g, b, a) \
    gDPSetColor(pkt, cmd, (_SHIFTL(r, 24, 8) | _SHIFTL(g, 16, 8) | _SHIFTL(b, 8, 8) | _SHIFTL(a, 0, 8)))
#define gDPSetEnvColor(pkt, r, g, b, a)   DPRGBColor(pkt, G_SETENVCOLOR, r, g, b, a)
#define gDPSetBlendColor(pkt, r, g, b, a) DPRGBColor(pkt, G_SETBLENDCOLOR, r, g, b, a)
#define gDPSetFogColor(pkt, r, g, b, a)   DPRGBColor(pkt, G_SETFOGCOLOR, r, g, b, a)
#define gDPSetFillColor(pkt, d)           gDPSetColor(pkt, G_SETFILLCOLOR, (d))
#define gDPSetPrimColor(pkt, m, l, r, g, b, a) \
    _gW(pkt, _SHIFTL(G_SETPRIMCOLOR, 24, 8) | _SHIFTL(m, 8, 8) | _SHIFTL(l, 0, 8), \
        (_SHIFTL(r, 24, 8) | _SHIFTL(g, 16, 8) | _SHIFTL(b, 8, 8) | _SHIFTL(a, 0, 8)))

#define GPACK_RGBA5551(r, g, b, a) \
    ((((r) << 8) & 0xf800) | (((g) << 3) & 0x7c0) | (((b) >> 2) & 0x3e) | ((a) & 0x1))

#define gDPSetCombineLERP(pkt, a0, b0, c0, d0, Aa0, Ab0, Ac0, Ad0, a1, b1, c1, d1, Aa1, Ab1, Ac1, Ad1) \
    _gW(pkt, _SHIFTL(G_SETCOMBINE, 24, 8) | \
        _SHIFTL(GCCc0w0(G_CCMUX_##a0, G_CCMUX_##c0, G_ACMUX_##Aa0, G_ACMUX_##Ac0) | \
                GCCc1w0(G_CCMUX_##a1, G_CCMUX_##c1), 0, 24), \
        (u32)(GCCc0w1(G_CCMUX_##b0, G_CCMUX_##d0, G_ACMUX_##Ab0, G_ACMUX_##Ad0) | \
              GCCc1w1(G_CCMUX_##b1, G_ACMUX_##Aa1, G_ACMUX_##Ac1, G_CCMUX_##d1, G_ACMUX_##Ab1, G_ACMUX_##Ad1)))
#define gDPSetCombineMode(pkt, a, b) gDPSetCombineLERP(pkt, a, b)

#define gDPSetImage(pkt, cmd, fmt, siz, width, i) \
    _gW(pkt, _SHIFTL(cmd, 24, 8) | _SHIFTL(fmt, 21, 3) | _SHIFTL(siz, 19, 2) | _SHIFTL((width) - 1, 0, 12), (i))
#define gDPSetColorImage(pkt, f, s, w, i)   gDPSetImage(pkt, G_SETCIMG, f, s, w, i)
#define gDPSetDepthImage(pkt, i)            gDPSetImage(pkt, G_SETZIMG, 0, 0, 1, i)
#define gDPSetTextureImage(pkt, f, s, w, i) gDPSetImage(pkt, G_SETTIMG, f, s, w, i)

#define gDPSetTile(pkt, fmt, siz, line, tmem, tile, palette, cmt, maskt, shiftt, cms, masks, shifts) \
    _gW(pkt, _SHIFTL(G_SETTILE, 24, 8) | _SHIFTL(fmt, 21, 3) | _SHIFTL(siz, 19, 2) | \
             _SHIFTL(line, 9, 9) | _SHIFTL(tmem, 0, 9), \
        _SHIFTL(tile, 24, 3) | _SHIFTL(palette, 20, 4) | _SHIFTL(cmt, 18, 2) | _SHIFTL(maskt, 14, 4) | \
        _SHIFTL(shiftt, 10, 4) | _SHIFTL(cms, 8, 2) | _SHIFTL(masks, 4, 4) | _SHIFTL(shifts, 0, 4))

#define gDPLoadTileGeneric(pkt, c, tile, uls, ult, lrs, lrt) \
    _gW(pkt, _SHIFTL(c, 24, 8) | _SHIFTL(uls, 12, 12) | _SHIFTL(ult, 0, 12), \
        _SHIFTL(tile, 24, 3) | _SHIFTL(lrs, 12, 12) | _SHIFTL(lrt, 0, 12))
#define gDPSetTileSize(pkt, t, uls, ult, lrs, lrt) gDPLoadTileGeneric(pkt, G_SETTILESIZE, t, uls, ult, lrs, lrt)
#define gDPLoadTile(pkt, t, uls, ult, lrs, lrt)    gDPLoadTileGeneric(pkt, G_LOADTILE, t, uls, ult, lrs, lrt)
#define gDPLoadBlock(pkt, tile, uls, ult, lrs, dxt) \
    _gW(pkt, _SHIFTL(G_LOADBLOCK, 24, 8) | _SHIFTL(uls, 12, 12) | _SHIFTL(ult, 0, 12), \
        _SHIFTL(tile, 24, 3) | _SHIFTL(lrs, 12, 12) | _SHIFTL(dxt, 0, 12))
#define gDPLoadTLUTCmd(pkt, tile, count) \
    _gW(pkt, _SHIFTL(G_LOADTLUT, 24, 8), _SHIFTL((tile), 24, 3) | _SHIFTL((count), 14, 10))

#define gDPSetScissor(pkt, mode, ulx, uly, lrx, lry) \
    _gW(pkt, _SHIFTL(G_SETSCISSOR, 24, 8) | _SHIFTL((int)((float)(ulx) * 4.0f), 12, 12) | \
             _SHIFTL((int)((float)(uly) * 4.0f), 0, 12), \
        _SHIFTL(mode, 24, 2) | _SHIFTL((int)((float)(lrx) * 4.0f), 12, 12) | _SHIFTL((int)((float)(lry) * 4.0f), 0, 12))

#define gDPFillRectangle(pkt, ulx, uly, lrx, lry) \
    _gW(pkt, _SHIFTL(G_FILLRECT, 24, 8) | _SHIFTL((lrx), 14, 10) | _SHIFTL((lry), 2, 10), \
        _SHIFTL((ulx), 14, 10) | _SHIFTL((uly), 2, 10))

/* Texture rectangles take two Gfx words: coords, then s/t and steps. */
#define gSPTextureRectangle(pkt, xl, yl, xh, yh, tile, s, t, dsdx, dtdy) do { \
    _gW(pkt, _SHIFTL(G_TEXRECT, 24, 8) | _SHIFTL(xh, 12, 12) | _SHIFTL(yh, 0, 12), \
        _SHIFTL(tile, 24, 3) | _SHIFTL(xl, 12, 12) | _SHIFTL(yl, 0, 12)); \
    _gW(pkt, _SHIFTL(G_RDPHALF_1, 24, 8), _SHIFTL(s, 16, 16) | _SHIFTL(t, 0, 16)); \
    _gW(pkt, _SHIFTL(G_RDPHALF_2, 24, 8), _SHIFTL(dsdx, 16, 16) | _SHIFTL(dtdy, 0, 16)); \
} while (0)

#define gSPTextureRectangleFlip(pkt, xl, yl, xh, yh, tile, s, t, dsdx, dtdy) do { \
    _gW(pkt, _SHIFTL(G_TEXRECTFLIP, 24, 8) | _SHIFTL(xh, 12, 12) | _SHIFTL(yh, 0, 12), \
        _SHIFTL(tile, 24, 3) | _SHIFTL(xl, 12, 12) | _SHIFTL(yl, 0, 12)); \
    _gW(pkt, _SHIFTL(G_RDPHALF_1, 24, 8), _SHIFTL(s, 16, 16) | _SHIFTL(t, 0, 16)); \
    _gW(pkt, _SHIFTL(G_RDPHALF_2, 24, 8), _SHIFTL(dsdx, 16, 16) | _SHIFTL(dtdy, 0, 16)); \
} while (0)

#endif /* D64PC_ULTRA64_H */
