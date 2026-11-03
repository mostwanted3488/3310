#pragma once
// Pixel art for the BOBAL phone LCD: a 5x7 font and the animated emojis.
// Ported 1:1 from the HTML preview so the plugin and the preview look the same.
// No JUCE dependency.

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace n3310art
{
// ------------------------------------------------------------------ 5x7 font
// 7 rows, each row 5 bits (bit 4 = leftmost pixel)
inline const unsigned char* glyph (char c)
{
    static const unsigned char blank[7] = { 0, 0, 0, 0, 0, 0, 0 };
    static const unsigned char digits[10][7] = {
        { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
        { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E },
        { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
        { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
        { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }
    };
    static const unsigned char letters[26][7] = {
        { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // A
        { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, // B
        { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, // C
        { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, // D
        { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, // E
        { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, // F
        { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, // G
        { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // H
        { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, // I
        { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, // J
        { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, // K
        { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, // L
        { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, // M
        { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, // N
        { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // O
        { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, // P
        { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, // Q
        { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, // R
        { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, // S
        { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, // T
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // U
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, // V
        { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }, // W
        { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, // X
        { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, // Y
        { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }  // Z
    };
    static const unsigned char percent[7] = { 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13 };
    if (c == '%') return percent;
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= 'a' && c <= 'z') return letters[c - 'a'];
    return blank;
}

inline int textWidth (const char* s)
{
    const int n = (int) std::strlen (s);
    return n > 0 ? n * 6 - 1 : 0;
}

// ------------------------------------------------------------- pixel canvas
constexpr int kW = 48, kH = 30; // emoji canvas

struct Canvas
{
    bool px[kH][kW];

    void clear() { std::memset (px, 0, sizeof (px)); }

    void P (double x, double y)
    {
        const int ix = (int) std::lround (x), iy = (int) std::lround (y);
        if (ix >= 0 && iy >= 0 && ix < kW && iy < kH)
            px[iy][ix] = true;
    }
    void R (int x, int y, int w, int h)
    {
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i)
                P (x + i, y + j);
    }
    void clr (int x, int y, int w, int h)
    {
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i)
            {
                const int ix = x + i, iy = y + j;
                if (ix >= 0 && iy >= 0 && ix < kW && iy < kH)
                    px[iy][ix] = false;
            }
    }
    void L (int x0, int y0, int x1, int y1)
    {
        const int dx = std::abs (x1 - x0), dy = -std::abs (y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int e = dx + dy;
        for (;;)
        {
            P (x0, y0);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * e;
            if (e2 >= dy) { e += dy; x0 += sx; }
            if (e2 <= dx) { e += dx; y0 += sy; }
        }
    }
    void C (int cx, int cy, int r, const std::function<bool (int, int)>& pred = {})
    {
        int x = r, y = 0, err = 1 - r;
        while (x >= y)
        {
            const int pts[8][2] = { { x, y }, { y, x }, { -y, x }, { -x, y }, { -x, -y }, { -y, -x }, { y, -x }, { x, -y } };
            for (auto& p : pts)
                if (! pred || pred (p[0], p[1]))
                    P (cx + p[0], cy + p[1]);
            ++y;
            if (err < 0) err += 2 * y + 1;
            else { --x; err += 2 * (y - x) + 1; }
        }
    }
    void D (int cx, int cy, int r)
    {
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                if ((double) (x * x + y * y) <= (double) (r * r) + r * 0.6)
                    P (cx + x, cy + y);
    }
    void face (int cx, int cy) { C (cx, cy, 11); C (cx, cy, 10); }
    void note (int x, int y) { D (x, y + 4, 2); L (x + 2, y + 4, x + 2, y); L (x + 2, y, x + 4, y + 1); }
    void heart (int x, int y, bool big)
    {
        static const char* bigH[6] = { "0110110", "1111111", "1111111", "0111110", "0011100", "0001000" };
        static const char* smallH[4] = { "01010", "11111", "01110", "00100" };
        const int rows = big ? 6 : 4;
        for (int j = 0; j < rows; ++j)
        {
            const char* row = big ? bigH[j] : smallH[j];
            for (int i = 0; row[i]; ++i)
                if (row[i] == '1')
                    P (x + i, y + j);
        }
    }
    void Zg (int x, int y, int s) { L (x, y, x + s, y); L (x + s, y, x, y + s); L (x, y + s, x + s, y + s); }
};

// Draws emoji number 1..9 (animation frame f) into c
inline void drawEmoji (Canvas& c, int mode, int f)
{
    c.clear();
    switch (mode)
    {
        case 1: // SING
        {
            const int m = f % 2;
            c.face (24, 16);
            c.L (17, 13, 20, 11); c.L (20, 11, 23, 13); c.L (25, 13, 28, 11); c.L (28, 11, 31, 13);
            c.D (24, 21, m ? 2 : 3);
            c.L (33, 18, 36, 17); c.L (33, 21, 37, 21); c.L (33, 24, 36, 25);
            const int t = f % 4;
            c.note (4 + t, 18 - t * 3);
            c.note (1 + (t + 2) % 4 * 2, 22 - ((t + 2) % 4) * 3);
            break;
        }
        case 2: // LAUGH
        {
            const int b = f % 2 ? 1 : 0;
            c.face (24, 16 + b);
            c.L (16, 12 + b, 20, 14 + b); c.L (16, 16 + b, 20, 14 + b);
            c.L (32, 12 + b, 28, 14 + b); c.L (32, 16 + b, 28, 14 + b);
            for (int y = 0; y < 5; ++y)
                c.R (18 + (int) std::floor (y * 0.6), 19 + y + b, 13 - (int) std::floor (y * 1.2), 1);
            if (f % 2) { c.P (12, 18); c.P (12, 20); c.P (36, 18); c.P (36, 20); }
            else { c.P (11, 19); c.P (37, 19); }
            break;
        }
        case 3: // LOVE
        {
            const bool m = f % 2;
            c.face (24, 17);
            c.heart (15, 11, m); c.heart (28, 11, m);
            c.L (19, 22, 21, 24); c.L (21, 24, 27, 24); c.L (27, 24, 29, 22);
            const int t = f % 4;
            c.heart (4 + t % 2 * 2, 22 - t * 5, false);
            c.heart (40 - t % 2 * 2, 16 - t * 4 + 8, false);
            break;
        }
        case 4: // SLEEP
        {
            c.face (21, 18);
            c.L (15, 15, 19, 15); c.L (24, 15, 28, 15);
            if (f % 2) c.D (21, 22, 1); else c.L (19, 22, 23, 22);
            const int t = f % 4;
            c.Zg (32, 18 - t, 3);
            if (t >= 1) c.Zg (37, 11 - t, 4);
            if (t >= 2) c.Zg (41, 4, 5);
            break;
        }
        case 5: // ANGRY
        {
            const int s = f % 2 ? 1 : -1;
            c.face (24 + s, 17);
            c.L (17 + s, 11, 22 + s, 13); c.L (31 + s, 11, 26 + s, 13);
            c.D (20 + s, 15, 1); c.D (28 + s, 15, 1);
            c.L (18 + s, 23, 20 + s, 21); c.L (20 + s, 21, 22 + s, 23); c.L (22 + s, 23, 24 + s, 21);
            c.L (24 + s, 21, 26 + s, 23); c.L (26 + s, 23, 28 + s, 21); c.L (28 + s, 21, 30 + s, 23);
            const int t = f % 3;
            c.C (6, 8, 1 + t); c.C (42, 8, 1 + (t + 1) % 3);
            break;
        }
        case 6: // COOL
        {
            const int b = f % 2 ? 1 : 0;
            c.face (24, 16 + b);
            c.R (14, 12 + b, 8, 4); c.R (26, 12 + b, 8, 4); c.R (22, 12 + b, 4, 1);
            c.L (14, 12 + b, 12, 11 + b); c.L (34, 12 + b, 36, 11 + b);
            c.clr (15 + (f % 4), 13 + b, 1, 1);
            c.L (18, 21 + b, 21, 23 + b); c.L (21, 23 + b, 27, 23 + b); c.L (27, 23 + b, 30, 21 + b);
            if (f % 4 == 2) { c.P (40, 4); c.P (40, 8); c.P (38, 6); c.P (42, 6); c.P (40, 6); }
            break;
        }
        case 7: // CRY
        {
            c.face (24, 16);
            c.L (16, 11, 20, 12); c.L (32, 11, 28, 12);
            c.D (19, 15, 1); c.D (29, 15, 1);
            c.L (19, 23, 21, 21); c.L (21, 21, 27, 21); c.L (27, 21, 29, 23);
            const int t = f % 4;
            c.R (18, 17 + t * 2, 1, 2); c.R (30, 17 + ((t + 2) % 4) * 2, 1, 2);
            if (t < 2) { c.P (17, 27); c.P (31, 27); }
            break;
        }
        case 8: // SKULL
        {
            const int j = f % 2;
            c.C (24, 13, 11); c.C (24, 13, 10);
            c.C (24, 13, 11, [] (int, int dy) { return dy > 2; });
            c.R (15, 13, 1, 8); c.R (33, 13, 1, 8);
            c.D (19, 13, 3); c.D (29, 13, 3);
            c.P (24, 18); c.P (23, 19); c.P (25, 19);
            c.R (17, 21 + j, 15, 6); c.clr (18, 22 + j, 13, 4);
            for (int x = 19; x <= 29; x += 2) c.L (x, 22 + j, x, 25 + j);
            c.L (18, 22 + j, 30, 22 + j);
            c.clr (18, 21, 13, 1);
            break;
        }
        case 9: // ROBOT
        {
            c.C (24, 3, 1); c.L (24, 4, 24, 7);
            if (f % 2) c.D (24, 2, 1);
            c.R (13, 7, 22, 1); c.R (13, 23, 22, 1); c.R (13, 7, 1, 17); c.R (34, 7, 1, 17);
            c.R (10, 12, 3, 6); c.R (35, 12, 3, 6);
            const bool blink = (f % 6 == 5);
            if (blink) { c.L (17, 14, 21, 14); c.L (27, 14, 31, 14); }
            else { c.R (17, 12, 4, 4); c.R (27, 12, 4, 4); }
            c.R (18, 19, 12, 1);
            for (int x = 18; x <= 30; x += 3) c.R (x, 19, 1, 3);
            c.R (18, 21, 12, 1);
            if (f % 2) c.P (40, 14);
            break;
        }
        default: break;
    }
}

} // namespace n3310art
