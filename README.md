TCP over KCP Tunnel.

# Dependencies
```
c++11
libevent2
```

# Build
```
mkdir  build
cd  build; cmake ..
make
```

# Usage

`oktun_server`

```
Usage: oktun_server [options]

Options:
  -h, --help                     Print this help.
  -b, --bind [int]               Local port to bind.
  -r, --remoteaddr [host:port]   Address of remote server to forward request to.

```

`oktun_client`

```
Usage: oktun_client [options]

Options:
  -h, --help                     Print this help.
  -b, --bind [int]               Local port to bind.
  -s, --serveraddr [host:port]   Address of oktun server.
  -r, --proxyport [int]          Local port to listen for proxy request
```
