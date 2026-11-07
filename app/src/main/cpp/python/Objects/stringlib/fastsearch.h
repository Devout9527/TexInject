#ifndef STRINGLIB_FASTSEARCH_H
#define STRINGLIB_FASTSEARCH_H

#define FAST_COUNT 0
#define FAST_SEARCH 1
#define FAST_RSEARCH 2

#if LONG_BIT >= 128
#define STRINGLIB_BLOOM_WIDTH 128
#elif LONG_BIT >= 64
#define STRINGLIB_BLOOM_WIDTH 64
#elif LONG_BIT >= 32
#define STRINGLIB_BLOOM_WIDTH 32
#else
#error "LONG_BIT is smaller than 32"
#endif

#define STRINGLIB_BLOOM_ADD(mask, ch) \
    ((mask |= (1UL << ((ch) & (STRINGLIB_BLOOM_WIDTH -1)))))
#define STRINGLIB_BLOOM(mask, ch)     \
    ((mask &  (1UL << ((ch) & (STRINGLIB_BLOOM_WIDTH -1)))))

Py_LOCAL_INLINE(Py_ssize_t)
fastsearch(const STRINGLIB_CHAR* s, Py_ssize_t n,
           const STRINGLIB_CHAR* p, Py_ssize_t m,
           Py_ssize_t maxcount, int mode)
{
    unsigned long mask;
    Py_ssize_t skip, count = 0;
    Py_ssize_t i, j, mlast, w;

    w = n - m;

    if (w < 0 || (mode == FAST_COUNT && maxcount == 0))
        return -1;

    if (m <= 1) {
        if (m <= 0)
            return -1;

        if (mode == FAST_COUNT) {
            for (i = 0; i < n; i++)
                if (s[i] == p[0]) {
                    count++;
                    if (count == maxcount)
                        return maxcount;
                }
            return count;
        } else if (mode == FAST_SEARCH) {
            for (i = 0; i < n; i++)
                if (s[i] == p[0])
                    return i;
        } else {
            for (i = n - 1; i > -1; i--)
                if (s[i] == p[0])
                    return i;
        }
        return -1;
    }

    mlast = m - 1;
    skip = mlast - 1;
    mask = 0;

    if (mode != FAST_RSEARCH) {

        for (i = 0; i < mlast; i++) {
            STRINGLIB_BLOOM_ADD(mask, p[i]);
            if (p[i] == p[mlast])
                skip = mlast - i - 1;
        }

        STRINGLIB_BLOOM_ADD(mask, p[mlast]);

        for (i = 0; i <= w; i++) {

            if (s[i+m-1] == p[m-1]) {

                for (j = 0; j < mlast; j++)
                    if (s[i+j] != p[j])
                        break;
                if (j == mlast) {

                    if (mode != FAST_COUNT)
                        return i;
                    count++;
                    if (count == maxcount)
                        return maxcount;
                    i = i + mlast;
                    continue;
                }

                if (!STRINGLIB_BLOOM(mask, s[i+m]))
                    i = i + m;
                else
                    i = i + skip;
            } else {

                if (!STRINGLIB_BLOOM(mask, s[i+m]))
                    i = i + m;
            }
        }
    } else {

        STRINGLIB_BLOOM_ADD(mask, p[0]);

        for (i = mlast; i > 0; i--) {
            STRINGLIB_BLOOM_ADD(mask, p[i]);
            if (p[i] == p[0])
                skip = i - 1;
        }

        for (i = w; i >= 0; i--) {
            if (s[i] == p[0]) {

                for (j = mlast; j > 0; j--)
                    if (s[i+j] != p[j])
                        break;
                if (j == 0)

                    return i;

                if (i > 0 && !STRINGLIB_BLOOM(mask, s[i-1]))
                    i = i - m;
                else
                    i = i - skip;
            } else {

                if (i > 0 && !STRINGLIB_BLOOM(mask, s[i-1]))
                    i = i - m;
            }
        }
    }

    if (mode != FAST_COUNT)
        return -1;
    return count;
}

#endif
