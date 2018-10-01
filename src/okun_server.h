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

#include "kcp/ikcp.h"
#include "oktun.h"
#include "socket/oassocket.h"
#include "buffer/obuffer.h"

OKTUN_BEGIN_NAMESPACE

class ProxyServer
{
public:
    struct Msg
    {
        struct sockaddr_storage addr;
        socklen_t addrlen;
        uint32_t datalen;
        char data[];
    };

    struct Client;

    struct Task
    {
        int sock;
        ikcpcb *kcp;
        struct event *ev[2];
        Buffer buf[2];
    };

    struct Client
    {
        struct sockaddr_storage addr;
        socklen_t addrlen;
        std::map<uint32_t, Task*> m_tasks;
        ProxyServer *server;

        bool Has(uint32_t id)
        {
            const auto &i = m_tasks.find(id);
            if (i == m_tasks.end())
                return false;
            return true;
        }

        Task* Get(uint32_t id)
        {
            return m_tasks[id];
        }
    };

    ProxyServer()
    {
        DLOG("");

        m_base = event_base_new();

        m_ev[0] = 0;
        m_ev[1] = 0;

        m_target_host = "localhost";
        m_target_serv = "80";
    }

    int Open(int port)
    {
        DLOG("");

        struct addrinfo hints, *res;

        memset(&hints, 0, sizeof(hints));

        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE;

        if (getaddrinfo(NULL,
                        std::to_string(port).c_str(),
                        &hints,
                        &res) != 0)
        {
            return -1;
        }

        int sock = -1;
        struct event *ev[2] = { 0, 0 };

        do
        {
            sock = socket(res->ai_family,
                          res->ai_socktype,
                          res->ai_protocol);

            if (sock < 0)
            {
                DLOG("failed");
                break;
            }

            int opt = 1;

            if (setsockopt(sock,
                           SOL_SOCKET,
                           SO_REUSEADDR,
                           &opt, sizeof(opt)) == -1)
            {
                DLOG("failed");
                break;
            }

            // non-blocking
            evutil_make_socket_nonblocking(sock);

            if (bind(sock,
                     res->ai_addr,
                     res->ai_addrlen) < 0)
            {
                DLOG("failed");
                break;
            }

            // read event
            ev[0] = event_new(m_base,
                              sock,
                              EV_READ | EV_PERSIST,
                              SocketReadCB,
                              this);

            if (!ev[0])
            {
                DLOG("failed");
                break;
            }

            // write event
            ev[1] = event_new(m_base,
                              sock,
                              EV_WRITE | EV_PERSIST,
                              SocketWriteCB,
                              this);

            if (!ev[1])
            {
                DLOG("failed");
                break;
            }

            m_sock = sock;

            m_ev[0] = ev[0];
            m_ev[1] = ev[1];

            return 0;

        } while (0);

        if (sock >= 0)
        {
            close(sock);
        }

        if (m_ev[0])
        {
            event_free(m_ev[0]);
            m_ev[0] = 0;
        }

        if (m_ev[1])
        {
            event_free(m_ev[1]);
            m_ev[1] = 0;
        }

        return -1;
    }

    int Connect(const std::string &host, int port)
    {
        DLOG("");

        if (m_sock < 0)
        {
            errno = EBADF;
            return -1;
        }

        struct addrinfo hints, *res;

        memset(&hints, 0, sizeof(hints));

        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;

        if (getaddrinfo(host.c_str(),
                        std::to_string(port).c_str(),
                        &hints,
                        &res) != 0)
        {
            DLOG("failed");
            return -1;
        }

        if (connect(m_sock,
                    res->ai_addr,
                    res->ai_addrlen) < 0)
        {
            DLOG("failed");
            freeaddrinfo(res);
            return -1;
        }

        freeaddrinfo(res);
        return 0;
    }

    void Listen()
    {
        if (m_sock < 0 || !m_base)
            return;

        event_add(m_ev[0], NULL);

        m_timer_ev = evtimer_new(m_base,
                                 UpdateCB,
                                 this);

        struct timeval tv { 0, 20000 };

        event_add(m_timer_ev, &tv);

        event_base_dispatch(m_base);
    }

    // void Process()
    // {
    //     DLOG("");

    //     if (m_kcp_buffer[0].Used())
    //     {
    //         printHex(m_kcp_buffer[0].Head(),
    //                  m_kcp_buffer[0].Used());

    //         m_kcp_buffer[0].Remove(m_kcp_buffer[0].Used());
    //     }
    // }

    ssize_t Send(ikcpcb *kcp, const char *data, size_t datalen)
    {
        DLOG("");

        size_t offset = 0;
        size_t rc = 0;

        while (datalen)
        {
            size_t max = std::min(datalen,
                                  (size_t) 1400);

            if (ikcp_send(kcp,
                          data + offset,
                          max) < 0)
            {
                DLOG("send failed");
                return (!rc) ? -1 : rc;
            }

            offset += max;
            datalen -= max;

            DLOG("send: %ld", max);
        }

        DLOG("send: %s(%ld)", data, datalen);
        return 0;
    }

    int NewClient(std::string &key,
                  struct sockaddr *addr, socklen_t addrlen)
    {
        std::unique_ptr<Client> c(new (std::nothrow) Client);

        if (!c)
            return -1;

        memcpy(&c->addr, addr, addrlen);
        c->addrlen = addrlen;
        c->server = this;

        m_clients.emplace(key, std::move(c));
        return 0;
    }

    Task* NewTask(Client *client, uint32_t conv)
    {
        Task *t = NULL;// = new Task;
        struct addrinfo hints, *res = NULL;

        memset(&hints, 0, sizeof(struct addrinfo));

        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        
        DLOG("%s:%s", 
             m_target_host.c_str(), m_target_serv.c_str());

        if (getaddrinfo(m_target_host.c_str(),
                        m_target_serv.c_str(),
                        &hints,
                        &res) != 0)
        {
            DLOG("getaddrinfo failed");
            return NULL;
        }

        {
            char host[NI_MAXHOST];
            char serv[NI_MAXSERV];

            getnameinfo(res->ai_addr,
                        res->ai_addrlen,
                        host, NI_MAXHOST,
                        serv, NI_MAXSERV,
                        NI_NUMERICHOST);

            DLOG("%s:%s", host, serv);
        }

        do
        {
            t = new (std::nothrow) Task;
                
            if (!t)
            {
                DLOG("new task failed");
                break;
            }

            t->sock = -1;
            t->ev[0] = 0;
            t->ev[1] = 0;

            t->kcp = ikcp_create(conv, client);

            if (!t->kcp)
            {
                DLOG("kcp create failed");
                break;
            }

            t->kcp->output = OutputCB;

            t->sock = socket(res->ai_family,
                             res->ai_socktype,
                             res->ai_protocol);

            if (t->sock < 0)
            {
                break;
            }
            
            // evutil_make_socket_nonblocking(t->sock);

            if (connect(t->sock,
                        res->ai_addr,
                        res->ai_addrlen) < 0)
            {
                DLOG("connect failed: %s", strerror(errno));
                break;
            }

            t->ev[0] = event_new(m_base,
                                 t->sock,
                                 EV_READ | EV_PERSIST,
                                 TaskReadCB,
                                 t);

            if (!t->ev[0])
            {
                DLOG("new event failed");
                break;
            }

            event_add(t->ev[0], NULL);

            t->ev[1] = event_new(m_base,
                                 t->sock,
                                 EV_WRITE | EV_PERSIST,
                                 TaskWriteCB,
                                 t);
            if (!t->ev[1])
            {
                DLOG("new event failed");
                break;
            }

            freeaddrinfo(res);
            return t;

        } while (0);

        if (t)
        {
            if (t->kcp)
                ikcp_release(t->kcp);

            if (t->ev[0])
                event_free(t->ev[0]);

            if (t->ev[1])
                event_free(t->ev[1]);

            delete t;
        }

        freeaddrinfo(res);
        return NULL;
    }

    void ForwardTaskData(Task *t)
    {
        while (1)
        {
            auto &b = t->buf[1];

            if (b.Full())
                break;

            int rc = ikcp_recv(t->kcp,
                               b.Tail(),
                               b.Unused());

            if (rc < 0)
            {
                break;
            }

            b.Commit(rc);
        }

        // forward data event
        event_add(t->ev[1], NULL);
    }

    void ProcessMsg(Msg *msg)
    {
        char host[NI_MAXHOST];
        char serv[NI_MAXSERV];
        std::string key;

        if (getnameinfo((struct sockaddr*) &msg->addr,
                        msg->addrlen,
                        host, NI_MAXHOST,
                        serv, NI_MAXSERV,
                        NI_NUMERICHOST | NI_DGRAM) != 0)
        {
            return;
        }

        key = host;
        key += ':';
        key += serv;

        if (!Has(key))
        {
            if (NewClient(key,
                          (struct sockaddr*) &msg->addr,
                          msg->addrlen) < 0)
            {
                DLOG("new client: %s", key.c_str());
                //TODO: error handle
                return;
            }
        }

        uint32_t conv = ikcp_getconv(msg->data);

        DLOG("%u", conv);

        if (!m_clients[key]->Has(conv))
        {
            DLOG("New Task");

            m_clients[key]->m_tasks[conv] = NewTask(m_clients[key].get(), conv);
        }

        // input data
        ikcp_input(m_clients[key]->Get(conv)->kcp,
                   msg->data,
                   msg->datalen);

        ForwardTaskData(m_clients[key]->Get(conv));
    }

    bool Has(const std::string &key)
    {
        const auto &i = m_clients.find(key);
        if (i == m_clients.end())
            return false;
        return true;
    }

    static uint32_t iClock()
    {
        struct timeval tv;

        evutil_gettimeofday(&tv, NULL);

        return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
    }

    // cb when socket is readable
    static void SocketReadCB(int, short, void *userdata)
    {
        auto *d = static_cast<ProxyServer*>(userdata);

        if (!d)
        {
            return;
        }

        auto &b = d->m_buffer[0];

        if (b.Full())
        {
            return;
        }

        // struct sockaddr_storage addr;
        // socklen_t addrlen = sizeof(addr);

        ssize_t rc = 0;
        Msg *msg = (Msg *) b.Tail();

        msg->addrlen = sizeof(msg->addr);
        size_t maxlen = b.Unused() - sizeof(Msg);

        rc = recvfrom(d->m_sock,
                      msg->data,
                      maxlen,
                      0,
                      (struct sockaddr*) &msg->addr,
                      &msg->addrlen);

        if (rc < 0)
        {
            if (errno == ENOBUFS ||
                errno == EWOULDBLOCK ||
                errno == EAGAIN)
            {
                // try again
                DLOG("try again");
                return;
            }
            //TODO: close conn
            DLOG("%s", strerror(errno));
            return;
        }

        if (!rc)
        {
            //TODO: close conn
            DLOG("rc==0");
            return;
        }

        msg->datalen = rc;
        b.Commit(sizeof(Msg) + rc);

        printHex(b.Head(), b.Used());
        return;
    }

    // cb when socket is writable
    static void SocketWriteCB(int, short, void *userdata)
    {
        auto *d = static_cast<ProxyServer*>(userdata);

        if (!d)
        {
            return;
        }

        auto &b = d->m_buffer[1];

        while (!b.Empty())
        {
            auto *msg = (Msg *) b.Head();

            ssize_t rc = sendto(d->m_sock,
                                msg->data,
                                msg->datalen,
                                0,
                                (struct sockaddr*) &msg->addr,
                                msg->addrlen);

            DLOG("%ld:%d", rc, msg->datalen);

            if (rc < 0)
            {
                if (errno == ENOBUFS ||
                    errno == EWOULDBLOCK ||
                    errno == EAGAIN)
                {
                    return;
                }
            }

            if (!rc)
            {
                break;
            }

            if (rc != msg->datalen)
            {
                DLOG("sendto sent incomplete data");
                continue;
            }

            b.Remove(rc + sizeof(Msg));
        }

        if (b.Empty())
        {
            DLOG("del event");
            event_del(d->m_ev[1]);
        }
    }

    static void UpdateCB(int, short, void *userdata)
    {
        auto *d = static_cast<ProxyServer*>(userdata);

        if (!d)
        {
            return;
        }

        for (auto &i : d->m_clients)
        {
            for (auto &t : i.second->m_tasks)
            {
                ikcp_update(t.second->kcp, iClock());
            }
        }
        // uint32_t now = iClock();
        // uint32_t next = ikcp_check(d->m_kcp, now);
        // struct timeval tv = { 0, (next - now) * 1000 };

        // DLOG("%ld", tv.tv_usec / 1000);

        auto &sb0 = d->m_buffer[0];

        // auto &kb0 = d->m_kcp_buffer[0];
        
        // ikcp_update(d->m_kcp, now);

        if (!sb0.Empty())
        {
            Msg *msg = (Msg *) sb0.Head();

            d->ProcessMsg(msg);
            sb0.Remove(msg->datalen + sizeof(Msg));
        }

        // int sz = ikcp_peeksize(d->m_kcp);

        // if (sz > 0)
        // {
        //     size_t max = std::min((size_t) sz,
        //                           kb0.Unused());

        //     // read from kcp and 
        //     // put into kcp buffer
        //     ikcp_recv(d->m_kcp,
        //               kb0.Tail(),
        //               max);

        //     kb0.Commit(max);
        // }

        // if (kb0.Used() > 0)
        // {
        //     d->Process();
        // }

        struct timeval tv = { 0, 20000 };
        event_add(d->m_timer_ev, &tv);
    }

    static int OutputCB(const char *data, int datalen, ikcpcb *, void *userdata)
    {
        auto *d = static_cast<Client*>(userdata);

        if (!d)
        {
            return -1;
        }

        auto &b = d->server->m_buffer[1];
        auto *msg = (Msg *) b.Tail();

        if (b.Unused() < (size_t) (datalen + sizeof(Msg)))
        {
            DLOG("not enuf buffer");
            errno = EAGAIN;
            return -1;
        }

        memcpy(&msg->addr, &d->addr, d->addrlen);
        msg->addrlen = d->addrlen;
        memcpy(msg->data, data, datalen);
        msg->datalen = datalen;

        b.Commit(datalen + sizeof(Msg));
        event_add(d->server->m_ev[1], NULL);

        return 0;
    }

    static void printHex(const char *data, size_t len)
    {
        DLOG("");

        char tmp[17];

        size_t paddedLen = len + ((~(len - 1)) & 0xf);

        bzero(tmp, sizeof(tmp));

        printf("%08zx  ", (size_t) 0);

        for (size_t n = 0; n < paddedLen; ++n)
        {
            if (n && !(n % 16))
            {
                printf(" %s\n%08zx  ", tmp, n);
                tmp[0] = '\0';
            }

            if (n < len)
            {
                tmp[n % 16] = 
                    (data[n] >= ' ' && data[n] <= '~') 
                    ? data[n] : '.';
                printf("%02x ", (uint8_t) data[n]);
            }
            else
            {
                tmp[n % 16] = '\0';
                printf("   ");
            }
        }

        if (tmp[0] != '\0')
            printf(" %s", tmp);

        printf("\n");
    }

    static void TaskReadCB(int, short, void *userdata)
    {
        DLOG("");

        auto *d = static_cast<Task*>(userdata);

        if (!d)
            return;

        auto &b = d->buf[0];

        if (b.Full())
            return;

        ssize_t rc = 0;

        rc = recv(d->sock,
                  b.Tail(),
                  b.Unused(),
                  0);

        if (rc < 0)
        {
            if (errno == ENOBUFS ||
                errno == EWOULDBLOCK ||
                errno == EAGAIN)
            {
                // try again
                DLOG("try again");
                return;
            }
            //TODO: close conn
            DLOG("%s", strerror(errno));
            return;
        }

        if (rc == 0)
        {
            // TODO: Close conn
            DLOG("rc==0");
            event_del(d->ev[0]);
            return;
        }

        DLOG("%ld", rc);

        b.Commit(rc);
        printHex(b.Head(), b.Used());
        
        while (!b.Empty())
        {
            if (ikcp_send(d->kcp,
                          b.Head(),
                          b.Used()) != 0)
            {
                DLOG("send failed");
                break;
            }

            DLOG("111");
            b.Remove(b.Used());
        }
    }

    static void TaskWriteCB(int, short, void *userdata)
    {
        auto *d = static_cast<Task*>(userdata);

        if (!d)
            return;

        auto &b = d->buf[1];

        while (!b.Empty())
        {
            ssize_t rc = send(d->sock,
                              b.Head(),
                              b.Used(),
                              0);

            if (rc < 0)
            {
                if (errno == ENOBUFS ||
                    errno == EWOULDBLOCK ||
                    errno == EAGAIN)
                {
                    break;
                }
            }

            if (!rc)
            {
                break;
            }

            DLOG("wrote %ld to remote", rc);
            b.Remove(rc);
        }

        if (b.Empty())
        {
            event_del(d->ev[1]);
        }
    }

private:
    int m_sock;

    struct event_base *m_base;

    struct event *m_ev[2];
    Buffer m_buffer[2];

    struct event *m_timer_ev;

    std::map<std::string,
             std::unique_ptr<Client>> m_clients;

    std::string m_target_host;
    std::string m_target_serv;
};

OKTUN_END_NAMESPACE
