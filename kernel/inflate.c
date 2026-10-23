/* YoroOS - DEFLATE (RFC 1951) decoder used by the ZIP reader.
 * Compact canonical-Huffman implementation. */
#include "kernel.h"

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    const u8 *in;
    int inlen, incnt;
    u32 bitbuf;
    int bitcnt;
    u8 *out;
    int outcap, outcnt;
    int err;
} Inflate;

typedef struct {
    short count[MAXBITS + 1];
    short symbol[FIXLCODES];
} Huff;

static int bits(Inflate *s, int need) {
    u32 val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) { s->err = 1; return 0; }
        val |= (u32)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int construct(Huff *h, const short *length, int n) {
    int len, sym, left;
    short offs[MAXBITS + 1];
    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;              /* complete but empty */
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;               /* over-subscribed */
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; sym++)
        if (length[sym]) h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static int decode(Inflate *s, const Huff *h) {
    int len, code = 0, first = 0, index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);
        {
            int count = h->count[len];
            if (code - first < count) return h->symbol[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
    }
    return -1;
}

static const short len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const short len_ext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3,
                                  3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                    6145, 8193, 12289, 16385, 24577};
static const short dist_ext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
                                   8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int put(Inflate *s, u8 b) {
    if (s->outcnt >= s->outcap) { s->err = 2; return -1; }
    s->out[s->outcnt++] = b;
    return 0;
}

static int codes(Inflate *s, const Huff *lencode, const Huff *distcode) {
    int symbol;
    do {
        symbol = decode(s, lencode);
        if (symbol < 0) return -1;
        if (symbol < 256) {
            if (put(s, (u8)symbol) < 0) return -1;
        } else if (symbol > 256) {
            int len, dist;
            symbol -= 257;
            if (symbol >= 29) return -1;
            len = len_base[symbol] + bits(s, len_ext[symbol]);
            symbol = decode(s, distcode);
            if (symbol < 0 || symbol >= 30) return -1;
            dist = dist_base[symbol] + bits(s, dist_ext[symbol]);
            if (dist > s->outcnt) return -1;
            while (len--) {
                if (put(s, s->out[s->outcnt - dist]) < 0) return -1;
            }
        }
    } while (symbol != 256);
    return 0;
}

static int stored(Inflate *s) {
    int len, nlen;
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return -1;
    len = s->in[s->incnt] | (s->in[s->incnt + 1] << 8);
    nlen = s->in[s->incnt + 2] | (s->in[s->incnt + 3] << 8);
    s->incnt += 4;
    if (len != (~nlen & 0xFFFF)) return -1;
    while (len--) {
        if (s->incnt >= s->inlen) return -1;
        if (put(s, s->in[s->incnt++]) < 0) return -1;
    }
    return 0;
}

static int fixed_block(Inflate *s) {
    static Huff lencode, distcode;
    static int built = 0;
    if (!built) {
        short lengths[FIXLCODES];
        int i;
        for (i = 0; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < FIXLCODES; i++) lengths[i] = 8;
        construct(&lencode, lengths, FIXLCODES);
        for (i = 0; i < MAXDCODES; i++) lengths[i] = 5;
        construct(&distcode, lengths, MAXDCODES);
        built = 1;
    }
    return codes(s, &lencode, &distcode);
}

static int dynamic_block(Inflate *s) {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    Huff lencode, distcode;
    short lengths[MAXLCODES + MAXDCODES];
    int nlen, ndist, ncode, index, err;
    short lencnt[MAXBITS + 1];

    nlen = bits(s, 5) + 257;
    ndist = bits(s, 5) + 1;
    ncode = bits(s, 4) + 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES) return -1;

    for (index = 0; index < ncode; index++) lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;

    err = construct(&lencode, lengths, 19);
    if (err != 0) return -1;

    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode);
        if (symbol < 0) return -1;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            int len = 0, rep;
            if (symbol == 16) {
                if (index == 0) return -1;
                len = lengths[index - 1];
                rep = 3 + bits(s, 2);
            } else if (symbol == 17) rep = 3 + bits(s, 3);
            else rep = 11 + bits(s, 7);
            if (index + rep > nlen + ndist) return -1;
            while (rep--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -1;

    for (index = 0; index <= MAXBITS; index++) lencnt[index] = 0;
    err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen != lencnt[0] + lencnt[1])) return -1;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0) return -1;

    return codes(s, &lencode, &distcode);
}

/* returns the decompressed size, or -1 on error / overflow */
int inflate_raw(const u8 *in, int inlen, u8 *out, int outcap) {
    Inflate s;
    int last, type;
    s.in = in; s.inlen = inlen; s.incnt = 0;
    s.bitbuf = 0; s.bitcnt = 0;
    s.out = out; s.outcap = outcap; s.outcnt = 0; s.err = 0;

    do {
        last = bits(&s, 1);
        type = bits(&s, 2);
        if (s.err) return -1;
        if (type == 0) { if (stored(&s) < 0) return -1; }
        else if (type == 1) { if (fixed_block(&s) < 0) return -1; }
        else if (type == 2) { if (dynamic_block(&s) < 0) return -1; }
        else return -1;
        if (s.err) return -1;
    } while (!last);
    return s.outcnt;
}
