#!/bin/sh

case "$1" in
  start)
    echo "Starting aesdsocket..."
    # Check if aesdsocket already running
    if pgrep -x "aesdsocket" > /dev/null
    then
      echo "aesdsocket already running"
    else
      # Start aesdsocket and background it
      /usr/bin/aesdsocket &
      
      # Wait until aesdsocket is actually listening on port 9000
      echo "Waiting for aesdsocket to start listening on port 9000..."
      for i in $(seq 1 10)
      do
        if netstat -an | grep ':9000' | grep LISTEN > /dev/null
        then
          echo "aesdsocket is now listening!"
          break
        fi
        sleep 1
      done

      # Final check
      if ! netstat -an | grep ':9000' | grep LISTEN > /dev/null
      then
        echo "Error: aesdsocket did not start listening on port 9000"
        exit 1
      fi
    fi
    ;;
  stop)
    echo "Stopping aesdsocket..."
    pkill -f "/usr/bin/aesdsocket"
    ;;
  restart)
    $0 stop
    $0 start
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1
esac

exit 0