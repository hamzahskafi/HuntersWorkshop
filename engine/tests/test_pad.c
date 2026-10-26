#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include "../src/runtime_pad.c"

static int capture, menu;
// The runtime clock (src/runtime_host.c), not linked into this test.
uint64_t host_monotonic_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
int bbgpu_overlay_captures_input(void) { return capture; }
int bbgpu_pc_input(BbPcPad* p,const bool* keys,int n,int gamepad) {
    (void)p;(void)keys;(void)n;(void)gamepad;return 0;
}
int bbgpu_menu_buttons(uint32_t* confirm,uint32_t* back) { *confirm=BTN_CROSS; *back=BTN_CIRCLE; return menu; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

static void inject(const char *path, const char *tokens) {
    FILE *f=fopen(path,"w");
    assert(f);
    fputs(tokens,f);
    fclose(f);
    usleep(25000);
}

/* The movement keys' stick (key_move): W then W+A turns toward the corner at most 30 degrees a read
 * (15 at 60 reads a second), never 45 at once; a half turn (A -> D) goes through forward; a released
 * key keeps the direction 60 ms. */
static double turn_between(double a, double b) { return fabs(fmod(b-a+3.0*KEY_PI,2.0*KEY_PI)-KEY_PI); }
static void test_key_move(void) {
    int x, y;
    uint64_t t=1000000;
    assert(key_move(0,-1,t,&x,&y) && x==0 && y==-127); /* W: forward all the way */
    double last=atan2(y,x);
    for (int i=0;i<10;++i) {
        t+=16667;
        assert(key_move(-1,-1,t,&x,&y));
        const double now=atan2(y,x);
        assert(turn_between(last,now)<16.0*KEY_PI/180.0);
        last=now;
    }
    assert(x==-127 && y==-127); /* the square's corner: both axes all the way */
    t+=100000;
    assert(key_move(-1,0,t,&x,&y)); /* A */
    for (int i=0;i<20;++i) { t+=16667; assert(key_move(-1,0,t,&x,&y)); }
    assert(x==-127 && y==0);
    for (int i=0;i<20;++i) { /* D: round through forward, never backward */
        t+=16667;
        assert(key_move(1,0,t,&x,&y) && y<=0);
    }
    assert(x==127 && y==0);
    assert(key_move(0,0,t+30000,&x,&y) && x==127 && y==0); /* let go: the direction kept 60 ms */
    assert(!key_move(0,0,t+70000,&x,&y));
}
/* round_gate: a stick past the round gate is brought back onto it in the same direction; one axis
 * all the way, and anything within the gate, is left as it is. */
static void test_round_gate(void) {
    uint8_t x=255, y=255;
    round_gate(0,&x,&y);
    assert(x==218 && y==218); /* 128 + 127 / sqrt 2 */
    x=0; y=0;
    round_gate(0,&x,&y);
    assert(x==38 && y==38);
    x=0; y=128;
    round_gate(1,&x,&y);
    assert(x==0 && y==128);
    x=255; y=128;
    round_gate(1,&x,&y);
    assert(x==255 && y==128);
    x=200; y=60;
    round_gate(1,&x,&y);
    assert(x==200 && y==60);
}

int main(void) {
    test_key_move();
    test_round_gate();
    char path[]="/tmp/bbport-pad-test-XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    setenv("BB_PAD_FILE",path,1);
    /* config.ini controls: buttons moved, a trigger as a button and a button as a trigger. */
    char config[]="/tmp/bbport-pad-config-XXXXXX";
    int config_fd=mkstemp(config);
    assert(config_fd>=0);
    const char controls[]="upscaler=fsr3\npad.cross=b\npad.circle=a\npad.r2=rightshoulder\n"
                          "pad.r1=righttrigger\nkey.cross=X, Space\npad.bogus=a\npad.menu_back=b\n";
    assert(write(config_fd,controls,sizeof(controls)-1)==(ssize_t)(sizeof(controls)-1));
    close(config_fd);
    setenv("BB_CONFIG",config,1);
    setenv("SDL_VIDEODRIVER","dummy",1);
    /* Only the virtual test controller is a gamepad, whatever is plugged in. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);
    PadData data;
    inject(path,"cross l3 touchpad_left");
    assert(pad_read_state(1,&data)==0);
    assert((data.buttons & (BTN_CROSS|BTN_L3|BTN_TOUCHPAD))==(BTN_CROSS|BTN_L3|BTN_TOUCHPAD));
    /* A new touch gets a new id (1..127), as from a DualShock 4: the game ignores id 0. */
    assert(data.touch_count==1 && data.touches[0].x==480 && data.touches[0].y==471 && data.touches[0].id==1);
    inject(path,"touchpad_right");
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==1440);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.touch_count==0);

    SDL_VirtualJoystickTouchpadDesc touch={.nfingers=2};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.name="bbport test controller";
    desc.vendor_id=0x1d50;
    desc.product_id=0x6189;
    desc.ntouchpads=1;
    desc.touchpads=&touch;
    SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);
    assert(id!=0);
    SDL_Joystick *joystick=SDL_OpenJoystick(id);
    assert(joystick);
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,true,0.75f,0.5f,1.0f));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,true,0.25f,1.0f,1.0f));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,true));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0);
    assert(gamepad && data.touch_count==2 && (data.buttons & BTN_TOUCHPAD));
    assert(data.touches[0].x==1439 && data.touches[0].y==471 && data.touches[0].id==2);
    assert(data.touches[1].x==480 && data.touches[1].y==942 && data.touches[1].id==3);
    capture=1;
    assert(pad_read_state(1,&data)==0 && data.touch_count==0 && data.buttons==0);
    capture=0;
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,false,0,0,0));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,false,0,0,0));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==480);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,false));
    /* Everything let go of once: buttons held through the overlay's capture stay hidden until
     * then (hold_after_capture). */
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.buttons==0);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_EAST,true));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0);
    assert(data.buttons==(BTN_CROSS|BTN_R2|BTN_R1) && data.r2==255);
    /* pad.menu_back=b: in a menu, B goes back (Circle) and no longer gives its world input (Cross). */
    menu=1;
    assert(pad_read_state(1,&data)==0);
    assert((data.buttons & (BTN_CROSS|BTN_CIRCLE))==BTN_CIRCLE);
    menu=0;
    /* Still held as the menu closed: no Cross in the world until B is let go of. */
    assert(pad_read_state(1,&data)==0 && !(data.buttons & (BTN_CROSS|BTN_CIRCLE)));
    assert(bindings[IN_CROSS].key_count==2 && bindings[IN_CROSS].keys[0]==SDL_SCANCODE_X &&
           bindings[IN_CROSS].keys[1]==SDL_SCANCODE_SPACE);
    SDL_CloseJoystick(joystick);
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    SDL_Quit();
    unlink(path);
    unlink(config);
    puts("PASS: keyboard stick turns, round gate, pad ABI, debug camera chord, left/right clicks, SDL touch coordinates, overlay capture, controls, menu buttons");
}
