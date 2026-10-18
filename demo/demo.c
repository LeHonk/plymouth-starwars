/* demo.c - a galaxy far, far away: starfield, receding logo, opening crawl
 *
 * Sequence:
 *   0 s   the boot mode ("BOOT", "SHUTDOWN", ...) appears as a huge
 *         outlined yellow logo and recedes into the centre of the screen
 *   3.5 s the opening crawl starts: an episode title, then every boot
 *         message as it arrives, tilted back and scrolling towards the
 *         vanishing point
 * All the while stars stream out from the centre, past a sliver of a blue
 * planet in the lower right corner. A keypress jumps to hyperspace.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "demo.h"

#define YELLOW       RGB (255, 232, 31)

#define NSTARS       320

#define LOGO_SUB     6          /* logo mask subpixels per font pixel */
#define LOGO_EDGE    2          /* outline thickness, in subpixels */
#define LOGO_START   1.5        /* initial width, in screen widths */
#define LOGO_SPEED   2.5        /* recession speed (size = 1 / (1 + speed * t)) */
#define LOGO_FADE0   5.0        /* fade out between these times */
#define LOGO_FADE1   8.0

#define CRAWL_START  3.5        /* seconds */
#define CRAWL_SPEED  14.0       /* text pixels per second, before catching up */
#define CRAWL_CHARS  36         /* width of the crawl column */
#define CRAWL_WRAP   32         /* boot messages wrap at this many characters */
#define CRAWL_LINES  256        /* ring buffer of crawl lines */
#define LINE_H       10         /* text pixels per line (8 font + 2 spacing) */
#define TEX_W        (CRAWL_CHARS * 8)

/* Crawl plane, in text pixels: the bottom screen row looks at the text
 * Z_NEAR away; text fades out between Z_FADE and Z_FAR */
#define Z_NEAR       200.0
#define Z_FADE       550.0
#define Z_FAR        1000.0
#define NEAR_SCALE   1.15       /* screen pixels per text pixel at the bottom */

typedef struct
{
        float x, y, z;
} star_t;

typedef struct
{
        char   text[CRAWL_CHARS + 1];
        int    scale;           /* 1, or 2 for the episode title */
        double pos;             /* crawl distance at which the line's top reaches the bottom edge */
} line_t;

typedef struct
{
        int       w, h;
        uint32_t  rng;
        double    last_t;

        star_t    stars[NSTARS];
        double    warp;         /* seconds of hyperspace left */

        uint8_t  *logo;         /* 0 = clear, 1 = letter inside, 2 = outline */
        int       logo_w, logo_h;

        line_t    lines[CRAWL_LINES];
        int       first, count; /* ring buffer window */
        double    scroll;
        char      last_text[256];
        char      last_status[256], last_message[256];

        /* planet sprite: out = bg * (255 - cover) / 255 + colour */
        int       px0, py0, pw, ph;
        uint32_t *planet;
        uint8_t  *planet_cover;
} state_t;

static const char *
mode_word (demo_mode_t mode)
{
        switch (mode) {
        case DEMO_MODE_SHUTDOWN: return "SHUTDOWN";
        case DEMO_MODE_REBOOT:   return "REBOOT";
        case DEMO_MODE_UPDATES:  return "UPDATE";
        default:                 return "BOOT";
        }
}

static void
mode_episode (demo_mode_t mode, const char **episode, const char **title)
{
        switch (mode) {
        case DEMO_MODE_SHUTDOWN: *episode = "Episode VIII"; *title = "THE LAST SHUTDOWN"; break;
        case DEMO_MODE_REBOOT:   *episode = "Episode VI";   *title = "RETURN OF THE BOOT"; break;
        case DEMO_MODE_UPDATES:  *episode = "Episode I";    *title = "THE PHANTOM UPDATE"; break;
        default:                 *episode = "Episode IV";   *title = "A NEW BOOT"; break;
        }
}

static float
frand (state_t *s)
{
        return (drand (&s->rng) & 0xffffff) / (float) 0x1000000;
}

/* ---- starfield ---------------------------------------------------------- */

static void
star_reset (state_t *s, star_t *st, bool anywhere)
{
        st->x = frand (s) * 2 - 1;
        st->y = frand (s) * 2 - 1;
        st->z = anywhere ? 0.05f + frand (s) * 0.95f : 1.0f;
}

static void
draw_stars (state_t *s, surface_t *fb, double dt)
{
        float cx = s->w / 2.0f, cy = s->h / 2.0f, f = s->w * 0.5f;
        float speed = 0.12f + (s->warp > 0 ? 2.5f * (float) (s->warp < 0.3 ? s->warp / 0.3 : 1.0) : 0);

        for (int i = 0; i < NSTARS; i++) {
                star_t *st = &s->stars[i];
                float ox = cx + st->x / st->z * f, oy = cy + st->y / st->z * f;

                st->z -= speed * (float) dt;
                if (st->z < 0.02f) {
                        star_reset (s, st, false);
                        continue;
                }
                float sx = cx + st->x / st->z * f, sy = cy + st->y / st->z * f;
                if (sx < 0 || sy < 0 || sx >= s->w || sy >= s->h) {
                        star_reset (s, st, false);
                        continue;
                }
                int b = (int) ((1.0f - st->z) * 255);
                uint32_t c = RGB (b, b, b > 40 ? b : 40);
                if (s->warp > 0) {
                        gfx_line (fb, (int) ox, (int) oy, (int) sx, (int) sy, RGB (b / 2, b / 2, b));
                        gfx_pset (fb, (int) sx, (int) sy, c);
                } else if (st->z < 0.3f) {
                        gfx_rect (fb, (int) sx, (int) sy, 2, 2, c);
                } else {
                        gfx_pset (fb, (int) sx, (int) sy, c);
                }
        }
}

/* ---- planet ------------------------------------------------------------- */

static float
hash2 (int x, int y)
{
        uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return ((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

static float
vnoise (float x, float y)
{
        int ix = (int) floorf (x), iy = (int) floorf (y);
        float fx = x - ix, fy = y - iy;
        fx = fx * fx * (3 - 2 * fx);
        fy = fy * fy * (3 - 2 * fy);
        float a = hash2 (ix, iy), b = hash2 (ix + 1, iy);
        float c = hash2 (ix, iy + 1), d = hash2 (ix + 1, iy + 1);
        return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static float
fbm (float x, float y)
{
        float v = 0, amp = 0.5f;
        for (int o = 0; o < 5; o++, x *= 2.03f, y *= 2.03f, amp *= 0.5f)
                v += vnoise (x, y) * amp;
        return v;
}

static uint32_t
rgbf (float r, float g, float b)
{
        r = r < 0 ? 0 : r > 255 ? 255 : r;
        g = g < 0 ? 0 : g > 255 ? 255 : g;
        b = b < 0 ? 0 : b > 255 ? 255 : b;
        return RGB ((int) r, (int) g, (int) b);
}

/* Render the planet once: a sphere centred below and right of the screen
 * so only its sunlit upper-left limb shows, with oceans, cloud bands and a
 * thin glowing atmosphere. 4x4 supersampled for a smooth edge. */
static void
make_planet (state_t *s)
{
        float R = s->h * 1.03f, pcx = s->w * 1.19f, pcy = s->h * 1.75f;
        float atmo = R * 0.035f;
        float lx = -0.62f, ly = -0.66f, lz = 0.42f;

        s->px0 = (int) (s->w * 0.70f);
        s->py0 = (int) (s->h * 0.70f);
        s->pw = s->w - s->px0;
        s->ph = s->h - s->py0;
        s->planet = calloc ((size_t) s->pw * s->ph, sizeof(uint32_t));
        s->planet_cover = calloc ((size_t) s->pw * s->ph, 1);

        for (int y = 0; y < s->ph; y++) {
                for (int x = 0; x < s->pw; x++) {
                        float r = 0, g = 0, b = 0, cover = 0;
                        for (int sy = 0; sy < 4; sy++) {
                                for (int sx = 0; sx < 4; sx++) {
                                        float dx = s->px0 + x + (sx + 0.5f) / 4 - pcx;
                                        float dy = s->py0 + y + (sy + 0.5f) / 4 - pcy;
                                        float d = sqrtf (dx * dx + dy * dy);
                                        if (d > R + atmo)
                                                continue;
                                        if (d > R) {
                                                /* atmosphere halo, added on top of the sky */
                                                float a = 1 - (d - R) / atmo;
                                                a *= a * 0.6f;
                                                r += 60 * a; g += 140 * a; b += 255 * a;
                                                continue;
                                        }
                                        float nx = dx / R, ny = dy / R, nz = sqrtf (1 - nx * nx - ny * ny);
                                        float lit = nx * lx + ny * ly + nz * lz;
                                        lit = lit < 0 ? 0 : lit;
                                        /* surface texture wraps on the sphere */
                                        float lon = atan2f (nx, nz) * 3.0f, lat = asinf (ny) * 3.0f;
                                        float land = fbm (lon * 1.3f + 7.0f, lat * 1.3f);
                                        float cloud = fbm (lon * 2.0f + 30.0f, lat * 6.0f);
                                        float cr, cg, cb;
                                        if (land > 0.58f) {
                                                cr = 40; cg = 90; cb = 140;     /* shallows / islands, still bluish */
                                        } else {
                                                float k = land / 0.58f;
                                                cr = 8 + 20 * k; cg = 30 + 60 * k; cb = 90 + 110 * k;
                                        }
                                        cloud = (cloud - 0.5f) * 3.0f;
                                        cloud = cloud < 0 ? 0 : cloud > 1 ? 1 : cloud;
                                        cr += (230 - cr) * cloud; cg += (240 - cg) * cloud; cb += (255 - cb) * cloud;
                                        /* light, then haze towards the limb */
                                        float limb = powf (1 - nz, 3.0f);
                                        float shade = 0.06f + lit * 1.1f;
                                        cr = cr * shade + 70 * limb * lit * 2;
                                        cg = cg * shade + 150 * limb * lit * 2;
                                        cb = cb * shade + 255 * limb * lit * 2;
                                        r += cr; g += cg; b += cb;
                                        cover += 1;
                                }
                        }
                        s->planet[y * s->pw + x] = rgbf (r / 16, g / 16, b / 16);
                        s->planet_cover[y * s->pw + x] = (uint8_t) (cover * 255 / 16);
                }
        }
}

static void
draw_planet (state_t *s, surface_t *fb)
{
        for (int y = 0; y < s->ph; y++) {
                uint32_t *row = fb->px + (s->py0 + y) * fb->pitch + s->px0;
                const uint32_t *col = s->planet + y * s->pw;
                const uint8_t *cov = s->planet_cover + y * s->pw;
                for (int x = 0; x < s->pw; x++) {
                        if (cov[x] == 0 && (col[x] & 0xffffff) == 0)
                                continue;
                        uint32_t bg = gfx_mix (row[x], RGB (0, 0, 0), cov[x]);
                        row[x] = gfx_add (bg, col[x]);
                }
        }
}

/* ---- logo --------------------------------------------------------------- */

static void
make_logo (state_t *s, const char *word)
{
        int n = (int) strlen (word);
        int w = n * 8 * LOGO_SUB, h = 8 * LOGO_SUB;
        uint8_t *in = calloc ((size_t) w * h, 1);

        for (int i = 0; i < n; i++) {
                const uint8_t *g = font8x8_glyph (word[i]);
                for (int y = 0; y < h; y++)
                        for (int x = 0; x < 8 * LOGO_SUB; x++)
                                if (g[y / LOGO_SUB] & (0x80 >> (x / LOGO_SUB)))
                                        in[y * w + i * 8 * LOGO_SUB + x] = 1;
        }

        /* letter pixels near the edge of a stroke become the yellow outline */
        s->logo = calloc ((size_t) w * h, 1);
        for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                        if (!in[y * w + x])
                                continue;
                        int edge = 0;
                        for (int dy = -LOGO_EDGE; dy <= LOGO_EDGE && !edge; dy++)
                                for (int dx = -LOGO_EDGE; dx <= LOGO_EDGE && !edge; dx++) {
                                        int xx = x + dx, yy = y + dy;
                                        if (xx < 0 || yy < 0 || xx >= w || yy >= h || !in[yy * w + xx])
                                                edge = 1;
                                }
                        s->logo[y * w + x] = edge ? 2 : 1;
                }
        }
        free (in);
        s->logo_w = w;
        s->logo_h = h;
}

static void
draw_logo (state_t *s, surface_t *fb, double t)
{
        if (t >= LOGO_FADE1)
                return;
        double size = LOGO_START / (1 + LOGO_SPEED * t);
        double scale = size * s->w / s->logo_w;  /* screen pixels per mask pixel */
        double x0 = s->w / 2.0 - s->logo_w * scale / 2, y0 = s->h / 2.0 - s->logo_h * scale / 2;
        int fade = t < LOGO_FADE0 ? 255 : (int) (255 * (LOGO_FADE1 - t) / (LOGO_FADE1 - LOGO_FADE0));
        int n = (int) ceil (1 / scale);          /* samples per axis */
        n = n < 2 ? 2 : n > 4 ? 4 : n;

        int xa = (int) floor (x0), xb = (int) ceil (x0 + s->logo_w * scale);
        int ya = (int) floor (y0), yb = (int) ceil (y0 + s->logo_h * scale);
        xa = xa < 0 ? 0 : xa; ya = ya < 0 ? 0 : ya;
        xb = xb > s->w ? s->w : xb; yb = yb > s->h ? s->h : yb;

        for (int y = ya; y < yb; y++) {
                for (int x = xa; x < xb; x++) {
                        int inside = 0, outline = 0;
                        for (int sy = 0; sy < n; sy++) {
                                int my = (int) ((y + (sy + 0.5) / n - y0) / scale);
                                if (my < 0 || my >= s->logo_h)
                                        continue;
                                for (int sx = 0; sx < n; sx++) {
                                        int mx = (int) ((x + (sx + 0.5) / n - x0) / scale);
                                        if (mx < 0 || mx >= s->logo_w)
                                                continue;
                                        uint8_t m = s->logo[my * s->logo_w + mx];
                                        inside += m == 1;
                                        outline += m == 2;
                                }
                        }
                        if (inside + outline == 0)
                                continue;
                        uint32_t *p = &fb->px[y * fb->pitch + x];
                        int nn = n * n;
                        /* the letters are hollow but opaque: black inside, yellow edge */
                        uint32_t c = gfx_mix (*p, RGB (0, 0, 0), (inside + outline) * fade / nn);
                        *p = gfx_add (c, gfx_mix (RGB (0, 0, 0), YELLOW, outline * fade / nn));
                }
        }
}

/* ---- crawl -------------------------------------------------------------- */

static line_t *
line_at (state_t *s, int i)
{
        return &s->lines[(s->first + i) % CRAWL_LINES];
}

static void
crawl_add (state_t *s, const char *text, int scale)
{
        double pos = s->scroll + 2;     /* never pop up in the middle of the screen */
        if (s->count > 0) {
                line_t *last = line_at (s, s->count - 1);
                double next = last->pos + LINE_H * last->scale;
                pos = next > pos ? next : pos;
        }
        if (s->count == CRAWL_LINES) {
                s->first = (s->first + 1) % CRAWL_LINES;
                s->count--;
        }
        line_t *l = line_at (s, s->count++);
        strncpy (l->text, text, CRAWL_CHARS);
        l->text[CRAWL_CHARS] = '\0';
        l->scale = scale;
        l->pos = pos;
}

/* Word-wrap a boot message into crawl lines */
static void
crawl_message (state_t *s, const char *msg)
{
        while (*msg == ' ')
                msg++;
        if (*msg == '\0' || strcmp (msg, s->last_text) == 0)
                return;
        strncpy (s->last_text, msg, sizeof(s->last_text) - 1);

        while (*msg) {
                int len = (int) strlen (msg), cut = len;
                if (len > CRAWL_WRAP) {
                        cut = CRAWL_WRAP;
                        while (cut > 0 && msg[cut] != ' ')
                                cut--;
                        if (cut == 0)
                                cut = CRAWL_WRAP;
                }
                char line[CRAWL_WRAP + 1];
                memcpy (line, msg, cut);
                line[cut] = '\0';
                crawl_add (s, line, 1);
                msg += cut;
                while (*msg == ' ')
                        msg++;
        }
        crawl_add (s, "", 1);   /* paragraph gap */
}

/* Pick up new status/message text from the boot */
static void
crawl_poll (state_t *s, const demo_ctx_t *ctx)
{
        if (ctx->message && strcmp (ctx->message, s->last_message) != 0) {
                strncpy (s->last_message, ctx->message, sizeof(s->last_message) - 1);
                crawl_message (s, ctx->message);
        }
        if (ctx->status && strcmp (ctx->status, s->last_status) != 0) {
                strncpy (s->last_status, ctx->status, sizeof(s->last_status) - 1);
                crawl_message (s, ctx->status);
        }
}

/* Is the text pixel at (u, q) set? u runs across the column, q along the
 * crawl (larger q = further down the text). */
static int
crawl_texel (const line_t *l, double u, double q)
{
        int row = (int) floor (q - l->pos) / l->scale;
        if (row < 0 || row >= 8)
                return 0;
        int len = (int) strlen (l->text);
        double ux = u - (TEX_W - len * 8 * l->scale) / 2.0;
        if (ux < 0)
                return 0;
        int col = (int) ux / l->scale;
        if (col >= len * 8)
                return 0;
        return (font8x8_glyph (l->text[col / 8])[row] >> (7 - col % 8)) & 1;
}

/* Find the line covering crawl position q (lines are sorted by pos) */
static const line_t *
crawl_find (state_t *s, double q)
{
        int lo = 0, hi = s->count - 1, best = -1;
        while (lo <= hi) {
                int mid = (lo + hi) / 2;
                if (line_at (s, mid)->pos <= q) {
                        best = mid;
                        lo = mid + 1;
                } else {
                        hi = mid - 1;
                }
        }
        if (best < 0)
                return NULL;
        const line_t *l = line_at (s, best);
        return q < l->pos + LINE_H * l->scale ? l : NULL;
}

static void
draw_crawl (state_t *s, surface_t *fb)
{
        double horizon = -s->h * 0.12;                  /* vanishing point, above the top edge */
        double K = (s->h - horizon) * Z_NEAR;           /* screen y = horizon + K / Z */
        double f = NEAR_SCALE * Z_NEAR;                 /* screen x = cx + X * f / Z */
        double cx = s->w / 2.0;

        if (s->count == 0)
                return;

        int ystart = (int) ceil (horizon + K / Z_FAR);
        for (int y = ystart; y < s->h; y++) {
                double Z = K / (y + 0.5 - horizon);
                double du = Z / f, dv = Z * Z / K;      /* footprint of one screen pixel */
                int nu = (int) ceil (du * 1.5), nv = (int) ceil (dv * 1.5);
                nu = nu < 2 ? 2 : nu > 4 ? 4 : nu;
                nv = nv < 2 ? 2 : nv > 4 ? 4 : nv;
                int fade = Z < Z_FADE ? 255 : (int) (255 * (Z_FAR - Z) / (Z_FAR - Z_FADE));

                const line_t *rows[4];
                double qs[4];
                int any = 0;
                for (int j = 0; j < nv; j++) {
                        double Zs = K / (y + (j + 0.5) / nv - horizon);
                        qs[j] = s->scroll - (Zs - Z_NEAR);
                        rows[j] = crawl_find (s, qs[j]);
                        any |= rows[j] != NULL;
                }
                if (!any)
                        continue;

                uint32_t *p = fb->px + y * fb->pitch;
                for (int x = 0; x < s->w; x++) {
                        int hits = 0;
                        for (int j = 0; j < nv; j++) {
                                if (!rows[j])
                                        continue;
                                for (int i = 0; i < nu; i++) {
                                        double u = (x + (i + 0.5) / nu - cx) * du + TEX_W / 2.0;
                                        hits += crawl_texel (rows[j], u, qs[j]);
                                }
                        }
                        if (hits)
                                p[x] = gfx_mix (p[x], YELLOW, hits * fade / (nu * nv));
                }
        }
}

/* ---- demo API ----------------------------------------------------------- */

void *
demo_init (const demo_ctx_t *ctx)
{
        state_t *s = calloc (1, sizeof(*s));
        const char *episode, *title;

        s->w = ctx->width;
        s->h = ctx->height;
        s->rng = 0x5ca1ab1eu;
        for (int i = 0; i < NSTARS; i++)
                star_reset (s, &s->stars[i], true);

        make_logo (s, mode_word (ctx->mode));
        make_planet (s);

        mode_episode (ctx->mode, &episode, &title);
        crawl_add (s, episode, 1);
        crawl_add (s, "", 1);
        crawl_add (s, title, strlen (title) * 16 <= TEX_W ? 2 : 1);
        crawl_add (s, "", 1);
        /* rewind so the first line enters at the bottom edge at CRAWL_START */
        s->scroll = -CRAWL_SPEED * CRAWL_START;
        return s;
}

void
demo_frame (void *state, const demo_ctx_t *ctx, surface_t *fb)
{
        state_t *s = state;
        double dt = ctx->t - s->last_t;

        dt = dt < 0 ? 0 : dt > 0.1 ? 0.1 : dt;
        s->last_t = ctx->t;
        if (s->warp > 0)
                s->warp -= dt;

        crawl_poll (s, ctx);

        /* scroll faster when messages pile up, so the crawl keeps pace with the boot */
        line_t *last = line_at (s, s->count - 1);
        double backlog = last->pos + LINE_H * last->scale - s->scroll;
        double speed = CRAWL_SPEED + (backlog > 60 ? (backlog - 60) * 0.5 : 0);
        if (ctx->t < CRAWL_START)
                speed = CRAWL_SPEED;
        s->scroll += speed * dt;

        gfx_clear (fb, RGB (0, 0, 0));
        draw_stars (s, fb, dt);
        draw_planet (s, fb);
        draw_logo (s, fb, ctx->t);
        draw_crawl (s, fb);
}

void
demo_key (void *state, const char *key)
{
        state_t *s = state;

        (void) key;
        s->warp = 1.0;          /* punch it */
}

void
demo_shutdown (void *state)
{
        state_t *s = state;

        free (s->logo);
        free (s->planet);
        free (s->planet_cover);
        free (s);
}
