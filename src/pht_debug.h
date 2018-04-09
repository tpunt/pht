/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-present The PHP Group                             |
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

#ifndef PHT_DEBUG_H
#define PHT_DEBUG_H

#if 0
# define pthread_mutex_lock(mut) \
    printf("A: %s (%s:%d)\n", #mut, __FILE__, __LINE__); \
    pthread_mutex_lock(mut);

# define pthread_mutex_unlock(mut) \
    printf("R: %s (%s:%d)\n", #mut, __FILE__, __LINE__); \
    pthread_mutex_unlock(mut);
#endif

#endif
