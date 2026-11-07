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
 *   MIDI in      a clock blinks PLAY on the beat; START / STOP light it
 *   SysEx in     (0.3, VIBE FM SDK 24, Protokoll 15c) F0 7D 56 4C 01, the page, then name and value of the VIBE's encoders
 *                1-4, A, B, DATA (each ended by 00), F7: the screen shows the page and what every knob is now. The VIBE,
 *                the Web Player and the plugin's window send it when it changes (a click on the host too). Without it
 *                the screen keeps the page last chosen here (EDIT ENV LFO ARP FX SEQ HOME).
 * What the VIBE makes of it is its controller profile (vibe-sdk/controllers/vibe-fm-link.json): the FM-1 shows up
 * as "VIBE FM Link", the profile matches that name. Every CC is plain MIDI: any DAW can learn it too.
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG. What platform.c offers: docs/API.md. */
#define APP_NAME "VIBE LINK"                    /* the rescue and the crash screen say this */
static void app_idle(void);
#define LCD_IDLE app_idle                       /* while the screen's DMA runs: MIDI in (platform.c) */
static void link_sysex(const unsigned char *b, unsigned int n);
#define APP_SYSEX link_sysex                    /* (0.3) the host's page and encoders: F0 7D 56 4C 01 … F7 (Protokoll 15c) */
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
static int strcmp_(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a != *b; }

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

/* ---- (0.3) the page and the knobs: from the host (SysEx), else the page last chosen here */
enum { L_E1 = 0, L_EA = 4, L_EB = 5, L_ED = 6 };   /* the VIBE's encoders as the host numbers them: 1-4, A, B, DATA */
static const uint8_t L_OF_ENC[7] = {L_ED, L_EA, L_E1, L_E1 + 1, L_E1 + 2, L_E1 + 3, L_EB};   /* the FM-1's SELECT ALGO KNOB1-4 PRESETS */
static char l_page[20], l_name[7][12], l_val[7][12], here_page[12];
static uint32_t l_on, l_ser, l_hot_t[7];
/* ---- (0.4) the host's picture: a converted FM-1 firmware's own 240 x 240 screen, as the VIBE / the Web Player / the VST
 *      run it. F0 7D 56 4C 02 i0 i1 i2 <tokens> F7 (Protokoll 15d): i = the first pixel, y * 240 + x, in 3 x 7 bits; then
 *      t 0..63 = t + 1 pixels of one colour, t 64..127 = t - 63 pixels one by one, a colour = RGB565 in 3 bytes (2+7+7 bits).
 *      Drawn row piece by row piece as it comes; every message answered F0 7D 56 4C 03 F7 once drawn (the host sends the
 *      next one then: the platform keeps one message at a time). No message for 3 s, or 00 / 01: the screen of its own. */
static uint32_t m_on, m_ms, m_nb, m_n;
static uint16_t m_row[2][240];                  /* (two: one goes out by DMA while the next one fills) */
static uint16_t m_col(const unsigned char *c)   /* (the panel takes a pixel's two bytes the other way round, as gfx.c's canvas keeps them) */
{ uint32_t v = (uint32_t)(c[0] & 3u) << 14 | (uint32_t)c[1] << 7 | c[2]; return (uint16_t)(v >> 8 | (v & 255u) << 8); }
static void m_rx(const unsigned char *b, unsigned int n)
{
    uint32_t i, k = 7, x0, x, y;
    if (n < 7u || (i = (uint32_t)b[4] << 14 | (uint32_t)b[5] << 7 | b[6]) >= 240u * 240u) return;
    y = i / 240u; x0 = x = i % 240u;
    while (k < n && y < 240u) {
        uint32_t t = b[k++], cnt = t < 64u ? t + 1u : t - 63u;
        uint16_t c = 0;
        if (t < 64u) { if (k + 3u > n) break; c = m_col(b + k); k += 3u; }
        for (; cnt && y < 240u; cnt--) {
            if (t >= 64u) { if (k + 3u > n) break; c = m_col(b + k); k += 3u; }
            m_row[m_nb][x++] = c;
            if (x == 240u) { lcd_blit(x0, y, 240u - x0, 1, m_row[m_nb] + x0); m_nb ^= 1u; x0 = x = 0; y++; }
        }
    }
    if (x > x0 && y < 240u) { lcd_blit(x0, y, x - x0, 1, m_row[m_nb] + x0); m_nb ^= 1u; }
}
static void m_ack(void)
{
#ifndef DINGHY_HOST
    static const uint8_t ACK[6] = {0xF0, 0x7D, 0x56, 0x4C, 0x03, 0xF7};
    ota_wire_send(ACK, sizeof ACK);
#endif
}

static void link_sysex(const unsigned char *b, unsigned int n)   /* (from platform_poll: the bytes between F0 and F7) */
{
    uint32_t i = 4, f, k;
    char *dst[15];
    if (n < 4u || b[1] != 0x56u || b[2] != 0x4Cu) return;
    if (b[3] == 0x02u) { if (!m_on) lcd_fill(0, 0, 240, 240, C_BLACK); m_on = 1; m_ms = fm1_ms; m_n++; m_rx(b, n); m_ack(); return; }   /* (0.4) */
    m_on = 0;
    if (b[3] == 0x00u) { l_on = 0; l_ser++; return; }  /* the host has nothing to show */
    if (b[3] != 0x01u) return;
    dst[0] = l_page;
    for (k = 0; k < 7u; k++) { dst[1 + 2 * k] = l_name[k]; dst[2 + 2 * k] = l_val[k]; }
    for (f = 0; f < 15u; f++) {
        uint32_t cap = f ? 12u : 20u, j = 0;
        while (i < n && b[i]) { if (j < cap - 1u) dst[f][j++] = b[i] >= 32u && b[i] < 127u ? (char)b[i] : '?'; i++; }
        dst[f][j] = 0;
        if (i < n) i++;                             /* (its 00) */
    }
    l_on = 1; l_ser++;
}
static void upper(char *d, const char *s, uint32_t cap)   /* FONT_L has no small letters */
{
    uint32_t j = 0;
    for (; s[j] && j < cap - 1u; j++) d[j] = s[j] >= 'a' && s[j] <= 'z' ? (char)(s[j] - 32) : s[j];
    d[j] = 0;
}
static void draw_title(const char *t, uint16_t c)  /* the page: big, else small when long */
{
    char u[20];
    upper(u, t, sizeof u);
    if (text_w(&FONT_L, u) <= 236) draw_text_box(0, 2, 240, &FONT_L, u, c, 1);
    else { cv_begin(240, 32, C_BLACK); cv_text((240 - text_w(&FONT_S, t)) / 2, 8, &FONT_S, t, c); cv_blit(0, 2); }
}
static void draw_cell(uint32_t x, uint32_t y, uint32_t e, uint32_t hot)   /* one knob: name small, value big */
{
    char u[12];
    const felucca_font_t *vf;
    uint16_t edge = hot ? C_ACC : RGB(50, 50, 60);
    cv_begin(118, 60, C_BLACK);
    cv_rect(0, 0, 118, 1, edge); cv_rect(0, 59, 118, 1, edge); cv_rect(0, 0, 1, 60, edge); cv_rect(117, 0, 1, 60, edge);
    if (l_name[e][0]) {
        cv_text(6, 4, &FONT_S, l_name[e], hot ? C_ACC : C_DIM);
        upper(u, l_val[e], sizeof u);
        vf = text_w(&FONT_L, u) <= 110 ? &FONT_L : &FONT_S;
        cv_text(vf == &FONT_L ? 6 : 6, vf == &FONT_L ? 24 : 32, vf, vf == &FONT_L ? u : l_val[e], C_WHITE);
    } else cv_text(6, 4, &FONT_S, "-", C_OFF);
    cv_blit(x, y);
}
static void draw_ab(uint32_t layer)             /* ALGORITHM and PRESETS (the host's A and B), or what a held button does */
{
    uint32_t r;
    cv_begin(240, 38, C_BLACK);
    if (layer) cv_text(4, 10, &FONT_S, layer == 1u ? "SHIFT: PLAY=STOP  OCT=< >" : "F KEYS: EDIT ENV LFO ARP FX SEQ", C_ACC);
    else for (r = 0; r < 2u; r++) {
        uint32_t e = r ? L_EB : L_EA;
        int32_t x = cv_text(4, (int32_t)r * 19, &FONT_S, r ? "B " : "A ", C_ACC);
        if (l_name[e][0]) { x = cv_text(x, (int32_t)r * 19, &FONT_S, l_name[e], C_DIM); cv_text(x + 8, (int32_t)r * 19, &FONT_S, l_val[e], C_WHITE); }
    }
    cv_blit(0, 159);
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
    uint32_t view_linked = 0, ser_drawn = ~0u, hot_drawn = ~0u;
    char here_drawn[12] = "";
    uint8_t held[27] = {0};
    if (settings_load(&set, sizeof set) || (int8_t)set.oct < -2 || (int8_t)set.oct > 2) set.oct = 0;

    uint32_t mirrored = 1;                      /* (0.4: the host's picture was on the screen – and at the start: draw it all) */

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
            if (pressed && (F_OF[k] || k == BTN_HOME)) {   /* (0.3) the page chosen here stays on the screen */
                const char *q = k == BTN_HOME ? "CONTROLS" : VIBE_NAME[k]; uint32_t j = 0;
                while (q[j] && j < sizeof here_page - 1u) { here_page[j] = q[j]; j++; } here_page[j] = 0;
            }
        }
        for (k = 0; k < 7u; k++) {              /* an endless encoder's acceleration (Ableton 7.11., Sven: "schwerfällig" with one step a
                                                   detent): a detent counts 1 when turned slowly, 2 / 3 / 6 the faster the last ones came
                                                   (under 90 / 45 / 25 ms apart); the detents of 20 ms go in one message */
            static int32_t enc_acc[7]; static uint32_t enc_t[7], enc_last[7]; int32_t d = fm1_enc_take(k);
            if (d) {
                uint32_t gap = fm1_ms - enc_last[k]; enc_last[k] = fm1_ms;
                enc_acc[k] += d * (gap < 25u ? 6 : gap < 45u ? 3 : gap < 90u ? 2 : 1);
            }
            if (!enc_acc[k] || fm1_ms - enc_t[k] < 20u) continue;
            e = enc_acc[k]; enc_acc[k] = 0; enc_t[k] = fm1_ms;
            {
                if (e > 63) e = 63; if (e < -63) e = -63;
                cc(ENC_CC[k], e > 0 ? (uint32_t)e : (uint32_t)(128 + e));
                l_hot_t[L_OF_ENC[k]] = fm1_ms + 1u;   /* (0.3) its box lit a moment (+ 1: never 0) */
                { char v[6]; int32_t a = e < 0 ? -e : e; v[0] = e < 0 ? '-' : '+'; v[1] = (char)('0' + a / 10); v[2] = (char)('0' + a % 10); v[3] = 0;
                  char s[16]; char *p = s; const char *q = ENC_VIBE[k]; while (*q) *p++ = *q++; *p++ = ' '; q = v + (a < 10 ? 1 : 0); *p++ = v[0]; while (*q) *p++ = *q++; *p = 0;
                  say2(ENC_NAME[k], s); }
            }
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
        if (m_on && (!usb_ok() || fm1_ms - m_ms > 3000u)) m_on = 0;   /* (0.4: the host's picture gone quiet) */
        if (m_on) { mirrored = 1; goto drawn; }   /* (the host's picture fills the screen) */
        if (mirrored) {                         /* (the start, or back from the host's picture: all of the own screen again) */
            mirrored = 0;
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_title("VIBE FM LINK", C_ACC);
            draw_text_box(0, 48, 240, &FONT_S, "the FM-1 as a controller  v" APP_VER, C_OFF, 1);
            draw_text_box(0, 66, 240, &FONT_S, "knobs, buttons, keys > USB-MIDI", C_DIM, 1);
            lit_drawn = ~0u; view_linked = 0; here_drawn[0] = 1; here_drawn[1] = 0; sent_drawn = ~0u; status_drawn = ~0u; layer_drawn = ~0u;
        }
        if (lit != lit_drawn) { lit_drawn = lit; draw_keys(lit); }
        if (l_on && usb_ok()) {                 /* (0.3) the host's page and knobs */
            uint32_t hot = 0;
            for (k = 0; k < 7u; k++) if (l_hot_t[k] && fm1_ms + 1u - l_hot_t[k] < 700u) hot |= 1u << k; else l_hot_t[k] = 0;
            if (!view_linked || l_ser != ser_drawn) {
                if (!view_linked) { lcd_fill(0, 0, 240, 196, C_BLACK); view_linked = 1; hot_drawn = ~0u; }
                ser_drawn = l_ser; draw_title(l_page[0] ? l_page : here_page[0] ? here_page : "VIBE FM LINK", C_ACC);
                hot_drawn = ~0u; layer_drawn = ~0u;
            }
            if (hot != hot_drawn) {
                for (k = 0; k < 4u; k++) if (hot_drawn == ~0u || ((hot ^ hot_drawn) >> k & 1u)) draw_cell(k % 2u ? 122u : 0u, k < 2u ? 36u : 97u, k, hot >> k & 1u);
                hot_drawn = hot;
            }
            if (layer != layer_drawn) { layer_drawn = layer; draw_ab(layer); }
        } else {
            if (view_linked) {                  /* (back to the screen of its own: the host is gone) */
                view_linked = 0; lcd_fill(0, 0, 240, 196, C_BLACK); here_drawn[0] = 1; sent_drawn = ~0u; status_drawn = ~0u; layer_drawn = ~0u;
            }
            if (here_page[0] && strcmp_(here_page, here_drawn)) {   /* (0.3) the page chosen here, big on top */
                uint32_t j = 0; while ((here_drawn[j] = here_page[j]) != 0) j++;
                draw_title(here_page, C_ACC);
            }
            if (sent_n != sent_drawn) { sent_drawn = sent_n; draw_last(); }
            if ((st = (uint32_t)(oct + 2) | (uint32_t)(usb_ok() ? 16u : 0u)) != status_drawn) { status_drawn = st; draw_status(oct, st & 16u); }
            if (layer != layer_drawn) { layer_drawn = layer; draw_layer(layer); }
        }
    drawn:
        idle_ok = 0;

        if (dirty && fm1_ms - t_change > 2000u) { settings_save(&set, sizeof set); dirty = 0; }
    }
}
