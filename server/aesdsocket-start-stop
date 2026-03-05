#!/bin/sh

DAEMON=/usr/bin/aesdsocket
NAME=aesdsocket
PIDFILE=/var/run/$NAME.pid

start() {
    echo "Starting $NAME"
    start-stop-daemon -S -n "$NAME" -p "$PIDFILE" -m -b --startas "$DAEMON" -- -d
}

stop() {
    echo "Stopping $NAME"
    start-stop-daemon -K -n "$NAME" -p "$PIDFILE" -s TERM
}

case "$1" in
    start)
        start
        ;;
    stop)
        stop
        ;;
    restart)
        stop
        start
        ;;
    *)
        echo "Usage: $0 {start|stop|restart}"
        exit 1
        ;;
esac

exit $?
