#pragma once

#include <stdint.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <sys/queue.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RPC_MAGIC 0x1B35529A
#define RPC_COMMAND_MAX 16

#define rpc_command_hash(s) (((union rpc_command) { .key = s }).hash)

enum {
    rpc_input_event,
    rpc_error_event,
    rpc_connect_event,
    rpc_disconnect_event,
    rpc_empty_output_event
};

struct rpc_server;
struct rpc_connection;
struct rpc_message_header;

typedef int (* rpc_connection_cb)(struct rpc_connection *connection, int event, struct rpc_message_header *msg, void *cbarg);

struct rpc_server {
    struct event_base *evbase;
    struct evconnlistener *evlistener;

    rpc_connection_cb cb;
    void *cbarg;

    TAILQ_HEAD(, rpc_connection) connections;
};

union rpc_command {
    char key[RPC_COMMAND_MAX];
    __uint128_t hash;
};

struct rpc_message_header {
    uint64_t size;
    union rpc_command command;

    uint32_t magic;

    uint16_t meta_size;
    uint16_t rsv;

    const char data[];
} __attribute__((packed));

struct rpc_connection;

int rpc_server_init(struct rpc_server *server, struct event_base *evbase, struct evconnlistener *listener, rpc_connection_cb cb, void *cbarg);
void rpc_server_stop(struct rpc_server *server);

struct rpc_connection *rpc_connection_init(struct event_base *evbase, struct sockaddr *address, int addrlen, rpc_connection_cb cb, void *cbarg);
void rpc_connection_free(struct rpc_connection *connection);

int rpc_connection_send(struct rpc_connection *connection, const char *command, const void *data, size_t size);
void rpc_connection_set_arg(struct rpc_connection *connection, void *cbarg);

void rpc_connection_set_ctx(struct rpc_connection *connection, void *ctx);
void *rpc_connection_get_ctx(struct rpc_connection *connection);

void rpc_connection_disconnect(struct rpc_connection *connection);

const void *rpc_message_meta(const struct rpc_message_header *msg);

#ifdef __cplusplus
}
#endif

