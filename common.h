// common.h
#ifndef COMMON_H
#define COMMON_H

#pragma once

#include <stdlib.h>
#include <arpa/inet.h>

// Message Types
#define REQ_CONNPEER  17
#define RES_CONNPEER  18
#define REQ_DISCPEER  19
#define REQ_CONN      20
#define RES_CONN      21
#define REQ_DISC      22

#define REQ_USRADD    33
#define REQ_USRACCESS 34
#define RES_USRACCESS 35
#define REQ_LOCREG    36
#define RES_LOCREG    37
#define REQ_USRLOC    38
#define RES_USRLOC    39
#define REQ_LOCLIST   40
#define RES_LOCLIST   41
#define REQ_USRAUTH   42
#define RES_USRAUTH   43

#define ERROR         255
#define OK            0

// Error Codes
#define ERR_PEER_LIMIT_EXCEEDED 1
#define ERR_PEER_NOT_FOUND      2
#define ERR_CLIENT_LIMIT        9
#define ERR_CLIENT_NOT_FOUND    10
#define ERR_USER_LIMIT          17
#define ERR_USER_NOT_FOUND      18
#define ERR_PERMISSION_DENIED   19

// Confirmation Codes
#define CONF_SUCCESS_DISC 1
#define CONF_SUCCESS_CREATE 2
#define CONF_SUCCESS_UPDATE 3

void logexit(const char *msg);
int addrparse(const char *addrstr, const char *portstr, struct sockaddr_storage *storage);
void addrtostr(const struct sockaddr *addr, char *str, size_t strsize);
int server_sockaddr_init(const char *proto, const char *portstr, struct sockaddr_storage *storage);

#endif