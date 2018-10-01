#ifndef OKTUN_CLIENT_H
#define OKTUN_CLIENT_H

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <algorithm>
#include <string>
#include <map>
#include <list>
#include <vector>
#include <queue>

//libevent
#include <event2/event.h>

//kcp ARQ
#include "kcp/ikcp.h"

#include "oktun.h"
#include "oktun_buffer.h"
#include "oktun_itunnel.h"

OKTUN_BEGIN_NAMESPACE

class TunnelClient 
    : public iTunnel
{
public:
    struct Client
    {
        uint32_t id;
        ikcpcb *kcp;
        OnReadCB on_read_cb;
        OnCloseCB on_close_cb;
        void *cb_userdata;
        Buffer buf;
    };

    TunnelClient(struct event_base *base);

    virtual ~TunnelClient();

    // bind to port
    int Bind(const std::string &port);

    // connect to remote
    int Connect(const std::string &host, const std::string &serv);

    // check if client id exists
    bool Has(uint32_t id);

    // get client
    Client* Get(uint32_t id);

    // new client
    virtual uint32_t NewClient(OnReadCB read_cb, OnCloseCB close_cb, void *userdata);

    // remove client
    virtual void RemoveClient(uint32_t id);

    // write to tunnel
    virtual ssize_t Write(uint32_t id, const char *data, size_t datalen);

    // read from tunnel
    virtual ssize_t Read(uint32_t, char *, size_t);

    // process data
    ssize_t Process(const char *data, size_t datalen);

    void ForwardData2Client(uint32_t id);

    // cb when data in socket 
    static void ReadCB(int, short, void *userdata);

    // cb when socket is writable
    // static void WriteCB(int, short, void *userdata);

    // periodic timer to update kcp
    static void UpdateCB(int, short, void *userdata);

    static int OutputCB(const char *data, int datalen, ikcpcb *, void *userdata);

private:
    int m_sock;

    struct event_base *m_base;

    struct event *m_ev[2];
    // Buffer m_buffer[2];
    std::queue<std::vector<char>> m_queue[2];

    struct event *m_timer_ev;

    int m_id_counter;

    std::map<uint32_t,
             std::unique_ptr<Client>> m_clients;
};

OKTUN_END_NAMESPACE

#endif
