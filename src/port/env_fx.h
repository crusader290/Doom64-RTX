#ifndef D64_ENV_FX_H
#define D64_ENV_FX_H
void PCEnv_Reset(void);
void PCEnv_Tick(void);
void PCEnv_Draw(void);
int PCEnv_Light(int index,float position[3],float color[3]);
#endif
