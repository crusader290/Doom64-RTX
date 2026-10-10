/*
 * input.c - keyboard, mouse and gamepad mapped onto an emulated N64 pad.
 *
 * The game reads one 32-bit word per tic: buttons in the high 16 bits and
 * the analog stick (signed x, y) in the low 16 bits, exactly like the
 * original OSContPad read as an int on the big-endian N64.
 *
 * Default keyboard layout targets the game's "Default 1" control setup:
 *   W/S or Up/Down  forward/back     Left/Right   turn
 *   A/D             strafe (L/R)     Mouse        turn / look
 *   Ctrl, LMB       fire (Z)         E, MMB       use (C-right)
 *   Shift           run (C-left)     Alt          strafe modifier (C-down)
 *   Tab             automap (C-up)   Q / wheel down  previous weapon (A)
 *   wheel up        next weapon (B)  Enter        menu confirm (A)
 *   Esc             start/menu       Backspace    menu back (B)
 * PC-only actions (not part of the pad word, read by the game directly):
 *   Space           jump             RMB          aim down sights
 *   1-8             select weapon    mouse Y      look up/down
 *   V, mouse 4      kick
 *
 * Doom64-RTX PC port, GPLv3.
 */
#include <SDL3/SDL.h>
#include <string.h>
#include <math.h>

#include <ultra64.h>
#include "input.h"
#include "config.h"

typedef struct { SDL_Scancode key; uint16_t button; } keybind_t;

static const keybind_t keybinds[] = {
    { SDL_SCANCODE_W,         CONT_UP },
    { SDL_SCANCODE_UP,        CONT_UP },
    { SDL_SCANCODE_S,         CONT_DOWN },
    { SDL_SCANCODE_DOWN,      CONT_DOWN },
    { SDL_SCANCODE_LEFT,      CONT_LEFT },
    { SDL_SCANCODE_RIGHT,     CONT_RIGHT },
    { SDL_SCANCODE_A,         CONT_L },
    { SDL_SCANCODE_D,         CONT_R },
    { SDL_SCANCODE_LCTRL,     CONT_G },
    { SDL_SCANCODE_RCTRL,     CONT_G },
    { SDL_SCANCODE_E,         CONT_F },
    { SDL_SCANCODE_LSHIFT,    CONT_C },
    { SDL_SCANCODE_RSHIFT,    CONT_C },
    { SDL_SCANCODE_LALT,      CONT_D },
    { SDL_SCANCODE_TAB,       CONT_E },
    { SDL_SCANCODE_Q,         CONT_A },
    { SDL_SCANCODE_RETURN,    CONT_A },
    { SDL_SCANCODE_KP_ENTER,  CONT_A },
    { SDL_SCANCODE_BACKSPACE, CONT_B },
    { SDL_SCANCODE_ESCAPE,    CONT_START },
};

static SDL_Gamepad *gamepad;
static float mouse_dx_accum;
static float mouse_dy_accum;
static int weapon_key;
static int wheel_pulse_up, wheel_pulse_down;
static uint16_t mouse_buttons;
static int input_grab;

void IN_Init(void)
{
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (ids && count > 0)
        gamepad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
}

void IN_Shutdown(void)
{
    if (gamepad)
        SDL_CloseGamepad(gamepad);
    gamepad = NULL;
}

void IN_SetGrab(SDL_Window *window, int grab)
{
    input_grab = grab && pc_config.mouse;
    SDL_SetWindowRelativeMouseMode(window, input_grab ? true : false);
}

void IN_HandleEvent(const SDL_Event *ev)
{
    switch (ev->type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        if (input_grab)
        {
            mouse_dx_accum += ev->motion.xrel;
            mouse_dy_accum += ev->motion.yrel;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        uint16_t b = 0;
        if (ev->button.button == SDL_BUTTON_LEFT) b = CONT_G;
        else if (ev->button.button == SDL_BUTTON_MIDDLE) b = CONT_F;
        if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN) mouse_buttons |= b;
        else mouse_buttons &= (uint16_t)~b;
        break;
    }
    case SDL_EVENT_KEY_DOWN:
        if (!ev->key.repeat && ev->key.scancode >= SDL_SCANCODE_1 && ev->key.scancode <= SDL_SCANCODE_9)
            weapon_key = ev->key.scancode - SDL_SCANCODE_1 + 1;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (ev->wheel.y > 0) wheel_pulse_up = 2;
        else if (ev->wheel.y < 0) wheel_pulse_down = 2;
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (!gamepad)
            gamepad = SDL_OpenGamepad(ev->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (gamepad && SDL_GetGamepadID(gamepad) == ev->gdevice.which)
        {
            SDL_CloseGamepad(gamepad);
            gamepad = NULL;
        }
        break;
    default:
        break;
    }
}

static int stick_from_axis(Sint16 v)
{
    /* N64 sticks report roughly -80..80 */
    float f = (float)v / 32767.0f;
    if (fabsf(f) < 0.15f)
        return 0;
    return (int)(f * 80.0f);
}

int IN_ReadPad(void)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    uint16_t buttons = 0;
    int sx = 0, sy = 0;
    size_t i;

    for (i = 0; i < sizeof(keybinds) / sizeof(keybinds[0]); i++)
        if (keys[keybinds[i].key])
            buttons |= keybinds[i].button;
    buttons |= mouse_buttons;

    /* number keys / wheel: weapon cycling pulses */
    if (wheel_pulse_up > 0) { buttons |= CONT_B; wheel_pulse_up--; }
    if (wheel_pulse_down > 0) { buttons |= CONT_A; wheel_pulse_down--; }

    if (gamepad)
    {
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) buttons |= CONT_A;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST)) buttons |= CONT_B;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST)) buttons |= CONT_F;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH)) buttons |= CONT_E;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START)) buttons |= CONT_START;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK)) buttons |= CONT_E;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) buttons |= CONT_L;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) buttons |= CONT_R;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK)) buttons |= CONT_C;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) buttons |= CONT_UP;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) buttons |= CONT_DOWN;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) buttons |= CONT_LEFT;
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) buttons |= CONT_RIGHT;
        if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000) buttons |= CONT_G;
        sx = stick_from_axis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX));
        sy = -stick_from_axis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));
        /* right stick turns like a mouse */
        {
            float rx = (float)SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
            float ry = (float)SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
            if (fabsf(rx) > 0.15f)
                mouse_dx_accum += rx * 18.0f;
            if (fabsf(ry) > 0.15f)
                mouse_dy_accum += ry * 10.0f;
        }
    }

    return (int)(((uint32_t)buttons << 16) | ((uint32_t)(uint8_t)(int8_t)sx << 8) | (uint32_t)(uint8_t)(int8_t)sy);
}

int I_PCMouseTurn(void)
{
    /* ~2048 counts per full turn at sensitivity 1.0 */
    float turn = -mouse_dx_accum * pc_config.mouse_sens * 2097152.0f;
    mouse_dx_accum = 0.0f;
    if (turn > 2.0e9f) turn = 2.0e9f;
    if (turn < -2.0e9f) turn = -2.0e9f;
    return (int)turn;
}

/* Vertical look since the last call, as a BAM pitch delta (up = positive). */
int I_PCMousePitch(void)
{
    extern int I_PCTestLook(void);
    float d = -mouse_dy_accum * pc_config.mouse_sens * 2097152.0f;
    mouse_dy_accum = 0.0f;
    if (pc_config.invert_mouse)
        d = -d;
    d += (float)I_PCTestLook();
    if (d > 2.0e9f) d = 2.0e9f;
    if (d < -2.0e9f) d = -2.0e9f;
    return (int)d;
}

/* PC-only actions held right now (PCACT_*). */
int I_PCActions(void)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    SDL_MouseButtonFlags mb = SDL_GetMouseState(NULL, NULL);
    extern int I_PCTestActions(void);
    int a = I_PCTestActions();

    if (keys[SDL_SCANCODE_SPACE])
        a |= PCACT_JUMP;
    if (keys[SDL_SCANCODE_F]) a |= 8; /* native flashlight toggle edge */
    if (input_grab && (mb & SDL_BUTTON_RMASK))
        a |= PCACT_ADS;
    if (keys[SDL_SCANCODE_V] || (input_grab && (mb & SDL_BUTTON_X1MASK)))
        a |= PCACT_KICK;
    if (gamepad)
    {
        if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK))
            a |= PCACT_JUMP;
        if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000)
            a |= PCACT_ADS;
    }
    return a;
}

/* Number key pressed since the last call (1..9), or 0. */
int I_PCWeaponKey(void)
{
    int k = weapon_key;
    weapon_key = 0;
    return k;
}

/* What I_PCMouseTurn/I_PCMousePitch would return now, without consuming
 * it (frame interpolation draws the view ahead of the game tic). */
int I_PCPeekMouseTurn(void)
{
    float turn = -mouse_dx_accum * pc_config.mouse_sens * 2097152.0f;
    if (turn > 2.0e9f) turn = 2.0e9f;
    if (turn < -2.0e9f) turn = -2.0e9f;
    return (int)turn;
}

int I_PCPeekMousePitch(void)
{
    float d = -mouse_dy_accum * pc_config.mouse_sens * 2097152.0f;
    if (pc_config.invert_mouse)
        d = -d;
    if (d > 2.0e9f) d = 2.0e9f;
    if (d < -2.0e9f) d = -2.0e9f;
    return (int)d;
}
