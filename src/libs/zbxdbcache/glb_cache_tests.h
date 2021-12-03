#ifdef GLB_CACHE_TESTS
#include "log.h"
#include "glb_tests.h"

#include "glb_cache.c"
#include "glb_cache_items.c"
#include "zbxserver.h"

extern int CONFIG_SERVER_STARTUP_TIME;
int glb_cache_add_elem(glb_cache_elems_t *elems, uint64_t id );

void init_cache(glb_cache_t * cache) {
    ZBX_DC_HISTORY hist[]= { {.ts.sec = 10, .itemid = 10, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 10},
                             {.ts.sec = 20, .itemid = 10, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 11},
                             {.ts.sec = 30, .itemid = 10, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 12},
                             {.ts.sec = 40, .itemid = 10, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 13} };
    glb_ic_add_values(hist,4);
    //glb_tsbuff_check_has_enough_items(elem->values, count, last_fit_idx);
}


int simple_succeed_cb(glb_cache_elem_t *elem, void *data) {
    return SUCCEED;
}

int simple_fail_cb(glb_cache_elem_t *elem, void *data) {
    return FAIL;
}

int simple_345_cb(glb_cache_elem_t *elem, void *data) {
    return 345;
}


void test__glb_cache_process_elem(glb_cache_t *cache) {
    LOG_INF("CACHE_TEST: start %s",__func__);
      
    TEST_ENSURE(glb_cache_process_elem(&glb_cache->items,10,simple_fail_cb,NULL),EQUAL,FAIL,"Check for FAIL callback param retrun");
    TEST_ENSURE(glb_cache_process_elem(&glb_cache->items,10,simple_succeed_cb,NULL),EQUAL,SUCCEED,"Check for SUCEED callback param retrun");
    TEST_ENSURE(glb_cache_process_elem(&glb_cache->items,11,simple_succeed_cb,NULL),EQUAL,SUCCEED,"Check for SUCEED on noniexiting element");
    TEST_ENSURE(glb_cache_process_elem(&glb_cache->items,10,NULL,NULL),EQUAL,FAIL,"Check for FAIL on NULL function");

    LOG_INF("CACHE_TEST: finished %s",__func__);
}


void test__glb_cache_add_elem(glb_cache_t *cache) {
    LOG_INF("CACHE_TEST: start %s",__func__);
    
    TEST_ENSURE( glb_cache_add_elem(&glb_cache->items, 123) == SUCCEED, EQUAL, 1, "Test of new item element creation");
    TEST_ENSURE( glb_cache_add_elem(&glb_cache->items, 123), EQUAL, FAIL, "Test of FAIL of another item with the same id creation");
    TEST_ENSURE( glb_cache_process_elem(&glb_cache->items, 123,simple_succeed_cb, NULL), EQUAL, SUCCEED, "Ensure the element is created");
    TEST_ENSURE( glb_cache_add_elem(&glb_cache->items, 122), EQUAL, SUCCEED, "Test of success on adding new element");
    TEST_ENSURE( glb_cache_process_elem(&glb_cache->items, 122,simple_succeed_cb, NULL), EQUAL,SUCCEED, "Ensure the previous element is created");
    TEST_ENSURE( glb_cache_add_elem(&glb_cache->items, 123), EQUAL, FAIL, "Test of FAIL another item with the same id creation");
    
    LOG_INF("CACHE_TEST: finished %s",__func__);
}

void test__glb_cache_add_item_values(glb_cache_t *cache) {
    LOG_INF("CACHE_TEST: start %s",__func__);

    //TEST_ENSURE(glb_cache_add_item_values(&glb_cache->items, NULL, 0), EQUAL, FAIL, "FAIL on no items added, no items check");

    LOG_INF("CACHE_TEST: finished %s",__func__);
}


void test__glb_tsbuff_add_value(glb_cache_t *cache) {
    LOG_INF("CACHE_TEST: start %s",__func__);
    
 //   glb_tsbuff_add_value()

    LOG_INF("CACHE_TEST: finished %s",__func__);
}

 
void test__glb_cache_get_elem(glb_cache_t *cache) {
    LOG_INF("CACHE_TEST: start %s",__func__);
    glb_cache_elem_t *elem;
       
    //elem = glb_cache_get_elem(&cache->items, 10);
    //TEST_ENSURE(elem, NOT_EQUAL, NULL, "Make sure fetching of an element works");
    
    LOG_INF("CACHE_TEST: finished %s",__func__);
}

void *glb_malloc(void *old, size_t size) {
    void *buff = NULL;
    buff = zbx_malloc(old,size);
    return buff;
}
void glb_free(void *old) {
    zbx_free(old);
}

int last_free_time=0;

void val_free_cb(zbx_mem_malloc_func_t alloc_func, zbx_mem_free_func_t free_func, void* value) {
    glb_tsbuff_value_t *val = (glb_tsbuff_value_t*)value;
    last_free_time = val->sec;
}

void test__glb_tsbuff(glb_tsbuff_t *tsbuff) {
    LOG_INF("CACHE_TEST: start %s",__func__);
    glb_cache_elem_t *elem;
    
    TEST_ENSURE(glb_tsbuff_init(tsbuff,10,30,glb_malloc), EQUAL, SUCCEED, "SUCCEED in normal run");
    TEST_ENSURE(glb_tsbuff_is_full(tsbuff), EQUAL, FAIL, "Check is_full is FAILing");
    TEST_ENSURE(glb_tsbuff_init(tsbuff,0,3,glb_malloc), EQUAL, FAIL, "FAIL no vals");
    TEST_ENSURE(glb_tsbuff_init(tsbuff,3,0, NULL), EQUAL, FAIL, "FAIL no size");
    TEST_ENSURE(glb_tsbuff_init(tsbuff,3,1, NULL), EQUAL, FAIL, "FAIL no mem func");
    TEST_ENSURE(glb_tsbuff_get_size(tsbuff), EQUAL, 10, "check proper size");
    TEST_ENSURE(glb_tsbuff_get_count(tsbuff), EQUAL, 0, "Elements count check" );
    assert( glb_tsbuff_add_to_head(tsbuff,234) != NULL); 
    TEST_ENSURE(glb_tsbuff_get_count(tsbuff), EQUAL, 1, "Elements count check after 1 elem" );
    TEST_ENSURE(glb_tsbuff_get_value_ptr(tsbuff, tsbuff->head) == glb_tsbuff_get_value_ptr(tsbuff, tsbuff->tail)
        , EQUAL, 1, "Head and tail point to the same first item");
    assert( glb_tsbuff_add_to_head(tsbuff,235) != NULL); 
    TEST_ENSURE(glb_tsbuff_get_value_ptr(tsbuff, tsbuff->head) !=  glb_tsbuff_get_value_ptr(tsbuff, tsbuff->tail)
                , EQUAL, 1 , "Head and tail point to different items");
    
    glb_tsbuff_init(tsbuff,2,10,glb_malloc);
    TEST_ENSURE(glb_tsbuff_get_time_tail(tsbuff),EQUAL,-1, "Ensure there is no time in the tail set");
    TEST_ENSURE(glb_tsbuff_add_to_tail(tsbuff,200) != NULL , EQUAL, 1, "Add to tail test");
    TEST_ENSURE(glb_tsbuff_get_count(tsbuff), EQUAL, 1, "Elements count check after 1 elem" );
    TEST_ENSURE(glb_tsbuff_get_time_head(tsbuff), EQUAL, 200, "Ensure getting the correct time from head");
    TEST_ENSURE(glb_tsbuff_get_time_tail(tsbuff), EQUAL, 200, "Ensure getting the correct time from head");
    TEST_ENSURE(glb_tsbuff_get_value_tail(tsbuff) != NULL , EQUAL, 1,  "Ensure getting a value ptr from the tail");
    TEST_ENSURE(glb_tsbuff_get_value_head(tsbuff) != NULL , EQUAL, 1 ,  "Ensure getting a value ptr from the head");
    TEST_ENSURE(glb_tsbuff_add_to_head(tsbuff, 250) != NULL , EQUAL, 1 , "SUCCEED to add value with recent time to the head");
    TEST_ENSURE(glb_tsbuff_add_to_tail(tsbuff, 250) == NULL , EQUAL, 1 , "FAIL to add the tail");
    TEST_ENSURE(glb_tsbuff_add_to_head(tsbuff, 100) == NULL , EQUAL, 1 , "FAIL to add to the head");
    
    TEST_ENSURE(glb_tsbuff_free_tail(tsbuff), EQUAL, SUCCEED, "SUCCEED to free a value");
    TEST_ENSURE(glb_tsbuff_add_to_head(tsbuff, 251) != NULL , EQUAL, 1 , "SUCCEED to add value with recent time to the head2");
    TEST_ENSURE(glb_tsbuff_free_tail(tsbuff), EQUAL, SUCCEED , "SUCCEED to free a value");
    TEST_ENSURE(glb_tsbuff_add_to_head(tsbuff, 251) != NULL , EQUAL, 1 , "SUCCEED to add value with recent time to the head3");
    TEST_ENSURE(glb_tsbuff_get_count(tsbuff), EQUAL, 2, "Elements count check after adding 5 elements to 2 element buffer" );


    glb_tsbuff_init(tsbuff,5,10,glb_malloc);
    
    TEST_ENSURE(glb_tsbuff_find_time_idx(tsbuff,501),EQUAL,FAIL,"Empty tsbuff search attempt");
    TEST_ENSURE(glb_tsbuff_free_tail(tsbuff), EQUAL, FAIL, "FAIL to free a value from empty buffer");
    glb_tsbuff_add_to_head(tsbuff, 100);
    glb_tsbuff_add_to_head(tsbuff, 200);
    glb_tsbuff_add_to_head(tsbuff, 300);
    glb_tsbuff_add_to_head(tsbuff, 400);
    glb_tsbuff_add_to_head(tsbuff, 500);
    TEST_ENSURE(glb_tsbuff_is_full(tsbuff), EQUAL, SUCCEED, "Check is_full is SUCCEEDing");
    TEST_ENSURE(glb_tsbuff_find_time_idx(tsbuff,250),EQUAL,1,"Test for correct index for time 200");
    TEST_ENSURE(glb_tsbuff_find_time_idx(tsbuff,500),EQUAL,4,"Test for correct index for time 500");
    TEST_ENSURE(glb_tsbuff_find_time_idx(tsbuff,501),EQUAL,FAIL,"Test for FAIL time out of bounds");

    //do an overflowed buffer test
    glb_tsbuff_free_tail(tsbuff);
    glb_tsbuff_free_tail(tsbuff);
    
    TEST_ENSURE(glb_tsbuff_get_count(tsbuff),EQUAL,3,"Only 3 values left ");
    glb_tsbuff_add_to_head(tsbuff, 501);
    glb_tsbuff_add_to_head(tsbuff, 502);
    
    TEST_ENSURE(tsbuff->head,EQUAL, 1, "head is 1");
    TEST_ENSURE(tsbuff->tail,EQUAL, 2, "tail is 2");

    TEST_ENSURE(glb_tsbuff_find_time_idx(tsbuff,501),EQUAL,0,"Test for overflow idx fetch");
    TEST_ENSURE(glb_tsbuff_find_time_idx(tsbuff,502),EQUAL,1,"Test for overflow idx fetch");
    TEST_ENSURE(glb_tsbuff_find_time_idx(tsbuff,503),EQUAL,FAIL,"Test overflow out of bounds");
    TEST_ENSURE(glb_tsbuff_find_time_idx(tsbuff,100),EQUAL,FAIL,"Test overflow out of bounds");
    TEST_ENSURE(glb_tsbuff_find_time_idx(tsbuff,300),EQUAL,2,"Test to first elem hit in overflow");

    TEST_ENSURE(glb_tsbuff_get_time_head(tsbuff), EQUAL, 502, "Check that the most recent time");
    //buffer resizing checks
    
    void *old_buffer = tsbuff->data;
    TEST_ENSURE(glb_tsbuff_resize(tsbuff, 10, glb_malloc, glb_free, NULL), EQUAL, 10, "Check resize worked");
    TEST_ENSURE(glb_tsbuff_get_size(tsbuff),EQUAL,10, "Check that size has changed");
    //TEST_ENSURE(tsbuff->data, NOT_EQUAL, old_buffer, "Check that buffer reallocated after resizing");
    
    TEST_ENSURE(glb_tsbuff_resize(tsbuff, 100000000, glb_malloc, glb_free, NULL),EQUAL,FAIL, "Check that resize to huge size fails");
    TEST_ENSURE(tsbuff->tail,EQUAL,0,"Check the tail is set to 0");
    TEST_ENSURE(tsbuff->head,EQUAL,4,"Check the head is set to 4");
    TEST_ENSURE(glb_tsbuff_get_time_head(tsbuff), EQUAL, 502, "Check that the most recent time left (resize correctness)");
    TEST_ENSURE(glb_tsbuff_get_time_tail(tsbuff), EQUAL, 300, "Check that the most recent time left (resize correctness)");

    //TESTING DOWNSCALING
    glb_tsbuff_free_tail(tsbuff);
    TEST_ENSURE(glb_tsbuff_get_time_tail(tsbuff), EQUAL, 400, "Check that the most recent time left (resize correctness)");

    glb_tsbuff_add_to_head(tsbuff, 503);
    glb_tsbuff_add_to_head(tsbuff, 504);

//    glb_tsbuff_dump(tsbuff);
    TEST_ENSURE(glb_tsbuff_resize(tsbuff, 5, glb_malloc, glb_free, val_free_cb), EQUAL, 5, "Check downsize worked");
//    glb_tsbuff_dump(tsbuff);
    TEST_ENSURE(glb_tsbuff_get_time_head(tsbuff),EQUAL,504, "Ensure proper buffer downsize");

    TEST_ENSURE(last_free_time, NOT_EQUAL, 0, "Check if val free callback has been called");
    TEST_ENSURE(glb_tsbuff_get_size(tsbuff), EQUAL, 5, "check proper size");
    TEST_ENSURE(glb_tsbuff_get_count(tsbuff), EQUAL, 5, "check proper count");  
    
    last_free_time =0;
    
    
    TEST_ENSURE(glb_tsbuff_resize(tsbuff, 4, glb_malloc, glb_free, NULL), EQUAL, 4, "Check downsize worked with NULL cb");
    
    //glb_tsbuff_dump(tsbuff);

    TEST_ENSURE(last_free_time, EQUAL, 0, "Check if val free callback has NOT been called");
    
    TEST_ENSURE(glb_tsbuff_resize(tsbuff, 5, glb_malloc, glb_free, NULL), EQUAL, 5, "Check resize worked back");
    TEST_ENSURE(glb_tsbuff_get_size(tsbuff), EQUAL, 5, "check proper size");
    TEST_ENSURE(glb_tsbuff_get_count(tsbuff), EQUAL, 4, "Elements count check" );
    
    TEST_ENSURE(glb_tsbuff_find_time_idx(tsbuff,503), EQUAL, 2,"Testing to time idx");
    
    //glb_tsbuff_dump(tsbuff);
    TEST_ENSURE(glb_tsbuff_check_has_enough_count_data_time(tsbuff, 2, 503), EQUAL, SUCCEED, "Check there are at least 2 values after 503");
    TEST_ENSURE(glb_tsbuff_check_has_enough_count_data_time(tsbuff, 2, 600), EQUAL, FAIL, "Check there no values starting at 600");
    TEST_ENSURE(glb_tsbuff_check_has_enough_count_data_time(tsbuff, 2, 501), EQUAL, FAIL, "Check there no values older then 501");
    TEST_ENSURE(glb_tsbuff_check_has_enough_count_data_time(tsbuff, 1, 501), EQUAL, SUCCEED, "Check there is a value older then 501 (including 501)");
    
    glb_tsbuff_free_tail(tsbuff);
    glb_tsbuff_free_tail(tsbuff);

    glb_tsbuff_add_to_head(tsbuff, 505);
    glb_tsbuff_add_to_head(tsbuff, 517);
    glb_tsbuff_add_to_head(tsbuff, 617);
    
    //glb_tsbuff_dump(tsbuff);
    TEST_ENSURE(glb_tsbuff_check_has_enough_count_data_time(tsbuff, 5, 617), EQUAL, SUCCEED, "Check with overflow");
    TEST_ENSURE(glb_tsbuff_check_has_enough_count_data_time(tsbuff, 1, 503), EQUAL, SUCCEED, "Check tail value");
    TEST_ENSURE(glb_tsbuff_check_has_enough_count_data_time(tsbuff, 6, 617), EQUAL, FAIL, "Check exceeding amount");
    TEST_ENSURE(glb_tsbuff_check_has_enough_count_data_time(tsbuff, 2, 503), EQUAL, FAIL, "Check tail value exceeding amount");
    TEST_ENSURE(glb_tsbuff_check_has_enough_count_data_time(tsbuff, 2, 550), EQUAL, SUCCEED, "Check some value");
    TEST_ENSURE(glb_tsbuff_check_has_enough_count_data_time(tsbuff, 0, 550), EQUAL, FAIL, "Zero values always fail");
    
    LOG_INF("CACHE_TEST: finished %s",__func__);
}

void test__glb_cache_item_values(glb_cache_t *cache) {
    ZBX_DC_HISTORY hist2[]= { {.ts.sec = 10, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 10},
                              {.ts.sec = 20, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 11},
                              {.ts.sec = 30, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 12},
                              {.ts.sec = 40, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 13} };
    glb_ic_add_values(hist2,4);
    
    zbx_vector_history_record_t	values;
	zbx_history_record_vector_create(&values);
      
    
    TEST_ENSURE(glb_cache_get_item_values_by_count(&cache->items.config, &cache->items, 12, ITEM_VALUE_TYPE_UINT64, &values, 2, 40),
            EQUAL, FAIL, "Absent id request FAIL");

    TEST_ENSURE(glb_cache_get_item_values_by_count(&cache->items.config, &cache->items, 11, ITEM_VALUE_TYPE_UINT64, &values, 2, 40),
            EQUAL, SUCCEED, "Present id request SUCCEED");
    
    TEST_ENSURE(values.values_num, EQUAL, 2, "Test fetching from the cache");
    
    //changing the value type
    ZBX_DC_HISTORY hist3[]= { {.ts.sec = 10, .itemid = 11, .value_type = ITEM_VALUE_TYPE_STR, .value.str = "test_record"}};
    //this should just fail
    TEST_ENSURE(glb_ic_add_values(hist3, 1), EQUAL, SUCCEED, "Succeeded to add outdated, but new type of data");
    
    //this should add new value type and purge all old data
    ZBX_DC_HISTORY hist4[]= { {.ts.sec = 100, .itemid = 11, .value_type = ITEM_VALUE_TYPE_STR, .value.str = "test_record"}};
    TEST_ENSURE(glb_ic_add_values(hist4, 1), EQUAL, SUCCEED, "Succeded to add new value type data");
    
    TEST_ENSURE(glb_cache_get_item_values_by_count(&cache->items.config, &cache->items, 11, ITEM_VALUE_TYPE_UINT64, &values, 1, 100),
            EQUAL, SUCCEED, "Cache retruned some data");
    
    TEST_ENSURE(strcmp(values.values[0].value.str,"test_record"),EQUAL,0,"Ensure returned from the cache string is the same");

        
    TEST_ENSURE(glb_cache_get_item_values_by_count(&cache->items.config, &cache->items, 11, ITEM_VALUE_TYPE_UINT64, &values, 4, 100),
            EQUAL, FAIL, "Cache indicated it has no data");

    //test adding with overflow
    //by default there 10 items, so lets try to add 10 more, and change the type again
    ZBX_DC_HISTORY hist5[]= { 
        {.ts.sec = 1, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 2},
        {.ts.sec = 2, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 3},
        {.ts.sec = 3, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 4},
        {.ts.sec = 4, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 5},
        {.ts.sec = 5, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 6},
        {.ts.sec = 6, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 7},
        {.ts.sec = 7, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 8},
        {.ts.sec = 8, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 9},
        {.ts.sec = 9, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 10},
        {.ts.sec = 10, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 11},
        {.ts.sec = 11, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 12}
    };

    glb_ic_add_values(hist5, 11);
    //the buffer hasn't to enrlage - demands hasn't beent set
    TEST_ENSURE(glb_cache_get_item_values_by_count(&cache->items.config, &cache->items, 11, ITEM_VALUE_TYPE_UINT64, &values, GLB_CACHE_MIN_ITEM_VALUES, 12),
             EQUAL,SUCCEED ,"Should be filled" );
    TEST_ENSURE(glb_cache_get_item_values_by_count(&cache->items.config, &cache->items, 11, ITEM_VALUE_TYPE_UINT64, &values, GLB_CACHE_MIN_ITEM_VALUES + 1, 12),
             EQUAL, FAIL ,"Should not be more then default items no" );
    
    //add 5 more elements to make sure buffer will not be enlarged and we can fetch all the data 
    ZBX_DC_HISTORY hist6[]= { 
        {.ts.sec = 12, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 2},
        {.ts.sec = 13, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 3},
        {.ts.sec = 14, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 4},
        {.ts.sec = 15, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 5},
        {.ts.sec = 16, .itemid = 11, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 6} };
    glb_ic_add_values(hist6, 5);
    
    TEST_ENSURE(glb_cache_get_item_values_by_count(&cache->items.config, &cache->items, 11, ITEM_VALUE_TYPE_UINT64, &values, GLB_CACHE_MIN_ITEM_VALUES , 16),
             EQUAL, SUCCEED ,"Should be filled, overflow check" );
    
    TEST_ENSURE(glb_cache_get_item_values_by_count(&cache->items.config, &cache->items, 11, ITEM_VALUE_TYPE_UINT64, &values, GLB_CACHE_MIN_ITEM_VALUES + 10, 15),
             EQUAL, FAIL ,"Should fail, overflow check" );

    TEST_ENSURE(values.values[9].timestamp.sec, EQUAL, 16, "Ensure the last item has 15 timestamp");
    TEST_ENSURE(values.values[9].value.ui64, EQUAL, 6, "Cache value equals to the one we've set");
    
    zbx_history_record_vector_destroy(&values, ITEM_VALUE_TYPE_UINT64);
    zbx_history_record_vector_create(&values);
    
    TEST_ENSURE(glb_cache_get_item_values_by_time(&cache->items.config, &cache->items, 11,
             ITEM_VALUE_TYPE_UINT64,&values, 4, 15),EQUAL,SUCCEED,"Time based fetch successfull");

   
    TEST_ENSURE(values.values_num, EQUAL, 5,"Ensure all 5 elements are returned from the cache" );
    
    TEST_ENSURE(values.values[0].timestamp.sec,EQUAL,11,"tail timestamp matches");
    TEST_ENSURE(values.values[4].timestamp.sec,EQUAL,15,"head timestamp matches");

    //testing db fetches
     ZBX_DC_HISTORY hist7[]= { 
        {.ts.sec = 11, .itemid = 12, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 2, .host_name ="test", .item_key="test_item"},
        {.ts.sec = 12, .itemid = 12, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 3, .host_name ="test", .item_key="test_item"},
        {.ts.sec = 13, .itemid = 12, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 4, .host_name ="test", .item_key="test_item"},
        {.ts.sec = 14, .itemid = 12, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 5, .host_name ="test", .item_key="test_item"},
        {.ts.sec = 15, .itemid = 12, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 6, .host_name ="test", .item_key="test_item"} };
     
    TEST_ENSURE(glb_history_add(hist7,5),EQUAL,SUCCEED, "Ensure could add data to the history backend");
    //getting by count from the cache
    //emulate already completed request
    //to properly set the value type adding one value to the hist cache
    glb_ic_add_values(&hist7[4], 1);
    CONFIG_SERVER_STARTUP_TIME = 0;
    int now = time(NULL) - 360;
    
    ZBX_DC_HISTORY hist8[]= { 
        {.ts.sec = now - 11, .itemid = 14, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 2, .host_name ="test", .item_key="test_item"},
        {.ts.sec = now - 12, .itemid = 14, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 3, .host_name ="test", .item_key="test_item"},
        {.ts.sec = now - 13, .itemid = 14, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 4, .host_name ="test", .item_key="test_item"},
        {.ts.sec = now - 14, .itemid = 14, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 5, .host_name ="test", .item_key="test_item"},
        {.ts.sec = now - 15, .itemid = 14, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 6, .host_name ="test", .item_key="test_item"}, 
        {.ts.sec = now - 16, .itemid = 14, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 7, .host_name ="test", .item_key="test_item"},
        {.ts.sec = now - 17, .itemid = 14, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 8, .host_name ="test", .item_key="test_item"} 
        
        };
    
     ZBX_DC_HISTORY hist9[]= { 
        {.ts.sec = now, .itemid = 14, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 2, .host_name ="test", .item_key="test_item"}};
    
    TEST_ENSURE(glb_history_add(hist8,5),EQUAL,SUCCEED, "Ensure could add up-to date data to the history backend");
    sleep(1); //to cause history iface to drop buffers need to wait a bit
    TEST_ENSURE(glb_history_add(hist9,1),EQUAL,SUCCEED, "adding one more value to flush the existing values");
    sleep(1);
    TEST_ENSURE(glb_history_add(hist9,1),EQUAL,SUCCEED, "adding one more value to flush the existing values");

    TEST_ENSURE( glb_ic_add_values(hist9, 1), EQUAL, SUCCEED, "Added now value to the cache");

    TEST_ENSURE(glb_cache_get_item_values_by_count(&cache->items.config, &cache->items, 14,
             ITEM_VALUE_TYPE_UINT64, &values,2,now),EQUAL,SUCCEED, "Count based fetch cache with one value, from db is successful");

    TEST_ENSURE(glb_cache_get_item_values_by_time(&cache->items.config, &cache->items, 14,
             ITEM_VALUE_TYPE_UINT64, &values, now - 14,  now-1),EQUAL,SUCCEED, "Time based fetch from the cache with one value, from db is successful");
    LOG_INF("Fetched %d values",values.values_num);



    zbx_history_record_vector_destroy(&values, ITEM_VALUE_TYPE_UINT64);
}

//scenario^ 
void test__glb_ic_add_get_values(glb_cache_t *cache) {
    LOG_INF("CACHE_TEST: started %s",__func__); 
    u_int64_t itemid = rand();
    int now = time(NULL);
    float test_float=-2.234234;
    char *test_str = "This is a a test string it may have to include some special chars for testing needs ";
    
    zbx_vector_history_record_t	values;
	zbx_history_record_vector_create(&values);

    LOG_INF("Using itemid %ld for tests", itemid);
    //lets try a single value fetch 
    ZBX_DC_HISTORY hist[]= { 
        {.ts.sec = now, .itemid = itemid, .value_type = ITEM_VALUE_TYPE_FLOAT, .value.dbl = test_float, .host_name ="test", .item_key="test_float_item"}};
    glb_ic_add_values(hist,1);
    
    TEST_ENSURE(glb_ic_get_values(itemid, ITEM_VALUE_TYPE_FLOAT,&values, 0, 1, now),EQUAL,SUCCEED,"Check for single float fetch");
    TEST_ENSURE(values.values[0].value.dbl == test_float,EQUAL, 1 ,"Ensure we got the same float value from the cache");
    zbx_history_record_vector_destroy(&values, ITEM_VALUE_TYPE_FLOAT);
    
    zbx_history_record_vector_create(&values);
    ZBX_DC_HISTORY hist1[]= { 
        {.ts.sec = now, .itemid = itemid, .value_type = ITEM_VALUE_TYPE_STR, .value.str = test_str, .host_name ="test", .item_key="test_str_item"}};
    glb_ic_add_values(hist1,1); //by the way this will cause purging and changing the type of the cache data
    TEST_ENSURE(glb_ic_get_values(itemid, ITEM_VALUE_TYPE_STR,&values, 0, 1, now),EQUAL,SUCCEED,"Check for single string fetch");
    TEST_ENSURE(strcmp(test_str, values.values[0].value.str), EQUAL, 0 ,"Ensure we got the same string value from the cache");
    zbx_history_record_vector_destroy(&values, ITEM_VALUE_TYPE_STR);

    zbx_history_record_vector_create(&values);
    ZBX_DC_HISTORY hist2[]= { 
        {.ts.sec = now-10,   .itemid = itemid, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 2,  .host_name ="test", .item_key="test_uint_item"},
        {.ts.sec = now, .itemid = itemid, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 4,  .host_name ="test", .item_key="test_uint_item"},
    };
  
    glb_ic_add_values(hist2,2); //by the way this will cause purging and changing the type of the cache data
    TEST_ENSURE(glb_ic_get_values(itemid, ITEM_VALUE_TYPE_UINT64,&values, 0, 2, now),EQUAL,SUCCEED,"Check for the double ui64 fetch");
   
    //lets test fetching from the history backend
    //first make sure fetching non history- existent item will fail
    TEST_ENSURE(glb_ic_get_values(itemid, ITEM_VALUE_TYPE_UINT64,&values, 0, 5, now),EQUAL,FAIL,"Check for the FAIL of partly - non existent history");
    //history exists but not old enough
    TEST_ENSURE(glb_ic_get_values(itemid, ITEM_VALUE_TYPE_UINT64,&values, 0 , 2, now-15), EQUAL, FAIL,
                "Check FAIL for too old history fetch");
    //history exists only partially
    TEST_ENSURE(glb_ic_get_values(itemid, ITEM_VALUE_TYPE_UINT64,&values, 0 , 2, now-7), EQUAL, FAIL,
                "Check FAIL for partial history fetch");

    zbx_history_record_vector_destroy(&values, ITEM_VALUE_TYPE_UINT64);
    LOG_INF("CACHE_TEST: finished %s",__func__);
}
void test__evaluate_functions(glb_cache_t *cache) {
    int ret;
    zbx_variant_t value;
    DC_ITEM item;
    zbx_timespec_t ts;
    //char error[1024];
    char *errptr = NULL;
    int now = time(NULL);
    u_int64_t itemid = rand();
    
    LOG_INF("CACHE_TEST: started %s",__func__);
    
    zbx_timespec(&ts);
    item.itemid = itemid;
    item.value_type = ITEM_VALUE_TYPE_UINT64;

    ZBX_DC_HISTORY hist[]= { 
        {.ts.sec = now-30,   .itemid = itemid, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 2,  .host_name ="test", .item_key="test_uint_item"}};

    glb_ic_add_values(hist,1);    
    
    TEST_ENSURE(evaluate_function2(&value, &item, "nodata", "10s", &ts, &errptr), EQUAL, SUCCEED, "Nodata call retrun succeed");
    TEST_ENSURE(value.data.dbl == 1, EQUAL, 1 , "nodata2 for 10sec returned 1");
    
    TEST_ENSURE(evaluate_function2(&value, &item, "nodata", "60s", &ts, &errptr), EQUAL, SUCCEED, "Nodata call retrun succeed");
    TEST_ENSURE(value.data.dbl == 0, EQUAL, 1 , "nodata2 for 60sec returned 0");

    //lets test a few average func calls 
    //count(10m:now-1d)
    TEST_ENSURE(evaluate_function2(&value, &item, "count", "30s:now-60s", &ts, &errptr), EQUAL, SUCCEED, "SUCCEED no data count call");

    TEST_ENSURE(value.data.dbl == 0, EQUAL, 1 , "count is 0 for 60sec ");
    
    TEST_ENSURE(evaluate_function2(&value, &item, "count", "30:now-10s", &ts, &errptr), EQUAL, SUCCEED , "SUCCEED count call");
    TEST_ENSURE(value.data.dbl == 1, EQUAL, 1 , "count for returned 1");

    ZBX_DC_HISTORY hist2[]= { 
        {.ts.sec = now-29,   .itemid = itemid, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 3,  .host_name ="test", .item_key="test_uint_item"},
        {.ts.sec = now-28,   .itemid = itemid, .value_type = ITEM_VALUE_TYPE_UINT64, .value.ui64 = 4,  .host_name ="test", .item_key="test_uint_item"}};

    glb_ic_add_values(hist2,2);    
    TEST_ENSURE(evaluate_function2(&value, &item, "avg", "30s", &ts, &errptr), EQUAL, SUCCEED, "SUCCEED avg call");
    TEST_ENSURE(value.data.dbl == 3.0, EQUAL, 1 , "avg returned 3");
    
    TEST_ENSURE(evaluate_function2(&value, &item, "avg", "#2", &ts, &errptr), EQUAL, SUCCEED, "SUCCEED avg call");
    TEST_ENSURE(value.data.dbl == 3.5, EQUAL, 1 , "avg for last two values returned 3.5");

    TEST_ENSURE(evaluate_function2(&value, &item, "avg", "#2:now-29s", &ts, &errptr), EQUAL, SUCCEED, "SUCCEED avg call");
    TEST_ENSURE(value.data.dbl == 2.5, EQUAL, 1 , "avg for last two values 29 sec ago returned 2.5");

    LOG_INF("CACHE_TEST: finished %s",__func__);
}

void glb_run_cache_tests(glb_cache_t *cache) {
   LOG_INF("CACHE_TESTS: starting");
   
    init_cache(cache);

    test__glb_cache_process_elem(cache);
    test__glb_cache_add_elem(cache);
    test__glb_cache_add_item_values(cache); 

    glb_tsbuff_t tsbuff;
    test__glb_tsbuff(&tsbuff);
    
    test__glb_cache_item_values(cache);
    test__glb_ic_add_get_values(cache);
    test__evaluate_functions(cache);

 //  TEST_FAIL("Tests are finished");
   LOG_INF("CACHE_TESTS: finished");
}
#endif
