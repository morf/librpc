#include "rpc-private.h"

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/queue.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

static void connection_event_cb(struct bufferevent *bev __attribute__((unused)), short events, void *arg)
{
    struct rpc_connection *connection = arg;

    if (events & BEV_EVENT_CONNECTED) {
        connection->cb(connection, rpc_connect_event, NULL, connection->cbarg);
    } else if (events & BEV_EVENT_ERROR) {
        connection->cb(connection, rpc_error_event, NULL, connection->cbarg);
    } else if (events & BEV_EVENT_EOF) {
        connection->cb(connection, rpc_disconnect_event, NULL, connection->cbarg);
    }
}

static void connection_write_cb(struct bufferevent *bev __attribute__((unused)), void *arg)
{
    struct rpc_connection *connection = arg;
    struct evbuffer *output = bufferevent_get_output(connection->evbuff);

    if (evbuffer_get_length(output) == 0) {
        connection->cb(connection, rpc_empty_output_event, NULL, connection->cbarg);
    }
}

static void connection_read_cb(struct bufferevent *bev, void *arg)
{
    struct rpc_connection *connection = arg;
    struct evbuffer *input = bufferevent_get_input(connection->evbuff);

    if (evbuffer_get_length(input) < sizeof(struct rpc_message_header)) {
        return;
    }

    struct rpc_message_header *header = (struct rpc_message_header *) evbuffer_pullup(input, sizeof(struct rpc_message_header));
    size_t message_size = sizeof(struct rpc_message_header) + header->size + header->meta_size;

    if (evbuffer_get_length(input) == sizeof(struct rpc_message_header)) {
        if (header->magic != RPC_MAGIC) {
            bufferevent_disable(connection->evbuff, EV_READ | EV_WRITE);
            connection->cb(connection, rpc_error_event, NULL, connection->cbarg);

            return;
        }

        if ((header->size + header->meta_size) > 0) {
            bufferevent_setwatermark(connection->evbuff, EV_READ, message_size, message_size);
            evbuffer_expand(input, message_size);
        } else {
            connection->cb(connection, rpc_input_event, header, connection->cbarg);

            evbuffer_drain(input, message_size);
            bufferevent_setwatermark(bev, EV_READ, sizeof(struct rpc_message_header), sizeof(struct rpc_message_header));
        }
    } else if (evbuffer_get_length(input) == message_size) {
        header = (struct rpc_message_header *) evbuffer_pullup(input, message_size);
        connection->cb(connection, rpc_input_event, header, connection->cbarg);

        evbuffer_drain(input, message_size);
        bufferevent_setwatermark(bev, EV_READ, sizeof(struct rpc_message_header), sizeof(struct rpc_message_header));
    }
}

static void rpc_server_accept_cb(struct evconnlistener *listener __attribute__((unused)),
    evutil_socket_t fd, struct sockaddr *addr __attribute__((unused)), int len __attribute__((unused)), void *arg)
{
    struct rpc_server *server = arg;
    struct rpc_connection *connection = calloc(1, sizeof(struct rpc_connection));

    TAILQ_INSERT_TAIL(&server->connections, connection, entries);

    connection->server = server;
    connection->cb = server->cb;
    connection->cbarg = server->cbarg;

    connection->evbuff = bufferevent_socket_new(server->evbase, fd, BEV_OPT_CLOSE_ON_FREE);

    bufferevent_setcb(connection->evbuff, connection_read_cb, connection_write_cb, connection_event_cb, connection);
    bufferevent_enable(connection->evbuff, EV_READ | EV_WRITE);

    bufferevent_setwatermark(connection->evbuff, EV_READ, sizeof(struct rpc_message_header), sizeof(struct rpc_message_header));

    connection->cb(connection, rpc_connect_event, NULL, connection->cbarg);
}

int rpc_server_init(struct rpc_server *server, struct event_base *evbase, struct evconnlistener *evlistener, rpc_connection_cb cb, void *cbarg)
{
    memset(server, 0, sizeof(struct rpc_server));
    server->evbase = evbase;
    server->evlistener = evlistener;
    server->cb = cb;
    server->cbarg = cbarg;

    TAILQ_INIT(&server->connections);

    evconnlistener_set_cb(evlistener, rpc_server_accept_cb, server);
    return 0;
}

void rpc_server_stop(struct rpc_server *server)
{
    evconnlistener_disable(server->evlistener);

    struct rpc_connection *connection = NULL;

    while ((connection = TAILQ_FIRST((&server->connections))) != NULL) {
        connection->cb(connection, rpc_disconnect_event, NULL, connection->cbarg);
    }
}

struct rpc_connection *rpc_connection_init(struct event_base *evbase, struct sockaddr *address, int addrlen, rpc_connection_cb cb, void *cbarg)
{
    struct rpc_connection *connection = calloc(1, sizeof(struct rpc_connection));

    connection->cb = cb;
    connection->cbarg = cbarg;

    connection->evbuff = bufferevent_socket_new(evbase, -1, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_socket_connect(connection->evbuff, address, addrlen);

    bufferevent_setcb(connection->evbuff, connection_read_cb, connection_write_cb,
        connection_event_cb, connection);
    bufferevent_enable(connection->evbuff, EV_READ | EV_WRITE);

    /* get fd and set options */
    int fd = bufferevent_getfd(connection->evbuff);
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (int []) { 1 }, sizeof(int));

    bufferevent_setwatermark(connection->evbuff, EV_READ,
        sizeof(struct rpc_message_header), sizeof(struct rpc_message_header));

    return connection;
}

int rpc_connection_send_meta(struct rpc_connection *connection, const char *command, const void *data, size_t size, const void *meta, size_t meta_size)
{
    if (meta_size > 0 && meta_size > UINT16_MAX) {
        return 1;
    }

    struct rpc_message_header header;
    memset(&header, 0, sizeof(header));

    strncat(header.command.key, command, RPC_COMMAND_MAX - 1);
    header.size = size;
    header.meta_size = meta_size;
    header.magic = RPC_MAGIC;

    struct evbuffer *output = bufferevent_get_output(connection->evbuff);
    evbuffer_add(output, &header, sizeof(header));

    if (header.size > 0) {
        evbuffer_add(output, data, header.size);
    }

    if (header.meta_size > 0) {
        evbuffer_add(output, meta, header.meta_size);
    }

    return 0;
}

int rpc_connection_send(struct rpc_connection *connection, const char *command, const void *data, size_t size)
{
    return rpc_connection_send_meta(connection, command, data, size, NULL, 0);
}

void rpc_connection_destroy(struct rpc_connection *connection)
{
    if (connection == NULL) {
        return;
    }

    if (connection->server != NULL) {
        TAILQ_REMOVE(&connection->server->connections, connection, entries);
    }

    if (connection->evbuff) {
        bufferevent_free(connection->evbuff);
        connection->evbuff = NULL;
    }
}

void rpc_connection_free(struct rpc_connection *connection)
{
    if (connection) {
        rpc_connection_destroy(connection);

        free(connection);
        connection = NULL;
    }
}

void rpc_connection_set_arg(struct rpc_connection *connection, void *cbarg)
{
    connection->cbarg = cbarg;
}

void rpc_connection_set_ctx(struct rpc_connection *connection, void *ctx)
{
    connection->ctx = ctx;
}

void *rpc_connection_get_ctx(struct rpc_connection *connection)
{
    return connection->ctx;
}

void rpc_connection_disconnect(struct rpc_connection *connection)
{
    if (connection && connection->evbuff) {
        bufferevent_free(connection->evbuff);
        connection->evbuff = NULL;
    }
}

const void *rpc_message_meta(const struct rpc_message_header *msg)
{
    if (msg->meta_size == 0) {
        return NULL;
    }

    return msg->data + msg->size;
}
