/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2016 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Thomas Punt <tpunt@php.net>                                  |
  +----------------------------------------------------------------------+
*/

#include <stdlib.h>
#include <string.h>

#include "src/pht_general.h"

pht_string_t *pht_str_new(char *s, int len)
{
    pht_string_t *pstr = malloc(sizeof(pht_string_t));

    PHT_STRL_P(pstr) = len;
    PHT_STRV_P(pstr) = malloc(len);
    memcpy(PHT_STRV_P(pstr), s, len);

    return pstr;
}

void pht_str_update(pht_string_t *str, char *s, int len)
{
    PHT_STRL_P(str) = len;
    PHT_STRV_P(str) = malloc(len);
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
