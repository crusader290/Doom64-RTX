/* Exercise the actual cosmetic implementation with a tiny synthetic world. */
#include "../src/port/gore.c"
#include <assert.h>

pcconfig_t pc_config;
boolean demoplayback, demorecording;
fixed_t viewsin, viewcos;
static sector_t test_sector;
static subsector_t test_subsector;
static Gfx commands[10000];
static Vtx vertices[10000];
Gfx *GFX1;
Vtx *VTX1;
static line_t wall;
static int wall_collision;
subsector_t *R_PointInSubsector(fixed_t x, fixed_t y)
{ (void)x; (void)y; return &test_subsector; }
void I_CheckGFX(void)
{ assert(GFX1 < commands + 9900); assert(VTX1 < vertices + 9900); }
boolean P_PathTraverse(fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2,
                       int flags, boolean (*callback)(intercept_t *))
{
    intercept_t in;
    (void)x1; (void)y1; (void)x2; (void)y2; (void)flags;
    if (!wall_collision) return true;
    memset(&in, 0, sizeof(in));
    in.frac = FRACUNIT / 2; in.d.line = &wall;
    return callback(&in);
}
static int live_particles(void)
{ int i, n = 0; for (i = 0; i < PARTICLES; i++) n += particles[i].life > 0; return n; }
static int live_stains(void)
{ int i, n = 0; for (i = 0; i < STAINS; i++) n += stains[i].life > 0; return n; }
int main(void)
{
    mobj_t target = {0}, source = {0};
    mobjinfo_t info = {0};
    gore_particle_t before[PARTICLES];
    int i;
    pc_config.gore = 1;
    test_subsector.sector = &test_sector;
    test_sector.ceilingheight = 128 * FRACUNIT; test_sector.lightlevel = 200;
    wall.frontsector = &test_sector; wall.dy = 100 * FRACUNIT;
    target.info = &info; info.spawnhealth = 100;
    target.health = 100; target.height = 40 * FRACUNIT;
    target.x = 40 * FRACUNIT; viewcos = FRACUNIT;
    I_PCGoreReset();
    for (i = 0; i < 1000; i++) I_PCGoreDamage(&target, &source, 250);
    assert(live_particles() == PARTICLES);
    for (i = 0; i < PARTICLES; i++) assert(particles[i].vx > -10 && particles[i].vx < 20);
    GFX1 = commands; VTX1 = vertices;
    memcpy(before, particles, sizeof(before));
    I_PCGoreDraw(.25f); I_PCGoreDraw(.75f);
    assert(!memcmp(before, particles, sizeof(before))); /* drawers cannot tick */
    for (i = 0; i < 120; i++) I_PCGoreTick();
    assert(live_particles() == 0 && live_stains() > 0);
    for (i = 0; i < 901; i++) I_PCGoreTick();
    assert(live_stains() == 0);
    I_PCGoreDamage(&target, &source, 20); wall_collision = 1;
    I_PCGoreTick();
    assert(live_particles() == 0 && live_stains() > 0);
    I_PCGoreReset(); demoplayback = true;
    I_PCGoreDamage(&target, &source, 250); assert(live_particles() == 0);
    demoplayback = false; demorecording = true;
    I_PCGoreDamage(&target, &source, 250); assert(live_particles() == 0);
    demorecording = false; target.flags = MF_NOBLOOD;
    I_PCGoreDamage(&target, &source, 250); assert(live_particles() == 0);
    target.flags = 0; I_PCGoreDamage(&target, &source, 250);
    pc_config.gore = 0; I_PCGoreTick(); assert(live_particles() == 0);
    puts("PASS: gore caps, collision, expiry, draw purity, demo gating and disable/reset");
    return 0;
}
