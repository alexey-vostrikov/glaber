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

#include "tags.h"
#include "zbxalgo.h"
#include "zbxcommon.h"
#include "zbxexpr.h"

static mem_funcs_t heap_memf = { .malloc_func = zbx_default_mem_malloc_func, 
              .free_func = zbx_default_mem_free_func, .realloc_func = zbx_default_mem_realloc_func};

struct tags_t {
    zbx_vector_ptr_t tags;
    strpool_t strpool;
};

/*create the new tags set, memf version*/
tags_t *tags_create_ext(mem_funcs_t *memf) {
    tags_t *t_set = memf->malloc_func(NULL, sizeof(tags_t));
    zbx_vector_ptr_create_ext(&t_set->tags, memf->malloc_func, memf->realloc_func, memf->free_func);
    strpool_init(&t_set->strpool, memf);
    return t_set;
}

/*create the new tags set, heap version*/
tags_t *tags_create(void) {
    mem_funcs_t heap_memf = {.malloc_func = ZBX_DEFAULT_MEM_MALLOC_FUNC, .realloc_func = ZBX_DEFAULT_MEM_REALLOC_FUNC, .free_func = ZBX_DEFAULT_MEM_FREE_FUNC};
    return tags_create_ext(&heap_memf); 
}

void tag_free_data_ext(tags_t *t_set, tag_t *tag, mem_funcs_t *memf) {
    LOG_INF("Free1");
    strpool_free(&t_set->strpool, tag->tag);
    LOG_INF("Free2");
    strpool_free(&t_set->strpool, tag->value);
    LOG_INF("Free3");
    memf->free_func(tag);
}

/*removes all tags from the set, but leaves the set initialized, memf version*/
void tags_clean_ext(tags_t *set, mem_funcs_t *memf) {
    int i; 
    for (int i = 0; i < set->tags.values_num; i++) {
        tag_free_data_ext(set, set->tags.values[i], memf);
    }
    zbx_vector_ptr_clear(&set->tags);
}

/*removes all tags from the set, but leaves the set initialized, heap version*/
void tags_clean(tags_t *set) {
    tags_clean_ext(set, &heap_memf);
}

/*fully releases all the memory associated with the set, memf version*/
void tags_destroy_ext(tags_t *t_set, mem_funcs_t *memf) {
    tags_clean_ext(t_set, memf);
    zbx_vector_ptr_destroy(&t_set->tags);
    strpool_destroy(&t_set->strpool);
    memf->free_func(t_set);
}

/*fully releases all the memory associated with the set, heap version*/
void tags_destroy(tags_t *set) {
    tags_destroy_ext(set, &heap_memf);    
}

/*searches for tags in the collection and returns if any matches the operation*/
int tags_check_name(tags_t *t_set, const char *name, unsigned char oper) {
    int j, ret_continue, ret;

    if (ZBX_CONDITION_OPERATOR_NOT_EQUAL == oper || ZBX_CONDITION_OPERATOR_NOT_LIKE == oper)
		ret_continue = SUCCEED;
	else
		ret_continue = FAIL;

    ret = ret_continue;
    
	for (j = 0; j < t_set->tags.values_num && ret == ret_continue; j++)
	{
		tag_t	*tag = t_set->tags.values[j];
		ret = zbx_strmatch_condition(tag->tag, name, oper);
	} 
    
    return ret;
}

int tags_check_value(tags_t *t_set, tag_t *check_tag, unsigned char oper) {
    int j, ret_continue, ret;

    if (ZBX_CONDITION_OPERATOR_NOT_EQUAL == oper || ZBX_CONDITION_OPERATOR_NOT_LIKE == oper)
		ret_continue = SUCCEED;
	else
		ret_continue = FAIL;

    ret = ret_continue;
    
	for (j = 0; j < t_set->tags.values_num && ret == ret_continue; j++)
	{
		tag_t	*tag = t_set->tags.values[j];
        if (0 == strcmp(check_tag->tag, tag->tag))
		    ret = zbx_strmatch_condition(tag->value, check_tag->value, oper);
	} 
    
    return ret;
}

/*note, the function expects both tags to be strpooled*/
static int	tags_cmp_func(const void *v1, const void *v2)
{
	const tag_t	*tag1 = *(const tag_t **)v1;
    const tag_t	*tag2 = *(const tag_t **)v2;
	
    if (tag1->tag != tag2->tag) 
        return strcmp(tag1->tag, tag2->tag);
	
    if (tag1->value == tag2->value)
        return 0;
	
    return strcmp(tag1->value, tag2->value);
}

/*note, the function expects both tags to be strpooled*/
static int	tags_name_cmp_func(const void *v1, const void *v2)
{
	const tag_t	*tag1 = *(const tag_t **)v1;
    const tag_t	*tag2 = *(const tag_t **)v2;
	
    

    if (tag1->tag != tag2->tag) 
        return strcmp(tag1->tag, tag2->tag);
	
    return 0;
}

/*check if tag exists, tag and value must be strpooled */
static int check_tag_exists(tags_t *t_set, tag_t *tag) {
    
    if (FAIL == zbx_vector_ptr_bsearch(&t_set->tags, tag, tags_cmp_func))
        return FAIL;
    
    return SUCCEED;
}

int tags_add_tag_ext(tags_t *t_set, tag_t *tag, mem_funcs_t *memf) {   

    if (NULL == tag)
        return FAIL;

    tag_t *new_tag = memf->malloc_func(NULL, sizeof(tag_t));
    new_tag->tag = (char *)strpool_add(&t_set->strpool, tag->tag);
    new_tag->value = (char *)strpool_add(&t_set->strpool, tag->value);

    if (FAIL == check_tag_exists(t_set, new_tag)) {
        zbx_vector_ptr_append(&t_set->tags, new_tag);
        zbx_vector_ptr_sort(&t_set->tags, tags_cmp_func);
        return SUCCEED;
    }

    tag_free_data_ext(t_set, new_tag, memf);

    return FAIL;
}

int tags_add_tag(tags_t *t_set, tag_t *tag) {
    return tags_add_tag_ext(t_set, tag, &heap_memf);
}

int tags_del_tags_by_tag_ext(tags_t *t_set, char *search_tag, mem_funcs_t *memf) {
    int idx, i = 0;
    tag_t s_tag = {.tag = (char *)strpool_add(&t_set->strpool, search_tag), .value = NULL};

    while ( FAIL != (idx = zbx_vector_ptr_bsearch(&t_set->tags, &s_tag, tags_name_cmp_func))) {
        tag_t *f_tag = t_set->tags.values[idx];
        LOG_INF("Found tag name %s => %s", f_tag->tag, f_tag->value);
        tag_free_data_ext(t_set, f_tag, memf);
        zbx_vector_ptr_remove(&t_set->tags, idx);
        i++;
    }
    
    strpool_free(&t_set->strpool, s_tag.tag);
    
    if (i > 0) 
        return SUCCEED;
    
    return FAIL;
}

int tags_del_tags_ext(tags_t *t_set, tag_t *tag, mem_funcs_t *memf) {
    int idx, i = 0;
    tag_t del_tag = {.tag = (char *)strpool_add(&t_set->strpool, tag->tag), 
                     .value = (char *)strpool_add(&t_set->strpool, tag->value)};

    while ( FAIL != (idx = zbx_vector_ptr_bsearch(&t_set->tags, &del_tag, tags_cmp_func))) {
        tag_t *f_tag = t_set->tags.values[idx];
        LOG_INF("Found tag name %s => %s", f_tag->tag, f_tag->value);
        tag_free_data_ext(t_set, f_tag, memf);
        zbx_vector_ptr_remove(&t_set->tags, idx);
        i++;
    }
    
    strpool_free(&t_set->strpool, del_tag.tag);
    strpool_free(&t_set->strpool, del_tag.value);

    if (i > 0) 
        return SUCCEED;
    
    return FAIL;
}

int tags_search_by_tag(tags_t *t_set, char *search_tag) {
    int idx;
    tag_t s_tag = {.tag = (char *)strpool_add(&t_set->strpool, search_tag), .value = NULL};
    
    idx = zbx_vector_ptr_bsearch(&t_set->tags, &s_tag, tags_name_cmp_func);
    strpool_free(&t_set->strpool, s_tag.tag);
    
    if (FAIL == idx)
        return FAIL;
    
    return SUCCEED;
}

int tags_search(tags_t *t_set, tag_t *tag) {
    int idx;
    tag_t s_tag = {.tag = (char *)strpool_add(&t_set->strpool, tag->tag), 
                     .value = (char *)strpool_add(&t_set->strpool, tag->value)};
    
    idx = zbx_vector_ptr_bsearch(&t_set->tags, &s_tag, tags_cmp_func);
    
    strpool_free(&t_set->strpool, s_tag.tag);
    strpool_free(&t_set->strpool, s_tag.value);
    
    if (FAIL == idx)
        return FAIL;
    
    return SUCCEED;
}


void tags_add_tags_ext(tags_t *t_set_dst, tags_t *t_set_src, mem_funcs_t *memf) {
    int i; 
    
    tags_reserve(t_set_dst,  t_set_src->tags.values_num);

    for (i = 0; i < t_set_src->tags.values_num; i++ ) {
        tag_t* s_tag = t_set_src->tags.values[i];
        tags_add_tag_ext(&t_set_dst->tags, s_tag, memf);
    }
}

void tags_add_tags(tags_t *t_set_dst, tags_t *t_set_src) {
    return tags_add_tags_ext(t_set_dst, t_set_src, &heap_memf);
}


void  tags_reserve(tags_t *t_set, int count) {
    zbx_vector_ptr_reserve(&t_set->tags, count);
}