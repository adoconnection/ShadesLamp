#include "api.h"

/*
 * Trading — a live candlestick chart wrapping around the lamp.
 *
 * A new candle is opened every Period (a multiple of 300 ms). While the period
 * runs, "trades" arrive at the Trade Rate (with random jitter), nudging the
 * price up or down: the body (open->current) grows and white markers track the
 * candle's high/low as new extremes are hit. When the period elapses the candle
 * is committed and a fresh one opens one slot to the right (wrapping). Older
 * candles fade, so the bright head chases its tail. Y=0 is the BOTTOM (low price).
 */

static const char META[] =
    "{\"name\":\"Trading\","
    "\"desc\":\"Live candlestick chart: trades shape each candle in real time\","
    "\"params\":["
        "{\"id\":0,\"name\":\"Period\",\"type\":\"int\","
         "\"min\":1,\"max\":20,\"default\":4,"
         "\"desc\":\"Candle period (x300ms)\"},"
        "{\"id\":1,\"name\":\"Trade Rate\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":18,"
         "\"desc\":\"How often trades arrive\"},"
        "{\"id\":2,\"name\":\"Brightness\",\"type\":\"int\","
         "\"min\":1,\"max\":255,\"default\":210,"
         "\"desc\":\"Overall brightness\"},"
        "{\"id\":3,\"name\":\"Volatility\",\"type\":\"int\","
         "\"min\":1,\"max\":100,\"default\":45,"
         "\"desc\":\"Size of each trade's move\"},"
        "{\"id\":4,\"name\":\"Width\",\"type\":\"int\","
         "\"min\":1,\"max\":5,\"default\":2,"
         "\"desc\":\"Candle width (pixels)\"},"
        "{\"id\":5,\"name\":\"Trail\",\"type\":\"int\","
         "\"min\":1,\"max\":10,\"default\":6,"
         "\"desc\":\"Visible candles (last 3 fade out)\"},"
        "{\"id\":6,\"name\":\"Palette\",\"type\":\"select\","
         "\"options\":[\"Classic\",\"Neon\",\"Mono\"],\"default\":0,"
         "\"desc\":\"Up/down colour scheme\"}"
    "]}";

EXPORT(get_meta_ptr)
int get_meta_ptr(void) { return (int)META; }
EXPORT(get_meta_len)
int get_meta_len(void) { return sizeof(META) - 1; }

/* ---- PRNG ---- */
static uint32_t rng = 1337;
static uint32_t rng_next(void) {
    uint32_t x = rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng = x; return x;
}
static float frand(void) { return (float)(rng_next() & 0xFFFF) / 65536.0f; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- State (one entry per candle slot) ---- */
#define MAX_SLOTS 64
static float s_open[MAX_SLOTS], s_high[MAX_SLOTS], s_low[MAX_SLOTS], s_close[MAX_SLOTS];
static int   s_used[MAX_SLOTS];

static float   price;
static int     head_slot;
static int     nslots;
static int     last_width;
static int32_t period_start, next_trade, prev_tick;
static int     started;

EXPORT(init)
void init(void) {
    rng = 1337;
    price = 0.5f;
    head_slot = 0;
    nslots = 0;
    last_width = -1;
    period_start = 0;
    next_trade = 0;
    prev_tick = 0;
    started = 0;
    for (int i = 0; i < MAX_SLOTS; i++) s_used[i] = 0;
}

/* open a fresh candle in `slot` at the current price */
static void open_candle(int slot) {
    s_open[slot] = price; s_close[slot] = price;
    s_high[slot] = price; s_low[slot]  = price;
    s_used[slot] = 1;
}

/* one trade nudges the price and stretches the head candle */
static void do_trade(int head, float step) {
    price += (0.5f - price) * 0.015f + (frand() - 0.5f) * 2.0f * step;
    price = clampf(price, 0.06f, 0.94f);
    s_close[head] = price;
    if (price > s_high[head]) s_high[head] = price;
    if (price < s_low[head])  s_low[head]  = price;
}

EXPORT(update)
void update(int tick_ms) {
    int period_p = get_param_i32(0);
    int rate     = get_param_i32(1);
    int bright   = get_param_i32(2);
    int vol      = get_param_i32(3);
    int width    = get_param_i32(4);
    int trail    = get_param_i32(5);
    int palette  = get_param_i32(6);

    int W = get_width();
    int H = get_height();
    if (W > MAX_SLOTS) W = MAX_SLOTS;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    if (period_p < 1) period_p = 1;
    if (rate < 1) rate = 1;
    if (width < 1) width = 1;
    if (width > 5) width = 5;
    if (trail < 1) trail = 1;
    if (trail > 10) trail = 10;

    rng ^= (uint32_t)tick_ms;

    int period_ms = period_p * 300;
    int slots = W / width;
    if (slots < 1) slots = 1;
    float step = 0.004f + (float)vol / 100.0f * 0.05f;   /* per-trade move */
    int trade_iv = 1000 / rate;                          /* avg ms between trades */
    if (trade_iv < 8) trade_iv = 8;

    int32_t delta = tick_ms - prev_tick;
    if (delta < 0 || delta > 250) delta = 33;
    prev_tick = tick_ms;

    /* (Re)build the ring on first frame or when the width (slot count) changes */
    if (!started || width != last_width) {
        for (int i = 0; i < MAX_SLOTS; i++) s_used[i] = 0;
        nslots = slots;
        for (int i = 0; i < nslots; i++) {
            price = clampf(price + (frand() - 0.5f) * 0.18f, 0.1f, 0.9f);
            open_candle(i);
            s_close[i] = price;
            s_high[i] = clampf(price + frand() * 0.08f, 0.0f, 1.0f);
            s_low[i]  = clampf(price - frand() * 0.08f, 0.0f, 1.0f);
        }
        head_slot = nslots - 1;
        period_start = tick_ms;
        next_trade = tick_ms;
        last_width = width;
        started = 1;
    }

    /* trades arrive (with jitter) and shape the current head candle */
    int guard = 0;
    while (tick_ms >= next_trade && guard < 200) {
        do_trade(head_slot, step);
        next_trade += (int)((float)trade_iv * (0.35f + frand() * 1.3f));
        guard++;
    }

    /* commit candle(s) when the period elapses */
    guard = 0;
    while (tick_ms - period_start >= period_ms && guard < nslots) {
        head_slot = (head_slot + 1) % nslots;
        open_candle(head_slot);
        period_start += period_ms;
        guard++;
    }

    /* brightness is a function of how many candles back from the head:
     * the newest are full, only the last few (tail) fade out.
     * Cap visible candles to nslots-1 so there is ALWAYS at least one empty
     * slot between the head candle and the tail candle. */
    int maxvis = nslots - 1; if (maxvis < 1) maxvis = 1;
    int vis = trail < maxvis ? trail : maxvis;
    if (vis < 1) vis = 1;
    int fade_n = vis < 3 ? vis : 3;              /* the tail 3 fade */
    int full_n = vis - fade_n;

    /* progress through the current period (0..1) — used to fade the tail
     * smoothly over the period instead of dropping a candle abruptly */
    float pp = (float)(tick_ms - period_start) / (float)period_ms;
    if (pp < 0.0f) pp = 0.0f; if (pp > 0.999f) pp = 0.999f;

    /* palette */
    int upR, upG, upB, dnR, dnG, dnB;
    switch (palette) {
        case 1: upR=0;  upG=230;upB=210; dnR=240;dnG=30; dnB=150; break;  /* Neon */
        case 2: upR=230;upG=235;upB=245; dnR=95; dnG=105;dnB=125; break;  /* Mono */
        default:upR=30; upG=225;upB=70;  dnR=235;dnG=45; dnB=45;  break;  /* Classic */
    }

    /* render */
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++)
            set_pixel(x, y, 0, 0, 0);

    int bodyW = width;                    /* candles fill their full width (no gap) */

    for (int s = 0; s < nslots; s++) {
        if (!s_used[s]) continue;

        /* age: 0 = head (newest), grows toward the tail */
        int age = (head_slot - s + nslots) % nslots;
        if (age >= vis) continue;          /* beyond the visible tail -> dark */
        /* continuous age = age + period progress, so the oldest candle fades
         * smoothly to zero over its last period instead of vanishing at once */
        float ca = (float)age + pp;
        float bf;
        if (age == 0)                 bf = 1.0f;   /* forming head: always full */
        else if (ca <= (float)full_n) bf = 1.0f;   /* leading candles full */
        else if (ca >= (float)vis)    bf = 0.0f;
        else bf = 1.0f - (ca - (float)full_n) / (float)fade_n;
        if (bf <= 0.01f) continue;

        int x0 = s * width;
        int cxw = x0 + bodyW / 2;          /* wick column (centre) */

        int up = s_close[s] >= s_open[s];
        int bR = up ? upR : dnR, bG = up ? upG : dnG, bB = up ? upB : dnB;
        float scale = bf * (float)bright / 255.0f;

        int hi_y = (int)(s_high[s] * (float)(H - 1) + 0.5f);
        int lo_y = (int)(s_low[s]  * (float)(H - 1) + 0.5f);
        int o_y  = (int)(s_open[s] * (float)(H - 1) + 0.5f);
        int cl_y = (int)(s_close[s]* (float)(H - 1) + 0.5f);
        int bt_y = o_y < cl_y ? o_y : cl_y;
        int tp_y = o_y > cl_y ? o_y : cl_y;
        if (lo_y < 0) lo_y = 0; if (hi_y > H - 1) hi_y = H - 1;

        /* wick: thin dim line down the centre over the high-low range */
        if (cxw < W) for (int y = lo_y; y <= hi_y; y++) {
            set_pixel(cxw, y, (int)(bR*scale*0.35f), (int)(bG*scale*0.35f), (int)(bB*scale*0.35f));
        }
        /* body: filled across the candle width over the open-close range */
        for (int dx = 0; dx < bodyW; dx++) {
            int x = x0 + dx; if (x >= W) break;
            for (int y = bt_y; y <= tp_y; y++)
                set_pixel(x, y, (int)(bR*scale), (int)(bG*scale), (int)(bB*scale));
        }

        /* white high/low markers on every candle: on the head they move as
         * trades hit new extremes; on older candles they fade with the tail */
        int wv = (int)((float)bright * bf);
        if (wv > 0) for (int dx = 0; dx < bodyW; dx++) {
            int x = x0 + dx; if (x >= W) break;
            set_pixel(x, hi_y, wv, wv, wv);
            set_pixel(x, lo_y, wv, wv, wv);
        }
    }

    draw();
}
