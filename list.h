/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2004-2014
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
 *
 * simple list
 */

#if !defined(LIST_H)
#define LIST_H

/* list */
struct list
{
    long* items;
    int count;
    int alloc_size;
    int grow_by;
    int auto_free;
};

struct list*
list_create(void);
void
list_delete(struct list* self);
void
list_add_item(struct list* self, long item);
long
list_get_item(struct list* self, int index);
void
list_clear(struct list* self);
int
list_index_of(struct list* self, long item);
void
list_remove_item(struct list* self, int index);
void
list_insert_item(struct list* self, int index, long item);

#endif
