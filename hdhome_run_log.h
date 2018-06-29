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

#ifndef _HDHOME_RUN_LOG_H_
#define _HDHOME_RUN_LOG_H_

#define LOG_LEVEL 1
#define LOGLN(_level, _args) \
  do \
  { \
    if (_level < LOG_LEVEL) \
    { \
        hdhome_run_log _args ; \
    } \
  } \
  while (0)

#define LOGF "%s:%d "
#define LOGP __FUNCTION__, __LINE__

int
hdhome_run_log(int log_level, const char* format, ...);

#endif

