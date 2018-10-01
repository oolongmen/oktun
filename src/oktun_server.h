#ifndef OKTUN_SERVER_H
#define OKTUN_SERVER_H

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

//libevent
#include <event2/event.h>

//kcp ARQ
#include "kcp/ikcp.h"

#include "oktun.h"
#include "oktun_buffer.h"
#include "oktun_itunnel.h"

OKTUN_BEGIN_NAMESPACE

class TunnelServer
{
public:
    struct Task
    {
        Task();
        ~Task();

        int sock;
        ikcpcb *kcp;
        struct event *ev[2];
        Buffer buf[2];
        bool IsClosing;

        void *userdata;
        void (*OnCloseCB)(uint32_t, void*userdata);
    };

    // tunnel client
    struct Client
    {
        Client(TunnelServer &s);
        ~Client();

        struct sockaddr_storage addr;
        socklen_t addrlen;

        std::map<uint32_t,
                 std::unique_ptr<Task>> m_tasks;

        TunnelServer &server;

        bool Has(uint32_t id);

        Task* Get(uint32_t id);

        void RemoveTask(uint32_t id);

        int NewTask(uint32_t id, const struct addrinfo *addrinfo);

        ssize_t Write2Task(uint32_t id, const char *data, size_t datalen);

        static void TaskCloseCB(uint32_t id, void *userdata);

        static void TaskReadCB(int, short, void *userdata);

        static void TaskWriteCB(int, short, void *userdata);
    };

    TunnelServer(struct event_base *base);

    int Open(int port);

    int Connect(const std::string &host, int port);

    int BindListen(const std::string &port);

    bool Has(const std::string &key);

    Client* Get(const std::string &key);

    int NewClient(const std::string &key,
                  struct sockaddr *addr, socklen_t addrlen);

    int Process(const char *data, int datalen, struct sockaddr *addr, socklen_t addrlen);

    int SetRemoteHost(const std::string &host, const std::string &serv);

    std::string GetRemoteHost();

    // cb when socket is readable
    static void ReadCB(int, short, void *userdata);

    // cb when socket is writable
    // static void WriteCB(int, short, void *userdata);

    static void UpdateCB(int, short, void *userdata);

    static int OutputCB(const char *data, int datalen, ikcpcb *, void *userdata);

private:
    int m_sock;

    struct event_base *m_base;

    struct event *m_ev[2];
    // Buffer m_buffer[2];

    struct event *m_timer_ev;

    std::map<std::string,
             std::unique_ptr<Client>> m_clients;

    struct addrinfo *m_remote_addrinfo;
};

OKTUN_END_NAMESPACE

#endif
