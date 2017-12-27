#include <stdlib.h>
#include <string.h>

#include "pht_general.h"

pht_string_t *pht_string_new(char *s, int len)
{
    pht_string_t *pstr = malloc(sizeof(pht_string_t));

    PHT_STRL_P(pstr) = len;
    PHT_STRV_P(pstr) = malloc(sizeof(char) * len);
    memcpy(PHT_STRV_P(pstr), s, len);

    return pstr;
}

void pht_str_update(pht_string_t *str, char *s, int len)
{
    PHT_STRL_P(str) = len;
    PHT_STRV_P(str) = malloc(sizeof(char) * len);
    memcpy(PHT_STRV_P(str), s, len);
}

int pht_str_eq(pht_string_t *phtstr1, pht_string_t *phtstr2)
{
    return PHT_STRL_P(phtstr1) == PHT_STRL_P(phtstr2) && !strncmp(PHT_STRV_P(phtstr1), PHT_STRV_P(phtstr2), PHT_STRL_P(phtstr2));
}

void pht_str_free(pht_string_t *str)
{
    free(PHT_STRV_P(str));
}
