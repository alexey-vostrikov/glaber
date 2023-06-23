/*
** Glaber
** Copyright (C) Glaber
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

#ifndef PREPROCESSING_WORKER_H
#define REPROCESSING_WORKER_H

#include "zbxcommon.h"
#include "metric.h"
#include "zbxself.h"


ZBX_THREAD_ENTRY(glb_preprocessing_worker_thread, args);

//NOTE: only to be used inside glb_preprocessing fork
int preprocess_metric(const metric_t *metric);

#endif
