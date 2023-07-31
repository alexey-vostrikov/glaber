/*
** GLaber
** Copyright (C) 2018-2023 
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

#include "zbxcommon.h"
#include "zbxdbhigh.h"
#include "dbupgrade.h"

#ifndef HAVE_SQLITE3

static int	GLB_DBpatch_3000001(void)
{
	const ZBX_TABLE table =
			{"hosts_depends", "depid", 0,
				{
					{"depid", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"hostid_up", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"hostid_down", NULL, NULL, NULL, 0, ZBX_TYPE_ID, ZBX_NOTNULL, 0},
					{"name", "", NULL, NULL, 128, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0},
					{0}
				},
				NULL
			};

	return DBcreate_table(&table);
}

static int	GLB_DBpatch_3000002(void)
{
	return DBcreate_index("hosts_depends", "hosts_depends_hostid_up_idx", "hostid_up", 0);
}

static int	GLB_DBpatch_3000003(void)
{
	return DBcreate_index("hosts_depends", "hosts_depends_hostid_down_idx", "hostid_down", 0);
}

static int	GLB_DBpatch_3000004(void)
{
	return DBcreate_index("hosts_depends", "hosts_depends_name_idx", "name", 0);
}

static int	GLB_DBpatch_3000005(void)
{
	const ZBX_FIELD	field = {"hostid_up", NULL, "hosts", "hostid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};
	return DBadd_foreign_key("hosts_depends", 1, &field);
}

static int	GLB_DBpatch_3000006(void)
{
	const ZBX_FIELD	field = {"hostid_down", NULL, "hosts", "hostid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};
	return DBadd_foreign_key("hosts_depends", 2, &field);
}

static int	GLB_DBpatch_3000007(void)
{
	return DBcreate_index("hosts_depends", "hosts_depends_up_down_name_idx", "hostid_up,hostid_down,name", 1);
}

static int      GLB_DBpatch_3000008(void)
{
        const ZBX_FIELD field = {"description", "", NULL, NULL, 2048, ZBX_TYPE_CHAR, ZBX_NOTNULL, 0};
        return DBmodify_field_type("triggers", &field, NULL);
}
#endif

GLB_DBPATCH_START(3000)

#ifndef HAVE_SQLITE3

GLB_DBPATCH_ADD(3000001, 0, 1)
GLB_DBPATCH_ADD(3000002, 0, 1)
GLB_DBPATCH_ADD(3000003, 0, 1)
GLB_DBPATCH_ADD(3000004, 0, 1)
GLB_DBPATCH_ADD(3000005, 0, 1)
GLB_DBPATCH_ADD(3000006, 0, 1)
GLB_DBPATCH_ADD(3000007, 0, 1)
GLB_DBPATCH_ADD(3000008, 0, 1)
#endif

DBPATCH_END()

