# Telnet-probe

- start telnet server in docker
  - xinted 
  - telnet
  - telnetd

`/etc/xinted.d/telnet` cotains line:
```
server = /usr/bin/telnetd
```

test inside container if it runs on the port 23 
```bash
netstat -tulnp | grep 23
```

when its running run docker exposing port 23
then try from different device run this script (edit ip)


