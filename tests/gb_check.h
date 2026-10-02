#ifndef GB_CHECK_H
#define GB_CHECK_H

#include <stdio.h>

/* Always evaluates. A plain assert() disappears when a build defines NDEBUG. */
#define GB_REQUIRE(condition)                                            \
    do                                                                   \
    {                                                                    \
        if (!(condition))                                                \
        {                                                                \
            fprintf(stderr, "%s:%d: requirement failed: %s\n", __FILE__, \
                    __LINE__, #condition);                               \
            return 1;                                                    \
        }                                                                \
    } while (0)

#endif
