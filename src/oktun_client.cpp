#include "oktun_client.h"
#include "oktun_utils.h"

static uint32_t iClock()
{
    struct timeval tv;

    evutil_gettimeofday(&tv, NULL);

    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

OKTUN_BEGIN_NAMESPACE

TunnelClient::TunnelClient(struct event_base *base)
    : m_base(base)
{
    assert(m_base);

    m_sock = -1;

    m_ev[0] = 0;
    m_ev[1] = 0;

    m_timer_ev = 0;

    m_id_counter = 0;
}

TunnelClient::~TunnelClient()
{
    ;
}

int TunnelClient::Bind(const std::string &port)
{
    DLOG("");

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
        DLOG("getaddrinfo failed");
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

        return 0;

    } while (0);

    if (sock >= 0)
    {
        close(sock);
    }

    if (ev[0])
    {
        event_free(ev[0]);
        ev[0] = 0;
    }

    if (ev[1])
    {
        event_free(ev[1]);
        ev[1] = 0;
    }

    return -1;
}

int TunnelClient::Connect(
        const std::string &host, const std::string &serv)
{
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
                    serv.c_str(),
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

    // add tunnel read event
    event_add(m_ev[0], NULL);

    m_timer_ev = evtimer_new(m_base,
                             UpdateCB,
                             this);

    struct timeval tv { 0, 20000 };

    // add timer event
    event_add(m_timer_ev, &tv);

    freeaddrinfo(res);
    return 0;
}

bool TunnelClient::Has(uint32_t id)
{
    return (m_clients.find(id) != m_clients.end());
}

TunnelClient::Client* TunnelClient::Get(uint32_t id)
{
    if (!Has(id))
        return NULL;

    return m_clients[id].get();
}

uint32_t TunnelClient::NewClient(OnReadCB read_cb, OnCloseCB close_cb, void *userdata)
{
    int retry = 10;
    uint32_t id = 0;

    //try get unused id
    while (--retry)
    {
        id = (m_id_counter) ? m_id_counter++
                            : ++m_id_counter;

        if (!Has(id))
        {
            break;
        }
    }

    std::unique_ptr<Client> c(
        new (std::nothrow) Client);

    if (!c)
    {
        DLOG("failed");
        return 0;
    }

    c->id = id;
    c->kcp = ikcp_create(c->id, this);
    c->kcp->output = OutputCB;
    c->on_read_cb = read_cb;
    c->on_close_cb = close_cb;
    c->cb_userdata = userdata;
    c->buf.Resize(65536);

    m_clients.emplace(c->id, std::move(c));

    DLOG("0x%08x", id);
    return id;
}

void TunnelClient::RemoveClient(uint32_t id)
{
    if (!Has(id))
        return;

    std::unique_ptr<Client> c(
        std::move(m_clients[id]));

    DLOG("remove: %d", id);
    m_clients.erase(id);
    DLOG("remaining client: %ld", m_clients.size());

    ikcp_release(c->kcp);
}

ssize_t TunnelClient::Write(uint32_t id, const char *data, size_t datalen)
{
    size_t written = 0;
    size_t rc = 0;

    Client *c = Get(id);

    if (!c)
    {
        DLOG("bad id");
        return -1;
    }

    while (datalen)
    {
        size_t max = std::min(datalen,
                              (size_t) 1400);

        if (ikcp_send(c->kcp,
                      data + written,
                      max) < 0)
        {
            DLOG("send failed");
            return (!rc) ? -1 : rc;
        }

        datalen -= max;
        written += max;

        DLOG("send: %ld", max);
    }

    DLOG("id: %d, written: %ld", id, written);
    return written;
}

ssize_t TunnelClient::Read(uint32_t, char *, size_t)
{
    return 0;
}

ssize_t TunnelClient::Process(const char *data, size_t datalen)
{
    uint32_t id = ikcp_getconv(data);

    Client *c = Get(id);

    DLOG("datalen: %ld", datalen);
    // Utils::HexDump(data, datalen);

    if (!c)
    {
        DLOG("bad id");
        return -1;
    }

    int rc = ikcp_input(c->kcp,
                        data,
                        datalen);
    if (rc < 0)
    {
        DLOG("input failed: %d", rc);
        return -1;
    }

    // forward data 2 client
    ForwardData2Client(id);

    return datalen;
}

void TunnelClient::ForwardData2Client(uint32_t id)
{
    Client *c = Get(id);

    if (!c)
    {
        DLOG("bad id");
        return;
    }

    auto &b = c->buf;

    int rc = ikcp_recv(c->kcp,
                       b.Tail(),
                       b.Unused());

    if (rc < 0)
    {
        if (rc == -3)
            DLOG("need more buf");
        return;
    }

    if (rc == 0)
    {
        DLOG("close signal");
        c->on_close_cb(c->id, c->cb_userdata);
        return;
    }

    b.Commit(rc);

    DLOG("%d:%ld", rc, b.Used());

    if (c->on_read_cb(c->id,
                      b.Head(),
                      rc,
                      c->cb_userdata) < 0)
    {
        DLOG("write failed");
        return;
    }

    DLOG("forward: %d", rc);
    b.Remove(rc);
}

// cb when data in socket 
void TunnelClient::ReadCB(int, short, void *userdata)
{
    auto *d = static_cast<TunnelClient*>(userdata);

    assert(d);

    char tmp[1400];
    int rc = 0;

    rc = recv(d->m_sock,
              tmp,
              1400,
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

    d->Process(tmp, rc);
    return;
}

// cb when socket is writable
//void TunnelClient::WriteCB(int, short, void *userdata)
//{
//    auto *d = static_cast<TunnelClient*>(userdata);

//    assert(d);

//    auto &b = d->m_buffer[1];

//}

// periodic timer to update kcp
void TunnelClient::UpdateCB(int, short, void *userdata)
{
    auto *d = static_cast<TunnelClient*>(userdata);

    assert(d);

    for (auto &it : d->m_clients)
    {
        Client *c = it.second.get();

        ikcp_update(c->kcp, iClock());

        if (ikcp_peeksize(c->kcp) < 0)
            continue;

        d->ForwardData2Client(c->id);
    }

    struct timeval tv = { 0, 20000 };
    event_add(d->m_timer_ev, &tv);
}

int TunnelClient::OutputCB(
        const char *data, int datalen, ikcpcb *, void *userdata)
{
    auto *d = static_cast<TunnelClient*>(userdata);

    assert(d);

    int rc = send(d->m_sock,
                  data,
                  datalen,
                  0);

    if (rc < 0)
    {
        if (errno == EWOULDBLOCK ||
            errno == EAGAIN)
        {
            DLOG("try again");
            //try again
            return -1;
        }

        DLOG("%s", strerror(errno));
        return -1;
    }

    if (rc != datalen)
    {
        DLOG("%d:%d", rc, datalen);
        return -1;
    }

    DLOG("%d", datalen);
    return 0;
}

OKTUN_END_NAMESPACE
