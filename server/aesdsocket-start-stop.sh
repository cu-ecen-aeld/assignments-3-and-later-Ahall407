#!/bin/sh

case "$1" in
    start)
        echo "Starting aesdsocket"
        start-stop-daemon -S -n aesdsocket -a aesdsocket -- -d

        echo "Waiting for aesdsocket to be listening on port 9000..."
        tries=0
        while [ $tries -lt 5 ]; do
            if netstat -an | grep ':9000' | grep LISTEN > /dev/null; then
                echo "aesdsocket is now listening!"
                break
            fi
            sleep 1
            tries=$((tries+1))
        done

        if [ $tries -eq 5 ]; then
            echo "ERROR: aesdsocket did not start listening after 5 seconds!"
        fi
        ;;
    stop)
        echo "Stopping aesdsocket"
        start-stop-daemon -K -n aesdsocket
        ;;
    *)
        echo "Usage: $0 {start|stop}"
        exit 1
        ;;
esac