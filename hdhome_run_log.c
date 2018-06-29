/**
 * hdhome_run_player
 *
 * Copyright 2018 Jay Sorg <jay.sorg@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/*****************************************************************************/
int
hdhome_run_log(int log_level, const char* format, ...)
{
    va_list ap;
    char* buf;

    buf = (char*)malloc(1024);
    va_start(ap, format);
    vsnprintf(buf, 1024, format, ap);
    printf("%s\n", buf);
    va_end(ap);
    free(buf);
    return 0;
}

