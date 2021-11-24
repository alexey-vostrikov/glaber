#ifndef GLB_TESTS_H
#define GLB_TESTS_H

#define NOT_EQUAL 0
#define EQUAL 1

static char *opertext[]= {"NOT_EQUAL", "EQUAL"};
//#define TEST_ENSURE ( condition, message )\
//    if (condition) {LOG_I("CHECK_TEST: messsage",)
#define TEST_OP_FAIL(value, operation, check_value, message) \
   { LOG_WRN("%s:%d :TEST FAILED (%s) got result %d, expected to be %s to %d, will exit, FIX the CODE to continue", \
            __FILE__, __LINE__, message, value, opertext[operation], check_value); \
    exit(-1); }

#define TEST_FAIL(message)  { LOG_WRN("TEST FAILED (%s), FIX CODE to continue",message); exit(-1);}
#define TEST_SUCCEED(message)  { LOG_WRN("TEST SUCCEED (%s)",message); }


#define TEST_ENSURE(value, operation, check_value, message) \
{       int res = value; \
        switch (operation) { \
        case NOT_EQUAL: \
            if (res == check_value) {  \
                TEST_OP_FAIL(res, operation, check_value, message); \
            } else { \
                TEST_SUCCEED(message); }\
            break; \
        case EQUAL:  \
            if (res != check_value) { \
                TEST_OP_FAIL(res, operation, check_value, message); \
            } else {\
                TEST_SUCCEED(message); }\
            break;\
        default: \
            TEST_FAIL("Unsupported operation supplied");\
            break;\
    }\
}
#endif