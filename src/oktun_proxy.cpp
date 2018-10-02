#include "oktun_proxy.h"
#include "oktun_utils.h"

OKTUN_BEGIN_NAMESPACE

ProxyServer::Client::Client(ProxyServer &s)
    : server(s)
{
    id = 0;
    sock = -1;

    ev[0] = 0;
    ev[1] = 0;

    userdata = 0;
    OnCloseCB = 0;

    buf[0].Resize(65536);
    buf[1].Resize(65536);
}

ProxyServer::Client::~Client()
{
    if (ev[0])
        event_free(ev[0]);

    if (ev[1])
        event_free(ev[1]);

    if (sock)
        close(sock);
}

ProxyServer::ProxyServer(
        struct event_base *base, iTunnel *tun)
    : m_ev_base(0),
      m_tunnel(0)
{
    assert(tun);
    assert(base);

    m_tunnel = tun;
    m_ev_base = base;

    DLOG("%p", tun);
}

int ProxyServer::BindListen(const std::string &port)
{
    struct addrinfo hints, *res = NULL;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    if (getaddrinfo(NULL,
                    port.c_str(),
                    &hints,
                    &res) != 0)
    {
        DLOG("getaddrinfo failed");
        return -1;
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
        m_sock = socket(res->ai_family,
                      res->ai_socktype,
                      res->ai_protocol);
        
        if (m_sock < 0)
        {
            DLOG("new socket failed");
            break;
        }

        int opt = 1;

        // reuse socket
        if (setsockopt(m_sock,
                       SOL_SOCKET,
                       SO_REUSEADDR,
                       &opt, sizeof(opt)) == -1)
        {
            DLOG("failed");
            break;
        }

        // bind
        if (bind(m_sock,
                 res->ai_addr,
                 res->ai_addrlen) < 0)
        {
            DLOG("bind failed");
            break;
        }

        // new conn event
        m_ev = event_new(m_ev_base,
                       m_sock,
                       EV_READ | EV_PERSIST,
                       NewConnCB,
                       this);

        if (!m_ev)
        {
            DLOG("new event failed");
            break;
        }

        event_add(m_ev, NULL);

        if (listen(m_sock, 20) < 0)
        {
            DLOG("listen failed");
            break;
        }

        evutil_make_socket_nonblocking(m_sock);

        freeaddrinfo(res);
        return 0;

    } while (0);

    if (m_ev)
    {
        event_free(m_ev);
        m_ev = 0;
    }

    if (m_sock >= 0)
    {
        close(m_sock);
        m_sock = -1;
    }

    freeaddrinfo(res);
    return -1;
}

bool ProxyServer::Has(uint32_t id)
{
    return (m_clients.find(id) != m_clients.end());
}

ProxyServer::Client* ProxyServer::Get(uint32_t id)
{
    return m_clients[id].get();
}

int ProxyServer::Accept()
{
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);

    int sock = accept(m_sock,
                      (struct sockaddr *) &addr,
                      &addrlen);

    if (sock < 0)
    {
        DLOG("accept failed:%s", strerror(errno));
        return -1;
    }

    evutil_make_socket_nonblocking(sock);

    // add new tunnel client
    uint32_t id = 
        m_tunnel->NewClient(TunnelReadCB, TunnelCloseCB, this);

    if (!id)
    {
        DLOG("add new tunnel client failed");
        return -1;
    }

    do
    {
        std::unique_ptr<Client> c(
                new (std::nothrow) Client(*this));

        if (!c)
        {
            DLOG("new client failed");
            break;
        }


        c->id = id;
        c->sock = sock;
        c->addr = addr;
        c->addrlen = addrlen;
        c->OnCloseCB = ClientCloseCB;
        c->userdata = this;

        c->ev[0] = event_new(m_ev_base,
                             sock,
                             EV_READ | EV_PERSIST,
                             ClientReadCB,
                             c.get());

        if (!c->ev[0])
        {
            DLOG("new event failed");
            break;
        }

        event_add(c->ev[0], NULL);

        c->ev[1] = event_new(m_ev_base,
                             sock,
                             EV_WRITE | EV_PERSIST,
                             ClientWriteCB,
                             c.get());

        if (!c->ev[1])
        {
            DLOG("new event failed");
            break;
        }

        DLOG("new client: %d", id);

        m_clients.emplace(id, std::move(c));

        return 0;

    } while (0);

    if (id)
    {
        //Remove tunnel client
        m_tunnel->RemoveClient(id);
    }

    DLOG("new client failed: %d", id);
    return -1;
}

void ProxyServer::RemoveClient(uint32_t id)
{
    if (!Has(id))
    {
        DLOG("bad id");
        return;
    }

    std::unique_ptr<Client> c(
        std::move(m_clients[id]));

    DLOG("remove client: %d", id);
    m_clients.erase(id);
    DLOG("remaining clients: %ld", m_clients.size());

    m_tunnel->RemoveClient(c->id);
}

ssize_t ProxyServer::Write2Client(
        uint32_t id, const char *data, size_t datalen)
{
    auto *c = Get(id);

    if (!c)
    {
        DLOG("bad id");
        return -1;
    }


    auto &b = c->buf[1];

    if (b.Unused() < datalen)
    {
        DLOG("no buf");
        errno = ENOBUFS;
        return -1;
    }

    memcpy(b.Tail(), data, datalen);
    b.Commit(datalen);

    event_add(c->ev[1], NULL); // add write event
    return datalen;
}

ssize_t ProxyServer::Write2Tunnel(
        uint32_t id, const char *data, size_t datalen)
{
    if (!m_tunnel)
        return -1;

    // forward data from client to tunnel
    return m_tunnel->Write(id, data, datalen);
}

void ProxyServer::NewConnCB(int, short, void *userdata)
{
    auto *d = static_cast<ProxyServer*>(userdata);

    assert(d);
    
    d->Accept();
}

ssize_t ProxyServer::TunnelReadCB(
        uint32_t id, const char *data, size_t datalen, void *userdata)
{
    auto *d = static_cast<ProxyServer*>(userdata);

    assert(d);

    if (!d->Has(id))
    {
        DLOG("no id: %d", id);
        return -1;
    }

    // forward data from tunnel to client
    return d->Write2Client(id, data, datalen);
}

void ProxyServer::TunnelCloseCB(uint32_t id, void *userdata)
{
    auto *d = static_cast<ProxyServer*>(userdata);

    assert(d);

    d->RemoveClient(id);
}

void ProxyServer::ClientReadCB(int, short, void *userdata)
{
    auto *d = static_cast<Client*>(userdata);

    assert(d);

    auto &b = d->buf[0];

    if (b.Full())
    {
        DLOG("Full");
        return;
    }

    ssize_t rc = 0;

    rc = recv(d->sock,
              b.Tail(),
              b.Unused(),
              0);

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
        // TODO: Close conn
        if (d->OnCloseCB)
            d->OnCloseCB(d->id, d->userdata);
        return;
    }

    DLOG("id: %d, Recv: %ld:%ld", d->id, b.Used(), rc);

    b.Commit(rc);
    // Utils::HexDump(b.Head(), b.Used());
    
    while (!b.Empty())
    {
        // forward data to tunnel
        rc = d->server.Write2Tunnel(d->id,
                                    b.Head(),
                                    b.Used());
        if (rc < 0)
        {
            DLOG("write2tunnel failed");
            break;
        }

        b.Remove(rc);
    }
}

void ProxyServer::ClientWriteCB(int, short, void *userdata)
{
    auto *d = static_cast<Client*>(userdata);

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
        }

        if (!rc)
        {
            break;
        }

        DLOG("wrote %ld to remote", rc);
        // Utils::HexDump(b.Head(), rc);
        b.Remove(rc);
    }

    if (b.Empty())
    {
        event_del(d->ev[1]);
        DLOG("del event");
    }
}

void ProxyServer::ClientCloseCB(uint32_t id, void *userdata)
{
    auto *d = static_cast<ProxyServer*>(userdata);

    if (!d)
        return;

    d->RemoveClient(id);
}

OKTUN_END_NAMESPACE
