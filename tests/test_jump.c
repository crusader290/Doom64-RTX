/* Actual native takeoff/landing code, including a long fall after jumping. */
#include "../src/doom64/p_user.c"
#include <assert.h>
pcconfig_t pc_config;
boolean demoplayback,demorecording;
int vblsinframe[MAXPLAYERS]={2};
fixed_t finesine[5*FINEANGLES/4];
fixed_t *finecosine=finesine+FINEANGLES/4;
state_t states[NUMSTATES];
static int grunts;
void S_StartSound(mobj_t *mo,int sound) { (void)mo;assert(sound==sfx_oof);grunts++; }
boolean P_SetMobjState(mobj_t *mo,statenum_t state) { mo->state=&states[state];return true; }
static void jump(fixed_t floor)
{
    player_t p={0};mobj_t mo={0};p.mo=&mo;mo.player=&p;
    mo.ceilingz=500*FRACUNIT;mo.height=56*FRACUNIT;p.onground=true;p.pc_buttons=PCACT_JUMP;
    int start=grunts;P_MovePlayer(&p);assert(grunts==start+1 && mo.momz==PC_JUMPSPEED);
    p.pc_buttons=0;mo.floorz=floor;
    for(int i=0;i<200 && (mo.z>floor || mo.momz);i++) {
        p.onground=mo.z<=mo.floorz;P_PlayerZMovement(&mo);P_MovePlayer(&p);
    }
    assert(mo.z==floor && mo.momz==0 && grunts==start+1);
}
int main(void)
{
    pc_config.jump=1;jump(0);jump(-300*FRACUNIT);
    player_t p={0};mobj_t mo={0};p.mo=&mo;mo.player=&p;mo.z=10*FRACUNIT;
    mo.ceilingz=500*FRACUNIT;mo.momz=-12*FRACUNIT;
    int before=grunts;P_PlayerZMovement(&mo);assert(grunts==before+1);
    puts("PASS: jump grunt at takeoff only, normal/long landing silent, ordinary fall grunt retained");
}
