/* SPDX-License-Identifier: GPL-3.0-only */
/* VIBE FM Link: the M-VAVE FM-1 as a controller for the VIBE FM (and for anything else that takes MIDI).
 * Built on Dinghy (zvenson/dinghy): everything below this file (platform.c) is Dinghy's – updates, the USB rescue,
 * MIDI, the keys, knobs, LEDs, the screen. This firmware makes no sound of its own: the VIBE does.
 *
 *   keys         notes over USB-MIDI, channel 1 (OCT- / OCT+ shift them, kept in flash)
 *   KNOB 1-4     CC 20-23, relative (two's complement: +n = n, -n = 128 - n)   -> the VIBE's encoders 1-4
 *   SELECT       CC 24, relative                                               -> DATA
 *   ALGORITHM    CC 25, relative                                               -> encoder A
 *   PRESETS      CC 26, relative                                               -> encoder B
 *   MASTER       CC 7 (0..127)                                                 -> the VIBE's volume
 *   buttons      CC 40 + id, 127 pressed / 0 let go: FX 42, SEL 43, ENV 44, LFO 45, EDIT 46, GLO 47, HOME 48,
 *                SAVE 49, ARP 50, SEQ 51, PLAY 52, REC 53 (OCT- / OCT+ stay here: the octave)
 *   SAVE held    = SHIFT on the VIBE (CC 49). SAVE + PLAY = STOP (CC 54, instead of PLAY).
 *   GLO held     the F layer: GLO + EDIT ENV LFO ARP FX SEQ = F1..F6 (CC 60-65); GLO tapped alone = MENU (CC 47)
 *                SAVE + OCT- / OCT+ = LEFT / RIGHT (CC 56 / 57)
 *   MIDI in      a clock blinks PLAY on the beat; START / STOP light it (nothing else is used)
 * What the VIBE makes of it is its controller profile (vibe-sdk/controllers/vibe-fm-link.json): the FM-1 shows up
 * as "VIBE FM Link", the profile matches that name. Every CC is plain MIDI: any DAW can learn it too.
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG. What platform.c offers: docs/API.md. */
#define APP_NAME "VIBE LINK"                    /* the rescue and the crash screen say this */
static void app_idle(void);
#define LCD_IDLE app_idle                       /* while the screen's DMA runs: MIDI in (platform.c) */
#include "platform.c"

/* ---- the sound: none. The buffer stays silent (platform.c clears it). */
static void app_audio(int32_t *out, uint32_t n) { (void)out; (void)n; }

/* ---- what goes out */
#define CH 0u                                   /* channel 1 */
enum { CC_MASTER = 7, CC_KNOB1 = 20, CC_SELECT = 24, CC_ALGO = 25, CC_PRESETS = 26, CC_BTN0 = 40, CC_STOP = 54, CC_LEFT = 56, CC_RIGHT = 57, CC_F1 = 60 };
static const char *const BTN_NAME[14] = {"OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"};
static const char *const VIBE_NAME[14] = {"", "", "FX", "ENTER", "ENV", "LFO", "SOUND", "MENU", "HOME", "SHIFT", "ARP", "SEQ", "PLAY", "REC"};
static const uint8_t F_OF[14] = {0, 0, 5, 0, 2, 3, 1, 0, 0, 0, 4, 6, 0, 0};   /* GLO + button = F n (0: none) */
static const char *const ENC_NAME[7] = {"SELECT", "ALGO", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4", "PRESETS"};
static const char *const ENC_VIBE[7] = {"DATA", "ENC A", "ENC 1", "ENC 2", "ENC 3", "ENC 4", "ENC B"};
static const uint8_t ENC_CC[7] = {CC_SELECT, CC_ALGO, CC_KNOB1, CC_KNOB1 + 1, CC_KNOB1 + 2, CC_KNOB1 + 3, CC_PRESETS};

static char last[32] = "";                      /* the last thing sent, for the screen */
static uint32_t last_ms, sent_n;
static void say2(const char *a, const char *b)  /* "a  >  b" */
{
    char *p = last; const char *q = a;
    while (*q && p < last + 14) *p++ = *q++;
    *p++ = ' '; *p++ = '>'; *p++ = ' ';
    q = b; while (*q && p < last + 30) *p++ = *q++;
    *p = 0; last_ms = fm1_ms; sent_n++;
}
static void cc(uint32_t n, uint32_t v) { midi_send(0xB0u | CH, n, v & 127u); }

/* ---- the screen */
#define C_ACC RGB(255, 96, 190)                 /* the VIBE's pink */
#define C_KEY RGB(210, 210, 210)
#define C_OFF RGB(90, 90, 90)
#define C_DIM RGB(140, 140, 140)
#define KEY0 53u                                /* key 0 = F3 */
static int black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }

static void draw_keys(uint32_t lit)             /* bit k: key k is held */
{
    uint32_t k, w = 0;
    cv_begin(240, 40, C_BLACK);
    for (k = 0; k < 27u; k++)
        if (!black(KEY0 + k)) { cv_rect(8 + (int32_t)w * 14, 0, 13, 40, lit >> k & 1u ? C_ACC : C_KEY); w++; }
    for (k = 0, w = 0; k < 27u; k++)
        if (black(KEY0 + k)) cv_rect(8 + (int32_t)w * 14 - 5, 0, 9, 24, lit >> k & 1u ? C_ACC : C_BLACK);
        else w++;
    cv_blit(0, 196);
}
static void draw_last(void) { draw_text_box(0, 96, 240, &FONT_L, last[0] ? last : "-", C_WHITE, 1); }
static void draw_status(int32_t oct, uint32_t usb)
{
    char s[24] = "OCT +0   USB --";
    s[4] = oct < 0 ? '-' : '+'; s[5] = (char)('0' + (oct < 0 ? -oct : oct));
    if (usb) { s[13] = 'O'; s[14] = 'K'; }
    draw_text_box(0, 170, 240, &FONT_S, s, C_OFF, 1);
}
static void draw_layer(uint32_t layer)          /* what the held button turns the others into */
{
    draw_text_box(0, 136, 240, &FONT_S, layer == 1u ? "SHIFT:  PLAY = STOP   OCT = < >" : layer == 2u ? "F KEYS:  EDIT ENV LFO ARP FX SEQ" : "", C_ACC, 1);
}

/* ---- MIDI in: only the clock, to blink PLAY */
static uint32_t idle_ok, clk_n, clk_t0, clk_beat_ms, clk_run, clk_on;
static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2) != 0) {
        if (st == 0xF8u) { if (clk_n % 24u == 0u) { clk_t0 = fm1_ms; clk_beat_ms = fm1_ms; clk_on = 1; } clk_n++; }
        else if (st == 0xFAu) { clk_run = 1; clk_n = 0; }
        else if (st == 0xFBu) clk_run = 1;
        else if (st == 0xFCu) clk_run = 0;
    }
}
static void app_idle(void) { if (idle_ok) midi_in(); }
#ifdef DINGHY_HOST
static int usb_ok(void) { return 1; }
#else
static int usb_ok(void) { return usb.config != 0; }   /* (usb.c: SET_CONFIGURATION seen – a computer or the VIBE listens) */
#endif

static struct { uint8_t oct, pad[3]; } set = {0, {0, 0, 0}};   /* what survives a power-off */

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0, lit_drawn = ~0u, status_drawn = ~0u, layer_drawn = ~0u, sent_drawn = ~0u;
    uint32_t glo_used = 0, glo_t = 0, master_sent = 0xFFFFu;
    uint8_t held[27] = {0};
    if (settings_load(&set, sizeof set) || (int8_t)set.oct < -2 || (int8_t)set.oct > 2) set.oct = 0;

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 14, 240, &FONT_L, "VIBE FM LINK", C_ACC, 1);
    draw_text_box(0, 48, 240, &FONT_S, "the FM-1 as a controller  v" APP_VER, C_OFF, 1);
    draw_text_box(0, 66, 240, &FONT_S, "knobs, buttons, keys > USB-MIDI", C_DIM, 1);

    for (;;) {
        uint32_t keys, down, up, rel, b, st, layer, lit;
        int32_t oct = (int8_t)set.oct, e, adc;
        uint32_t base = (uint32_t)((int32_t)KEY0 + 12 * oct);
        platform_poll();

        keys = fm1_in.notes;
        down = keys & ~prev; up = prev & ~keys; prev = keys;
        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u) { held[k] = (uint8_t)(base + k); midi_send(0x90u | CH, held[k], 100); }
            if (up >> k & 1u) midi_send(0x80u | CH, held[k], 0);
        }

        layer = fm1_in.buttons >> BTN_SAVE & 1u ? 1u : fm1_in.buttons >> BTN_GLO & 1u ? 2u : 0u;
        b = fm1_input_edges(&rel);
        for (k = 0; k < 14u; k++) {
            uint32_t pressed = b >> k & 1u, released = rel >> k & 1u;
            if (!pressed && !released) continue;
            if (k == BTN_OCT_DOWN || k == BTN_OCT_UP) {
                if (layer == 1u) {              /* SHIFT + OCT = LEFT / RIGHT */
                    cc(k == BTN_OCT_DOWN ? CC_LEFT : CC_RIGHT, pressed ? 127 : 0);
                    if (pressed) say2(k == BTN_OCT_DOWN ? "SHIFT OCT-" : "SHIFT OCT+", k == BTN_OCT_DOWN ? "LEFT" : "RIGHT");
                } else if (pressed) {
                    if (k == BTN_OCT_DOWN && oct > -2) set.oct = (uint8_t)(oct - 1);
                    if (k == BTN_OCT_UP && oct < 2) set.oct = (uint8_t)(oct + 1);
                    dirty = 1; t_change = fm1_ms;
                }
                continue;
            }
            if (k == BTN_GLO) {                 /* held: the F layer; tapped alone: MENU */
                if (pressed) { glo_used = 0; glo_t = fm1_ms; }
                else if (!glo_used && fm1_ms - glo_t < 500u) { cc(CC_BTN0 + BTN_GLO, 127); cc(CC_BTN0 + BTN_GLO, 0); say2("GLO", "MENU"); }
                continue;
            }
            if (layer == 2u && F_OF[k]) {       /* GLO + a function button = F1..F6 */
                glo_used = 1;
                cc(CC_F1 + F_OF[k] - 1u, pressed ? 127 : 0);
                if (pressed) { char f[4] = "F1"; f[1] = (char)('0' + F_OF[k]); say2(BTN_NAME[k], f); }
                continue;
            }
            if (k == BTN_PLAY && layer == 1u) { /* SHIFT + PLAY = STOP */
                cc(CC_STOP, pressed ? 127 : 0);
                if (pressed) say2("SHIFT PLAY", "STOP");
                continue;
            }
            cc(CC_BTN0 + k, pressed ? 127 : 0);
            if (pressed) say2(BTN_NAME[k], VIBE_NAME[k]);
        }
        for (k = 0; k < 7u; k++)
            if ((e = fm1_enc_take(k)) != 0) {
                if (e > 63) e = 63; if (e < -63) e = -63;
                cc(ENC_CC[k], e > 0 ? (uint32_t)e : (uint32_t)(128 + e));
                { char v[6]; int32_t a = e < 0 ? -e : e; v[0] = e < 0 ? '-' : '+'; v[1] = (char)('0' + a / 10); v[2] = (char)('0' + a % 10); v[3] = 0;
                  char s[16]; char *p = s; const char *q = ENC_VIBE[k]; while (*q) *p++ = *q++; *p++ = ' '; q = v + (a < 10 ? 1 : 0); *p++ = v[0]; while (*q) *p++ = *q++; *p = 0;
                  say2(ENC_NAME[k], s); }
            }
#ifdef DINGHY_HOST
        adc = -1;                               /* (the host has no MASTER knob) */
#else
        adc = fm1_adc_read(FM1_ADC_MASTER);
#endif
        if (adc >= 0) {                         /* MASTER: 0..1023 -> 0..127 – sent once it has moved 2 steps (its ADC flickers by one:
                                                   55 / 56 / 55 … flooded the plugin, 7.11.); the ends (0, 127) always reach */
            uint32_t v = (uint32_t)adc >> 3, d = master_sent == 0xFFFFu ? 99u : v > master_sent ? v - master_sent : master_sent - v;
            if (master_sent == 0xFFFFu) master_sent = v;
            else if (d >= 2u || ((v == 0u || v == 127u) && d)) { cc(CC_MASTER, v); say2("MASTER", "VOLUME"); master_sent = v; }
        }

        midi_in();
        if (clk_on && fm1_ms - clk_t0 > 2000u) clk_on = 0, clk_run = 0;

        oct = (int8_t)set.oct;
        lit = keys;
        for (k = 0; k < 27u; k++) fm1_led_key(KEY_LED(k), lit >> k & 1u);
        for (k = 2; k < 14u; k++) fm1_led_key(k, k == BTN_PLAY ? (clk_run || (clk_on && fm1_ms - clk_beat_ms < 80u) || (fm1_in.buttons >> k & 1u)) : (fm1_in.buttons >> k & 1u));
        fm1_led_key(BTN_OCT_DOWN, oct < 0); fm1_led_key(BTN_OCT_UP, oct > 0);

        idle_ok = 1;
        if (lit != lit_drawn) { lit_drawn = lit; draw_keys(lit); }
        if (sent_n != sent_drawn) { sent_drawn = sent_n; draw_last(); }
        if ((st = (uint32_t)(oct + 2) | (uint32_t)(usb_ok() ? 16u : 0u)) != status_drawn) { status_drawn = st; draw_status(oct, st & 16u); }
        if (layer != layer_drawn) { layer_drawn = layer; draw_layer(layer); }
        idle_ok = 0;

        if (dirty && fm1_ms - t_change > 2000u) { settings_save(&set, sizeof set); dirty = 0; }
    }
}
