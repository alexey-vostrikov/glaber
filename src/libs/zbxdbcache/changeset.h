/*
** Glaber
** Copyright (C) 2001-2389 Glaber JSC
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
#ifndef CHANGESET_H

#define CHANGESET_WORK_TABLE "changeset_work"
#define CHANGESET_AUTOLOAD_TIME 120

#define DB_CREATE 1
#define DB_UPDATE 2
#define DB_DELETE 3

#define OBJ_NONE        0
#define OBJ_HOSTS       12
#define OBJ_ITEMS       13
#define OBJ_TRIGGERS    14
#define OBJ_FUNCTIONS   15
#define OBJ_PREPROCS    16
#define OBJ_TRIGGERDEPS 17
#define OBJ_TRIGGERTAGS 18
#define OBJ_ITEMTAGS    19
#define OBJ_TEMPLATES   20
#define OBJ_PROTOTYPES	21

typedef struct {
	char *sql;
	size_t alloc;
	size_t offset;
} glb_changeset_t;

void    changeset_flush_tables();
void    changeset_prepare_work_table();
void    changeset_delete_work_table();

void	changeset_prepare(glb_changeset_t* cset);
void	changeset_add_to_cache(glb_changeset_t *cset, int obj_type, u_int64_t *obj_id, int change_type, int num);
void	changeset_flush(glb_changeset_t *cset);

int		changeset_get_recent_time();
#endif