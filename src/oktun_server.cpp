#include "oktun_server.h"
#include "oktun_utils.h"

static uint32_t iClock()
{
    struct timeval tv;

    evutil_gettimeofday(&tv, NULL);

    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

OKTUN_BEGIN_NAMESPACE

TunnelServer::TunnelServer(struct event_base *base)
    : m_base(0)
{
    assert(base);

    m_base = base;

    m_ev[0] = 0;
    m_ev[1] = 0;

    SetRemoteHost("localhost", "80");
}

int TunnelServer::BindListen(const std::string &port)
{
    DLOG("%s", port.c_str());

    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL,
                    port.c_str(),
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
                          ReadCB,
                          this);

        if (!ev[0])
        {
            DLOG("failed");
            break;
        }

        event_add(ev[0], NULL);

        // write event
        // ev[1] = event_new(m_base,
        //                   sock,
        //                   EV_WRITE | EV_PERSIST,
        //                   WriteCB,
        //                   this);

        // if (!ev[1])
        // {
        //     DLOG("failed");
        //     break;
        // }

        m_sock = sock;

        m_ev[0] = ev[0];
        m_ev[1] = ev[1];

        m_timer_ev = evtimer_new(m_base,
                                 UpdateCB,
                                 this);

        struct timeval tv { 0, 20000 };

        event_add(m_timer_ev, &tv);

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

int TunnelServer::Connect(const std::string &host, int port)
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

int TunnelServer::NewClient(
        const std::string &key, struct sockaddr *addr, socklen_t addrlen)
{
    std::unique_ptr<Client> c(
            new (std::nothrow) Client(*this));

    if (!c)
    {
        DLOG("new client failed");
        return -1;
    }

    memcpy(&c->addr, addr, addrlen);
    c->addrlen = addrlen;

    m_clients.emplace(key, std::move(c));
    return 0;
}

ssize_t TunnelServer::Client::Write2Task(uint32_t id, const char *data, size_t datalen)
{
    Task *t = Get(id);
    size_t total = 0;

    if (!t)
    {
        DLOG("bad id");
        return -1;
    }

    DLOG("%ld", datalen);

    int rc = ikcp_input(t->kcp,
                        data,
                        datalen);
    
    if (rc < 0)
    {
        DLOG("input failed: %d", rc);
        return -1;
    }

    DLOG("rc=%d", rc);

    auto &b = t->buf[1];

    rc = ikcp_recv(t->kcp,
                   b.Tail(),
                   b.Unused());

    if (rc < 0)
    {
        if (rc == -3)
            DLOG("ikcp_recv need more buffer");
        return -1;
    }

    DLOG("recv: %d", rc);

    b.Commit(rc);

    // forward data event
    event_add(t->ev[1], NULL);

    return total;
}

int TunnelServer::Process(const char*data, int datalen,
        struct sockaddr *addr, socklen_t addrlen)
{
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    char key[NI_MAXHOST + NI_MAXSERV + 1];

    if (getnameinfo(addr,
                    addrlen,
                    host,
                    NI_MAXHOST,
                    serv,
                    NI_MAXSERV,
                    NI_NUMERICHOST | NI_DGRAM) != 0)
    {
        DLOG("getnameinfo failed.");
        return -1;
    }

    if (snprintf(key, sizeof(key), "%s:%s", host, serv) < 0)
    {
        DLOG("snprintf failed.");
        return -1;
    }

    Client *c = Get(key);

    if (!c)
    {
        if (NewClient(key,
                      addr,
                      addrlen) < 0)
        {
            DLOG("new client: %s", key);
            //TODO: error handle
            return -1;
        }

        c = Get(key);
    }

    uint32_t id = ikcp_getconv(data);

    if (!c->Has(id))
    {
        DLOG("create new task: %d", id);

        if (c->NewTask(id, m_remote_addrinfo) < 0)
        {
            DLOG("New Task failed");
            return -1;
        }
    }

    // forward data 2 task
    c->Write2Task(id, data, datalen);
    return datalen;
}

bool TunnelServer::Has(const std::string &key)
{
    return (m_clients.find(key) != m_clients.end());
}

int TunnelServer::SetRemoteHost(const std::string  &host,
                                const std::string &serv)
{
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host.c_str(),
                    serv.c_str(),
                    &hints,
                    &res) != 0)
    {
        DLOG("getaddrinfo failed");
        return -1;
    }

    m_remote_addrinfo = res;
    return 0;
}

std::string TunnelServer::GetRemoteHost()
{
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];

    getnameinfo(m_remote_addrinfo->ai_addr,
                m_remote_addrinfo->ai_addrlen,
                host,
                NI_MAXHOST,
                serv,
                NI_MAXSERV,
                NI_NUMERICHOST);

    DLOG("%s:%s", host, serv);

    std::string s = host;

    s += ":";
    s += serv;

    return s;
}

TunnelServer::Client* TunnelServer::Get(const std::string &key)
{
    if (!Has(key))
        return NULL;

    return m_clients[key].get();
}

// cb when socket is readable
void TunnelServer::ReadCB(int, short, void *userdata)
{
    auto *d = static_cast<TunnelServer*>(userdata);

    assert(d);

    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);

    char tmp[1400];

    int rc = recvfrom(d->m_sock,
                      tmp,
                      1400,
                      0,
                      (struct sockaddr *) &addr,
                      &addrlen);

    if (rc < 0)
    {
        if (errno == EWOULDBLOCK ||
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

    // Utils::HexDump(tmp, rc);
    d->Process(tmp, rc, (struct sockaddr *) &addr, addrlen);
    return;
}

// cb when socket is writable
// void TunnelServer::WriteCB(int, short, void *userdata)
// {
//     auto *d = static_cast<TunnelServer*>(userdata);

//     assert(d);

// }

void TunnelServer::UpdateCB(int, short, void *userdata)
{
    auto *d = static_cast<TunnelServer*>(userdata);

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

    struct timeval tv = { 0, 20000 };
    event_add(d->m_timer_ev, &tv);
}

int TunnelServer::OutputCB(const char *data, int datalen, ikcpcb *, void *userdata)
{
    auto *d = static_cast<Client*>(userdata);

    if (!d)
    {
        return -1;
    }

    int rc = sendto(d->server.m_sock,
                    data,
                    datalen,
                    0,
                    (struct sockaddr*) &d->addr,
                    d->addrlen);

    if (rc < 0)
    {
        if (errno == EWOULDBLOCK ||
            errno == EAGAIN)
        {
            DLOG("try again");
            return -1;
        }

        DLOG("error: %s", strerror(errno));
        return -1;
    }

    if (rc != datalen)
    {
        DLOG("%d:%d", rc, datalen);
        return -1;
    }

    // Utils::HexDump(data, datalen);
    DLOG("Send %d", rc);
    return 0;
}

TunnelServer::Task::Task()
{
    sock = -1;
    kcp = 0;

    ev[0] = 0;
    ev[1] = 0;
}

TunnelServer::Task::~Task()
{
    if (kcp)
    {
        ikcp_release(kcp);
    }

    if (ev[0])
    {
        event_free(ev[0]);
    }

    if (ev[1])
    {
        event_free(ev[1]);
    }

    if (sock)
    {
        close(sock);
    }
}

TunnelServer::Client::Client(TunnelServer &s)
    : server(s)
{
    addrlen = sizeof(addr);
    memset(&addr, 0, addrlen);
}

TunnelServer::Client::~Client()
{
    for (auto &it : m_tasks)
    {
        Task *t = it.second.get();

        if (t->kcp)
        {
            ikcp_release(t->kcp);
        }

        if (t->sock)
        {
            close(t->sock);
        }
    }
}

void TunnelServer::Client::TaskReadCB(int, short, void *userdata)
{
    auto *task = static_cast<Task*>(userdata);

    if (!task)
    {
        DLOG("bad id");
        return;
    }

    auto &b = task->buf[0];

    if (b.Full())
    {
        DLOG("buffer full");
        return;
    }

    ssize_t rc = 0;

    rc = recv(task->sock,
              b.Tail(),
              b.Unused(),
              0);

    DLOG("%ld", rc);

    if (rc < 0)
    {
        if (errno == EWOULDBLOCK ||
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
        if (!task->IsClosing)
        {
            DLOG("send close signal to client");

            task->IsClosing = true;
            ikcp_send(task->kcp, NULL, 0); //send empty packet

            struct timeval tv = { 0, 20000 };
            event_add(task->ev[0], &tv);
        }
        else
        {
            if (iqueue_is_empty(&task->kcp->snd_queue))
            {
                // close when msg has been sent
                task->OnCloseCB(task->kcp->conv, task->userdata); 
            }
        }

        return;
    }

    b.Commit(rc);
    Utils::HexDump(b.Head(), rc);
    
    while (!b.Empty())
    {
        if (ikcp_send(task->kcp,
                      b.Head(),
                      b.Used()) != 0)
        {
            DLOG("send failed");
            break;
        }

        b.Remove(b.Used());
    }
}

bool TunnelServer::Client::Has(uint32_t id)
{
    return (m_tasks.find(id) != m_tasks.end());
}

TunnelServer::Task* TunnelServer::Client::Get(uint32_t id)
{
    if (!Has(id))
        return NULL;

    return m_tasks[id].get();
}

int TunnelServer::Client::NewTask(
        uint32_t id, const struct addrinfo *info)
{
    std::unique_ptr<Task> t(
        new (std::nothrow) Task);

    if (!t)
    {
        DLOG("new task failed");
        return -1;
    }

    t->IsClosing = false;

    t->kcp = ikcp_create(id, this);

    if (!t->kcp)
    {
        DLOG("kcp create failed");
        return -1;
    }

    t->kcp->output = OutputCB;

    t->sock = socket(info->ai_family,
                     info->ai_socktype,
                     info->ai_protocol);

    if (t->sock < 0)
    {
        return -1;
    }
    
    if (connect(t->sock,
                info->ai_addr,
                info->ai_addrlen) < 0)
    {
        DLOG("connect failed: %s", strerror(errno));
        return -1;
    }

    evutil_make_socket_nonblocking(t->sock);

    t->ev[0] = event_new(server.m_base,
                         t->sock,
                         EV_READ | EV_PERSIST,
                         TaskReadCB,
                         t.get());

    if (!t->ev[0])
    {
        DLOG("new event failed");
        return -1;
    }

    event_add(t->ev[0], NULL);

    t->ev[1] = event_new(server.m_base,
                         t->sock,
                         EV_WRITE | EV_PERSIST,
                         TaskWriteCB,
                         t.get());
    if (!t->ev[1])
    {
        DLOG("new event failed");
        return -1;
    }

    t->OnCloseCB = TaskCloseCB;
    t->userdata = this;

    m_tasks.emplace(t->kcp->conv, std::move(t));
    
    return 0;
}

void TunnelServer::Client::RemoveTask(uint32_t id)
{
    if (!Has(id))
        return;

    DLOG("erase id: %d", id);
    m_tasks.erase(id);
    DLOG("remaining task: %ld", m_tasks.size());
}

void TunnelServer::Client::TaskWriteCB(int, short, void *userdata)
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
            if (errno == EWOULDBLOCK ||
                errno == EAGAIN)
            {
                break;
            }
            DLOG("send failed: %s", strerror(errno));
            return;
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

void TunnelServer::Client::TaskCloseCB(uint32_t id, void *userdata)
{
    auto *d = static_cast<Client*>(userdata);

    if (!d)
        return;

    d->RemoveTask(id);
}

OKTUN_END_NAMESPACE
