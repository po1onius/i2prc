#!/bin/sh

cp /tmp/i2prc /root/


IP=$(ip addr | egrep -o '172\.17\.[0-9]{1,3}\.[0-9]{1,3}' | egrep -v '172\.17\.[0-9]{1,3}\.255')

echo ${IP}
PEERS="172.17.0.2,172.17.0.3,172.17.0.4,172.17.0.5,172.17.0.6,172.17.0.7,172.17.0.8,172.17.0.9,172.17.0.10,192.168.31.231"
/root/i2prc ${PEERS}  --floodfill --loglevel=debug --host=${IP}
