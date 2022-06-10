#pragma once

#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>

#include "asn1.h"

#define SNMP_VERSION_1  0
#define SNMP_VERSION_2c 1
#define SNMP_VERSION_3  3

#define SNMP_CMD_GET      (ASN1_CONTEXT | ASN1_CONSTRUCTOR | 0x00)
#define SNMP_CMD_GET_NEXT (ASN1_CONTEXT | ASN1_CONSTRUCTOR | 0x01)
#define SNMP_CMD_RESPONSE (ASN1_CONTEXT | ASN1_CONSTRUCTOR | 0x02)
#define SNMP_CMD_SET      (ASN1_CONTEXT | ASN1_CONSTRUCTOR | 0x03)
#define SNMP_CMD_TRAP     (ASN1_CONTEXT | ASN1_CONSTRUCTOR | 0x04)
#define SNMP_CMD_GET_BULK (ASN1_CONTEXT | ASN1_CONSTRUCTOR | 0x05)

#define SNMP_TP_BOOL     (ASN1_UNIVERSAL | 0x1)
#define SNMP_TP_INT      (ASN1_UNIVERSAL | 0x2)
#define SNMP_TP_BIT_STR  (ASN1_UNIVERSAL | 0x3)
#define SNMP_TP_OCT_STR  (ASN1_UNIVERSAL | 0x4)
#define SNMP_TP_NULL     (ASN1_UNIVERSAL | 0x5)
#define SNMP_TP_OID      (ASN1_UNIVERSAL | 0x6)
#define SNMP_TP_SEQ      (ASN1_UNIVERSAL | 0x10)
#define SNMP_TP_TYPE_SET (ASN1_UNIVERSAL | 0x11)

#define SNMP_TP_IP_ADDR   (ASN1_APPLICATION | 0x0)
#define SNMP_TP_COUNTER   (ASN1_APPLICATION | 0x1)
#define SNMP_TP_GAUGE     (ASN1_APPLICATION | 0x2)
#define SNMP_TP_TIMETICKS (ASN1_APPLICATION | 0x3)
#define SNMP_TP_OPAQUE    (ASN1_APPLICATION | 0x4)
#define SNMP_TP_COUNTER64 (ASN1_APPLICATION | 0x6)
#define SNMP_TP_FLOAT     (ASN1_APPLICATION | 0x8)
#define SNMP_TP_DOUBLE    (ASN1_APPLICATION | 0x9)
#define SNMP_TP_INT64     (ASN1_APPLICATION | 0x10)
#define SNMP_TP_UINT64    (ASN1_APPLICATION | 0x11)

#define SNMP_ERR_OK           0x0
#define SNMP_ERR_TOO_BIG      0x1
#define SNMP_ERR_NO_SUCH_NAME 0x2
#define SNMP_ERR_BAD_VALUE    0x3
#define SNMP_ERR_READ_ONLY    0x4
#define SNMP_ERR_GENERAL      0x5

#define SNMP_TP_NO_SUCH_OBJ      (ASN1_CONTEXT | ASN1_PRIMITIVE | 0x0)
#define SNMP_TP_NO_SUCH_INSTANCE (ASN1_CONTEXT | ASN1_PRIMITIVE | 0x1)
#define SNMP_TP_END_OF_MIB_VIEW  (ASN1_CONTEXT | ASN1_PRIMITIVE | 0x2)

typedef asn1_oid_t csnmp_oid_t;
typedef asn1_str_t csnmp_str_t;

typedef struct {
    csnmp_oid_t oid;
    int type;
    void* value;

    asn1_error_t error;
} csnmp_var_t;

typedef struct {
    struct sockaddr addr;
    socklen_t addr_len;

    int version;
    csnmp_str_t community;

    int command;
    int req_id;

    int max_repeaters, max_repetitions;

    int error_status, error_index;

    csnmp_var_t* vars;
    int vars_len;
    int vars_cap;

    asn1_error_t error;
} csnmp_pdu_t;

void csnmp_free_var(csnmp_var_t* v);
void csnmp_free_var_value(csnmp_var_t* v);
void csnmp_free_pdu(csnmp_pdu_t* p);
void csnmp_free_pdu_vars(csnmp_pdu_t* p);

int csnmp_add_error(csnmp_pdu_t* p, int code, const char* msg);
int csnmp_set_error_index(csnmp_pdu_t* p, int code, int index);
int csnmp_add_var(csnmp_pdu_t* p, asn1_oid_t oid, int tp, void* val);

int csnmp_bind(uint32_t addr, int port);
int csnmp_bind_addr(const char* addr);
int csnmp_close(int fd);

int csnmp_recv_pdu(int fd, csnmp_pdu_t* pdu);
int csnmp_send_pdu(int fd, csnmp_pdu_t* pdu);

int csnmp_dump_packet(int fd);

void csnmp_dump_pdu(const char* msg, csnmp_pdu_t* p);

const char* csnmp_command_str(int c);

int* csnmp_new_int(int v);

long long* csnmp_new_long(long long v);
