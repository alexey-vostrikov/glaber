
/****************** GNU GPL2 copyright goes here ***********************/

#include "zbxalgo.h"
#include "log.h"

typedef struct {
	uint32_t ref_count;
} binpool_head_t;

static zbx_hashset_t binpool;

static zbx_hash_t	__binpool_hash(const void *hdata)
{	//binpool_head_t *head = (binpool_head_t*)hdata;
	binpool_data_t *bdata = (binpool_data_t *) ((void *) hdata  + sizeof(binpool_head_t));
	
	//const u_int64_t *dat;
	//dat = (u_int64_t*)bdata->data;
	//LOG_INF("Not found record3 for data %ld %ld %ld %ld %ld %ld, size %d", dat[0],dat[1],dat[2],dat[3],dat[4],dat[5],  bdata->size );

	u_int32_t hash =zbx_hash_modfnv( (void *)bdata->data , bdata->size  ,0 );
//	LOG_INF("Calculated hash is %d, size is %d", hash, bdata->size);
	return hash;
	//return XXH64((void *)data + sizeof(binpool_head_t), info->size, 0);
	//return ZBX_DEFAULT_STRING_HASH_FUNC((void *)data + sizeof(binpool_head_t));
}

static int	__binpool_compare(const void *d1, const void *d2)
{
	binpool_data_t *p1 = (binpool_data_t *)(d1 + sizeof(binpool_head_t)), 
				   *p2 = (binpool_data_t *)(d2 + sizeof(binpool_head_t));

	if (p1->size > p2->size) return -1;
	if (p1->size < p2->size) return 1;

	return memcmp((void *)p1->data, (void *)p2->data, p1->size);
}

void glb_heap_binpool_init(){
	zbx_hashset_create(&binpool, 100, __binpool_hash, __binpool_compare);
}

void glb_heap_binpool_destroy(){
	zbx_hashset_destroy(&binpool);
}

const binpool_data_t *glb_heap_binpool_intern(binpool_data_t *bdata)
{
	binpool_head_t *head;
	binpool_data_t *saved;
	
	//u_int64_t *dat = (u_int64_t*)bdata->data;
	
	//LOG_INF("Interning data %ld %ld %ld %ld %ld %ld, size %d", dat[0],dat[1],dat[2],dat[3],dat[4],dat[5],  bdata->size );

	if (NULL == bdata) 
		return NULL;
	
	//LOG_INF("Searhcing");
	head =(binpool_head_t *) zbx_hashset_search(&binpool,(void *)bdata - sizeof(binpool_head_t));

	if (NULL == head)
	{
		//LOG_INF("Not found");
		//LOG_INF("Not found record for data %ld %ld %ld %ld %ld %ld, size %d", dat[0],dat[1],dat[2],dat[3],dat[4],dat[5],  bdata->size );
		
		head = (binpool_head_t*)zbx_hashset_insert_ext(&binpool, (void*)bdata - sizeof(binpool_head_t),
				sizeof(binpool_head_t) + bdata->size, sizeof(binpool_head_t));
	
		if (NULL == head) {
			return NULL;
		}
		head->ref_count=0;
		
		//clean next three lines
		saved = (binpool_data_t *)((void*)(head) + sizeof(binpool_head_t));
		
	//	dat = (u_int64_t*)saved->data;
	//	LOG_INF("Not found record2 for data %ld %ld %ld %ld %ld %ld, size %d", dat[0],dat[1],dat[2],dat[3],dat[4],dat[5],  saved->size );
	} else {
		//LOG_INF("Not found");
		saved = (binpool_data_t *)((void*)(head) + sizeof(binpool_head_t));
	}
	//LOG_INF("Added/found");
	head->ref_count++;
		
	return saved;
}

void glb_heap_binpool_release(const binpool_data_t *bdata)
{
	binpool_head_t *head;

	if ( NULL == bdata ) 
		return;
	
	head = (binpool_head_t *)((void*)bdata - sizeof(binpool_head_t));

	if (0 == (--head->ref_count) )
		zbx_hashset_remove(&binpool, head);
}

const binpool_data_t *glb_heap_binpool_acquire(binpool_data_t *bdata)
{
	binpool_head_t *head;

	head = (binpool_head_t *)((void *)bdata - sizeof(binpool_head_t));
	head->ref_count ++;

	return bdata;
}