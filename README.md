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

ex:

Remote PC @ 11.22.33.44
Target Webserver @ port #8080

Remote PC
```
./oktun_server -b 51024 -r :8080
```

Client PC
```
./oktun_client -b 51024 -r 8888 -s 11.22.33.44:51024
```

Connect to target webserver via http://localhost:8888 from client pc

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
