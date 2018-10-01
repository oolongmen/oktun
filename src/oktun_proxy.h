#ifndef OKTUN_PROXYSERVER_H
#define OKTUN_PROXYSERVER_H

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <map>
#include <memory>

//libevent
#include <event2/event.h>

#include "oktun.h"
#include "oktun_itunnel.h"
#include "oktun_buffer.h"

OKTUN_BEGIN_NAMESPACE

class ProxyServer
{
public:
    struct Client
    {
        Client(ProxyServer &s);
        ~Client();

        uint32_t id;

        int sock;
        struct sockaddr_storage addr;
        socklen_t addrlen;

        struct event *ev[2];
        Buffer buf[2];

        void *userdata;
        void (*OnCloseCB)(uint32_t, void *userdata);

        ProxyServer &server;
    };

    ProxyServer(struct event_base *base, iTunnel *tun);

    int BindListen(const std::string &port);

    bool Has(uint32_t id);

    Client* Get(uint32_t id);

    int Accept();

    void RemoveClient(uint32_t id);

    ssize_t Write2Client(uint32_t id, const char *data, size_t datalen);

    ssize_t Write2Tunnel(uint32_t id, const char *data, size_t datalen);

    static void NewConnCB(int, short, void *userdata);

    static ssize_t TunnelReadCB(uint32_t id, const char *data, size_t datalen, void *userdata);

    static void TunnelCloseCB(uint32_t id, void *userdata);

    static void ClientReadCB(int, short, void *userdata);

    static void ClientWriteCB(int, short, void *userdata);

    static void ClientCloseCB(uint32_t, void *userdata);

    friend Client;

private:
    int m_sock;

    struct event *m_ev;
    struct event_base *m_ev_base;

    std::map<uint32_t,
             std::unique_ptr<Client>> m_clients;

    iTunnel *m_tunnel;
};

OKTUN_END_NAMESPACE

#endif
