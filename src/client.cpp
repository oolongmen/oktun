
#include <getopt.h>
#include <string.h>

#include "oktun_proxy.h"
#include "oktun_client.h"

#define APP_NAME "oktun_client"

static std::string s_port  = "51024";
static std::string s_rhost = "localhost";
static std::string s_rserv = "51024";
static std::string s_listen_port = "8080";

void ParseHostName(const std::string &s)
{
    size_t pos = s.find(':');

    assert(pos != std::string::npos);

    DLOG("%ld", pos);

    s_rhost = s.substr(0, pos);

    if (s_rhost.empty())
        s_rhost = "localhost";

    s_rserv = s.substr(pos+1);

    assert(!s_rserv.empty());

    DLOG("%s:%s", s_rhost.c_str(), s_rserv.c_str());
}

void PrintUsage()
{
    printf(
        "Usage: " APP_NAME " [options]\n"
        "\n"
        "Options:\n"
        "  -h, --help                     Print this help.\n"
        "  -b, --bind [int]               Local port to bind.\n"
        "  -s, --serveraddr [host:port]   Address of oktun server.\n"
        "  -l, --listenport [int]         Local port to listen for proxy request\n"
        "\n"
    );
}
int main(int argc, char *argv[])
{
    int opt;

    static const struct option long_options[] =
    {
        { "bind", required_argument, 0, 'b' },
        { "serveraddr", required_argument, 0, 's' },
        { "listenport", required_argument, 0, 'l' },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 }
    };

    while ((opt = getopt_long(argc,
                              argv,
                              "hb:l:s:",
                              long_options,
                              NULL)) != -1)
    {
        switch (opt)
        {
            case 'b':
                s_port = optarg;
                break;

            case 'l':
                s_listen_port = optarg;
                break;

            case 's':
                ParseHostName(optarg);
                break;

            case 'h':
                PrintUsage();
                return 0;

            default:
                PrintUsage();
                return -1;
        }
    }

    struct event_base *base = event_base_new();

    oktun::TunnelClient tunnel(base);
    oktun::ProxyServer proxy(base, &tunnel);

    if (tunnel.Bind(s_port) < 0)
    {
        DLOG("open port failed");
        return -1;
    }

    // bind to port
    if (proxy.BindListen(s_listen_port) < 0)
    {
        DLOG("Proxy server bind failed");
        return -1;
    }

    // connect to remote tunnel
    if (tunnel.Connect(s_rhost, s_rserv) < 0)
    {
        DLOG("connect failed");
        return -1;
    }

    event_base_dispatch(base);

    return 0;
}
