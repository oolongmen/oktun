#include <getopt.h>
#include <string.h>

#include "oktun_server.h"

#define APP_NAME "oktun_server"

static std::string s_port  = "51024";
static std::string s_rhost = "localhost";
static std::string s_rserv = "80";

void ParseHostName(const std::string &s)
{
    size_t pos = s.find(':');

    if (pos != std::string::npos)
    {
        s_rhost = s.substr(0, pos);
        s_rserv = s.substr(pos+1);
    }
}

void PrintUsage()
{
    printf(
        "Usage: " APP_NAME " [options]\n"
        "\n"
        "Options:\n"
        "  -h, --help                     Print this help.\n"
        "  -b, --bind [int]               Local port to bind.\n"
        "  -r, --remoteaddr [host:port]   Address of remote server to forward request to.\n"
        "\n"
    );
}

int main(int argc, char *argv[])
{
    int opt;

    static const struct option long_options[] =
    {
        { "bind", required_argument, 0, 'b' },
        { "remoteaddr", required_argument, 0, 'r' },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 }
    };

    while ((opt = getopt_long(argc,
                              argv,
                              "hb:r:",
                              long_options,
                              NULL)) != -1)
    {
        switch (opt)
        {
            case 'b':
                s_port = optarg;
                break;

            case 'r':
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

    if (!base)
    {
        DLOG("new event base failed");
        return -1;
    }

    oktun::TunnelServer srv(base);

    if (srv.BindListen(s_port) < 0)
    {
        DLOG("bind failed");
        return -1;
    }

    if (srv.SetRemoteHost(s_rhost,
                          s_rserv) < 0)
    {
        DLOG("set remote host failed");
        return -1;
    }

    event_base_dispatch(base);
    return 0;
}
