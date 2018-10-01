#ifndef OKTUN_ITUNNEL_H
#define OKTUN_ITUNNEL_H

#include <stdint.h>
#include <stdlib.h>

#include "oktun.h"

OKTUN_BEGIN_NAMESPACE

class iTunnel
{
public:
    virtual ~iTunnel() {}

    // cb when got data from tunnel
    typedef ssize_t (*OnReadCB)(uint32_t, const char*, size_t, void*);

    // cb when got close signal from remote server
    typedef void (*OnCloseCB)(uint32_t, void*);

    // add new tunnel client
    virtual uint32_t NewClient(OnReadCB read_cb, OnCloseCB close_cb, void *userdata) = 0;

    // remove tunnel client
    virtual void RemoveClient(uint32_t id) = 0;

    // write data to tunnel
    virtual ssize_t Write(uint32_t id, const char *data, size_t datalen) = 0;

    // read data from tunnel
    virtual ssize_t Read(uint32_t id, char *data, size_t datalen) = 0;
};

OKTUN_END_NAMESPACE

#endif
