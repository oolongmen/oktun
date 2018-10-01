#ifndef OKTUN_UTILS_H
#define OKTUN_UTILS_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "oktun.h"

OKTUN_BEGIN_NAMESPACE

namespace Utils
{
    static void HexDump(const char *data, size_t len)
    {
        DLOG("");

        char tmp[17];

        size_t paddedLen = len + ((~(len - 1)) & 0xf);

        bzero(tmp, sizeof(tmp));

        printf("%08zx  ", (size_t) 0);

        for (size_t n = 0; n < paddedLen; ++n)
        {
            if (n && !(n % 16))
            {
                printf(" %s\n%08zx  ", tmp, n);
                tmp[0] = '\0';
            }

            if (n < len)
            {
                tmp[n % 16] = 
                    (data[n] >= ' ' && data[n] <= '~') 
                    ? data[n] : '.';
                printf("%02x ", (uint8_t) data[n]);
            }
            else
            {
                tmp[n % 16] = '\0';
                printf("   ");
            }
        }

        if (tmp[0] != '\0')
            printf(" %s", tmp);

        printf("\n");
    }
}

OKTUN_END_NAMESPACE

#endif
