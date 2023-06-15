/*
** Glaber
** Copyright (C) 2001-2100 Glaber
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
               
typedef struct preproc_discovery_agg_conf_t preproc_discovery_agg_conf_t;

preproc_discovery_agg_conf_t *preproc_discovery_agg_init();
void    proproc_discovery_agg_destroy(preproc_discovery_agg_conf_t * conf);

int    preproc_discovery_account_json(preproc_discovery_agg_conf_t * conf, u_int64_t itemid, int reexport_freq,  const char *json, char **response);
//int  preproc_discovery_get_ready_data(preproc_discovery_agg_conf_t * conf, u_int64_t itemid, char **json_data);