#include "tiny_font.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "matrix_display.hpp"

namespace {
// Five rows, bit 2 = left pixel, bit 0 = right pixel.
const uint8_t* glyph(char ch) {
    static const uint8_t blank[5] = {0,0,0,0,0};
#define G(name,a,b,c,d,e) static const uint8_t name[5] = {a,b,c,d,e}
    G(A,2,5,7,5,5); G(B,6,5,6,5,6); G(C,3,4,4,4,3); G(D,6,5,5,5,6);
    G(E,7,4,6,4,7); G(F,7,4,6,4,4); G(G,3,4,5,5,3); G(H,5,5,7,5,5);
    G(I,7,2,2,2,7); G(J,1,1,1,5,2); G(K,5,5,6,5,5); G(L,4,4,4,4,7);
    G(M,5,7,7,5,5); G(N,5,7,7,7,5); G(O,2,5,5,5,2); G(P,6,5,6,4,4);
    G(Q,2,5,5,7,3); G(R,6,5,6,5,5); G(S,3,4,2,1,6); G(T,7,2,2,2,2);
    G(U,5,5,5,5,7); G(V,5,5,5,5,2); G(W,5,5,7,7,5); G(X,5,5,2,5,5);
    G(Y,5,5,2,2,2); G(Z,7,1,2,4,7);
    // Compact 3x5 lowercase glyphs for the weather-description row.
    G(a,0,3,5,5,3); G(b,4,6,5,5,6); G(c,0,3,4,4,3); G(d,1,3,5,5,3);
    G(e,0,2,5,6,3); G(f,1,2,7,2,2); G(g,0,3,5,3,1); G(h,4,6,5,5,5);
    G(i,2,0,6,2,7); G(j,1,0,1,5,2); G(k,4,5,6,5,5); G(l,6,2,2,2,7);
    G(m,0,7,7,5,5); G(n,0,6,5,5,5); G(o,0,2,5,5,2); G(p,0,6,5,6,4);
    G(q,0,3,5,3,1); G(r,0,5,6,4,4); G(s,0,3,6,1,6); G(t,2,7,2,2,1);
    G(u,0,5,5,5,3); G(v,0,5,5,5,2); G(w,0,5,5,7,5); G(x,0,5,2,5,5);
    G(y,0,5,5,3,1); G(z,0,7,1,2,7);
    G(N0,7,5,5,5,7); G(N1,2,6,2,2,7); G(N2,6,1,7,4,7); G(N3,6,1,3,1,6);
    G(N4,5,5,7,1,1); G(N5,7,4,6,1,6); G(N6,3,4,7,5,7); G(N7,7,1,2,2,2);
    G(N8,7,5,7,5,7); G(N9,7,5,7,1,6);
    G(DOT,0,0,0,0,2); G(COLON,0,2,0,2,0); G(DASH,0,0,7,0,0); G(SLASH,1,1,2,4,4);
    G(PERCENT,5,1,2,4,5); G(PLUS,0,2,7,2,0); G(LPAREN,1,2,2,2,1); G(RPAREN,4,2,2,2,4);
    G(DEG,2,5,2,0,0); G(QUESTION,6,1,2,0,2); G(EQUALS,0,7,0,7,0); G(UNDERSCORE,0,0,0,0,7);
#undef G
    switch (ch) {
        case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D;
        case 'E': return E; case 'F': return F; case 'G': return G; case 'H': return H;
        case 'I': return I; case 'J': return J; case 'K': return K; case 'L': return L;
        case 'M': return M; case 'N': return N; case 'O': return O; case 'P': return P;
        case 'Q': return Q; case 'R': return R; case 'S': return S; case 'T': return T;
        case 'U': return U; case 'V': return V; case 'W': return W; case 'X': return X;
        case 'Y': return Y; case 'Z': return Z;
        case 'a': return a; case 'b': return b; case 'c': return c; case 'd': return d;
        case 'e': return e; case 'f': return f; case 'g': return g; case 'h': return h;
        case 'i': return i; case 'j': return j; case 'k': return k; case 'l': return l;
        case 'm': return m; case 'n': return n; case 'o': return o; case 'p': return p;
        case 'q': return q; case 'r': return r; case 's': return s; case 't': return t;
        case 'u': return u; case 'v': return v; case 'w': return w; case 'x': return x;
        case 'y': return y; case 'z': return z;
        case '0': return N0; case '1': return N1; case '2': return N2; case '3': return N3; case '4': return N4;
        case '5': return N5; case '6': return N6; case '7': return N7; case '8': return N8; case '9': return N9;
        case '.': return DOT; case ':': return COLON; case '-': return DASH; case '/': return SLASH;
        case '%': return PERCENT; case '+': return PLUS; case '(': return LPAREN; case ')': return RPAREN;
        case '*': return DEG; case '?': return QUESTION; case '=': return EQUALS; case '_': return UNDERSCORE;
        default: return blank;
    }
}

void append_ascii(std::string& out, const char* s) {
    while (*s) out.push_back(*s++);
}
}

int tiny_text_width(const char* text) {
    if (!text || !*text) return 0;
    return static_cast<int>(std::strlen(text)) * 4 - 1;
}

void tiny_draw_text(MatrixDisplay& display, int x, int y, const char* text, uint16_t color) {
    if (!text) return;
    for (const char* p = text; *p; ++p) {
        const uint8_t* g = glyph(*p);
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 3; ++col) {
                if (g[row] & (1U << (2 - col))) display.drawPixel(x + col, y + row, color);
            }
        }
        x += 4;
        if (x >= MatrixDisplay::WIDTH) break;
    }
}

void tiny_draw_text_centered(MatrixDisplay& display, int y, const char* text, uint16_t color) {
    const int w = tiny_text_width(text);
    const int x = std::max(0, (MatrixDisplay::WIDTH - w) / 2);
    tiny_draw_text(display, x, y, text, color);
}

std::string tiny_ascii_text(const char* utf8) {
    std::string out;
    if (!utf8) return out;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8);
    while (*p) {
        if (*p < 0x80) {
            char c = static_cast<char>(*p++);
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
            out.push_back(c);
            continue;
        }
        // German umlauts / sharp s (UTF-8 C3 xx), enough for OpenWeather DE text.
        if (p[0] == 0xC3 && p[1]) {
            switch (p[1]) {
                case 0x84: append_ascii(out, "AE"); break; // Ä
                case 0xA4: append_ascii(out, "ae"); break; // ä
                case 0x96: append_ascii(out, "OE"); break; // Ö
                case 0xB6: append_ascii(out, "oe"); break; // ö
                case 0x9C: append_ascii(out, "UE"); break; // Ü
                case 0xBC: append_ascii(out, "ue"); break; // ü
                case 0x9F: append_ascii(out, "ss"); break; // ß
                default: out.push_back('?'); break;
            }
            p += 2;
            continue;
        }
        // Skip the remaining bytes of an unsupported UTF-8 character.
        if ((*p & 0xE0) == 0xC0) p += 2;
        else if ((*p & 0xF0) == 0xE0) p += 3;
        else if ((*p & 0xF8) == 0xF0) p += 4;
        else ++p;
        out.push_back('?');
    }

    // Collapse repeated spaces and keep a small, display-friendly character set.
    std::string clean;
    bool last_space = false;
    for (char c : out) {
        const bool space = c == ' ';
        if (space && last_space) continue;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == ' ' || c == '.' || c == ':' || c == '-' || c == '/' ||
            c == '%' || c == '+' || c == '(' || c == ')' || c == '?' || c == '=') {
            clean.push_back(c);
            last_space = space;
        }
    }
    while (!clean.empty() && clean.front() == ' ') clean.erase(clean.begin());
    while (!clean.empty() && clean.back() == ' ') clean.pop_back();
    return clean;
}
