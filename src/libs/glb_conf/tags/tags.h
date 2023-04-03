/*
** Copyright Glaber 2018-2023
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#ifndef GLB_TAGS_H
#define GLB_TAGS_H

#include "zbxalgo.h"

typedef struct {
    const char *tag;
    const char *value;
} tag_t;

typedef struct tags_t tags_t;

tags_t*     tags_create_ext(mem_funcs_t *memf); 
tags_t*     tags_create(void);
void        tags_clean_ext(tags_t *set, mem_funcs_t *memf);
void        tags_clean(tags_t *set);
void        tags_destroy_ext(tags_t *set, mem_funcs_t *memf);
void        tags_destroy(tags_t *set);

int         tags_check_name(tags_t *t_set, const char *name, unsigned char oper);
int         tags_check_value(tags_t *t_set, tag_t *check_tag, unsigned char oper);

int     tags_add_tag(tags_t *t_set, tag_t *tag);
int     tags_add_tag_ext(tags_t *t_set, tag_t *tag, mem_funcs_t *memf);
void    tags_add_tags(tags_t *t_set_dst, tags_t *t_set_src);
void    tags_add_tags_ext(tags_t *t_set_dst, tags_t *t_set_src, mem_funcs_t *memf);
void    tags_reserve(tags_t *t_set, int count);

int tags_del_tags_ext(tags_t *t_set, tag_t *tag, mem_funcs_t *memf);
int tags_del_tags_by_tag_ext(tags_t *t_set, char *search_tag, mem_funcs_t *memf);

int tags_search(tags_t *t_set, tag_t *tag);
int tags_search_by_tag(tags_t *t_set, char *search_tag);


#endif