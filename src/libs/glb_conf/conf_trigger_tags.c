static void	DCdump_trigger_tags(const ZBX_DC_TRIGGER *trigger)
{
	int			i;
	zbx_vector_ptr_t	index;

	zbx_vector_ptr_create(&index);

	zbx_vector_ptr_append_array(&index, trigger->tags.values, trigger->tags.values_num);
	zbx_vector_ptr_sort(&index, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	zabbix_log(LOG_LEVEL_TRACE, "  tags:");

	for (i = 0; i < index.values_num; i++)
	{
		zbx_dc_trigger_tag_t	*tag = (zbx_dc_trigger_tag_t *)index.values[i];
		zabbix_log(LOG_LEVEL_TRACE, "      tagid:" ZBX_FS_UI64 " tag:'%s' value:'%s'",
				tag->triggertagid, tag->tag, tag->value);
	}

	zbx_vector_ptr_destroy(&index);
}

/*static void	DCsync_trigger_tags(zbx_dbsync_t *sync)
{
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	int			found, ret, index;
	zbx_uint64_t		triggerid, triggertagid;
	//ZBX_DC_TRIGGER		*trigger;
	zbx_dc_trigger_tag_t	*trigger_tag;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		/* removed rows will be always added at the end */
/*		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

//		ZBX_STR2UINT64(triggerid, row[1]);

		if (FAIL == conf_triggers_trigger_exists(triggerid))
		//if (NULL == (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &triggerid)))
			continue;

		ZBX_STR2UINT64(triggertagid, row[0]);

		trigger_tag = (zbx_dc_trigger_tag_t *)DCfind_id(&config->trigger_tags, triggertagid,
				sizeof(zbx_dc_trigger_tag_t), &found);
		DCstrpool_replace(found, &trigger_tag->tag, row[2]);
		DCstrpool_replace(found, &trigger_tag->value, row[3]);

		if (0 == found)
		{
			trigger_tag->triggerid = triggerid;
			conf_triggers_add_tag(triggerid, trigger_tag);
			//zbx_vector_ptr_append(&trigger->tags, trigger_tag);
		}
	}

	/* remove unused trigger tags */
/*
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		if (NULL == (trigger_tag = (zbx_dc_trigger_tag_t *)zbx_hashset_search(&config->trigger_tags, &rowid)))
			continue;

		if 	(FAIL == conf_triggers_trigger_exists(trigger_tag->triggerid)) 
		//(NULL != (trigger = (ZBX_DC_TRIGGER *)zbx_hashset_search(&config->triggers, &trigger_tag->triggerid)))
		{
			if (SUCCEED == conf_triggers_trigger_has_tag(trigger_tag->triggerid, trigger_tag))
//			if (FAIL != (index = zbx_vector_ptr_search(&trigger->tags, trigger_tag,
//					ZBX_DEFAULT_PTR_COMPARE_FUNC)))
			{
				//zbx_vector_ptr_remove_noorder(&trigger->tags, index);
				conf_triggers_trigger_remove_tag(trigger_tag->triggerid, trigger_tag);
				/* recreate empty tags vector to release used memory */
				//if (0 == trigger->tags.values_num)
				//{
				//	zbx_vector_ptr_destroy(&trigger->tags);
				//	zbx_vector_ptr_create_ext(&trigger->tags, __config_mem_malloc_func,
				//			__config_mem_realloc_func, __config_mem_free_func);
				//}
/*			}
		}

		zbx_strpool_release(trigger_tag->tag);
//		zbx_strpool_release(trigger_tag->value);

//		zbx_hashset_remove_direct(&config->trigger_tags, trigger_tag);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}
*/