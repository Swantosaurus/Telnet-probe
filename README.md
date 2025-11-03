# Telnet-probe

- start telnet server in docker
  - xinted 
  - telnet
  - telnetd

`/etc/xinetd.d/telnet` cotains line:
```
service telnet
{
    disable         = no
    flags           = REUSE
    socket_type     = stream
    wait            = no
    user            = root
    server          = /usr/sbin/telnetd
    log_on_failure  += USERID
}
```

test inside container if it runs on the port 23 
```bash
netstat -tulnp | grep 23
```



when its running run docker exposing port 23
then try from different device run this script (edit ip)


