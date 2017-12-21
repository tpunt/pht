#include <stdlib.h>
#include <string.h>

#include "pht_general.h"

void string_update(pht_string_t *str, char *s, int len)
{
    PHT_STRL_P(str) = len;
    PHT_STRV_P(str) = malloc(sizeof(char) * len);
    memcpy(PHT_STRV_P(str), s, len);
}
