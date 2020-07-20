// Copyright (C) 2018-2019, Cloudflare, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <ev.h>
#include <uthash.h>

#include <pthread.h>
#include <quiche.h>

#define GWLINE printf("gwgwgw---------%s   %d------------\n", __FUNCTION__, __LINE__);
#define GWINFO(FORMAT , args...) printf("gwgwgw---------%s   %d------------"FORMAT, __FUNCTION__, __LINE__,##args);

#define REMOVE_TIMER 1

#define LOCAL_CONN_ID_LEN 16

#define MAX_DATAGRAM_SIZE 1350

#define MAX_TOKEN_LEN \
    sizeof("quiche") - 1 + \
    sizeof(struct sockaddr_storage) + \
    QUICHE_MAX_CONN_ID_LEN

struct connections {
    int sock;

    struct conn_io *h;
};

struct conn_io {
    ev_timer timer;

    int sock;

    uint8_t cid[LOCAL_CONN_ID_LEN];

    quiche_conn *conn;

    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len;

    UT_hash_handle hh;
};

static quiche_config *config = NULL;

static struct connections *conns = NULL;

static void timeout_cb(EV_P_ ev_timer *w, int revents);

static void debug_log(const char *line, void *argp) {
    fprintf(stderr, "%s\n", line);
}

static void flush_egress(struct ev_loop *loop, struct conn_io *conn_io) {
    static uint8_t out[MAX_DATAGRAM_SIZE];
	GWLINE;
    while (1) {
        ssize_t written = quiche_conn_send(conn_io->conn, out, sizeof(out));

        if (written == QUICHE_ERR_DONE) {
            fprintf(stderr, "done writing\n");
            break;
        }

        if (written < 0) {
            fprintf(stderr, "failed to create packet: %zd\n", written);
            return;
        }

        ssize_t sent = sendto(conn_io->sock, out, written, 0,
                              (struct sockaddr *) &conn_io->peer_addr,
                              conn_io->peer_addr_len);
        if (sent != written) {
            perror("failed to send");
            return;
        }

        fprintf(stderr, "sent %zd bytes\n", sent);
    }
#if NEED_REMOVE_TIMER
    double t = quiche_conn_timeout_as_nanos(conn_io->conn) / 1e9f;
    conn_io->timer.repeat = t;
    ev_timer_again(loop, &conn_io->timer);
#endif
}

static void mint_token(const uint8_t *dcid, size_t dcid_len,
                       struct sockaddr_storage *addr, socklen_t addr_len,
                       uint8_t *token, size_t *token_len) {
    memcpy(token, "quiche", sizeof("quiche") - 1);
    memcpy(token + sizeof("quiche") - 1, addr, addr_len);
    memcpy(token + sizeof("quiche") - 1 + addr_len, dcid, dcid_len);

    *token_len = sizeof("quiche") - 1 + addr_len + dcid_len;
}

static bool validate_token(const uint8_t *token, size_t token_len,
                           struct sockaddr_storage *addr, socklen_t addr_len,
                           uint8_t *odcid, size_t *odcid_len) {
    if ((token_len < sizeof("quiche") - 1) ||
         memcmp(token, "quiche", sizeof("quiche") - 1)) {
        return false;
    }

    token += sizeof("quiche") - 1;
    token_len -= sizeof("quiche") - 1;

    if ((token_len < addr_len) || memcmp(token, addr, addr_len)) {
        return false;
    }

    token += addr_len;
    token_len -= addr_len;

    if (*odcid_len < token_len) {
        return false;
    }

    memcpy(odcid, token, token_len);
    *odcid_len = token_len;

    return true;
}

static struct conn_io *create_conn() {
    struct conn_io *conn_io = malloc(sizeof(*conn_io));
    if (conn_io == NULL) {
        fprintf(stderr, "failed to allocate connection IO\n");
        return NULL;
    }

    int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0) {
        perror("failed to open /dev/urandom");
        return NULL;
    }

    ssize_t rand_len = read(rng, conn_io->cid, LOCAL_CONN_ID_LEN);
    if (rand_len < 0) {
        perror("failed to create connection ID");
        return NULL;
    }

    conn_io->conn = NULL;

    HASH_ADD(hh, conns->h, cid, LOCAL_CONN_ID_LEN, conn_io);

    return conn_io;
}

static ssize_t accept_conn(struct conn_io *conn_io, uint8_t *odcid, size_t odcid_len) {
    quiche_conn *conn = quiche_accept(conn_io->cid, LOCAL_CONN_ID_LEN,
                                      odcid, odcid_len, config);
    if (conn == NULL) {
        fprintf(stderr, "failed to create connection\n");
        return -1;
    }

    conn_io->sock = conns->sock;
    conn_io->conn = conn;
#if NEED_REMOVE_TIMER
    ev_init(&conn_io->timer, timeout_cb);
    conn_io->timer.data = conn_io;
#endif
    fprintf(stderr, "new connection\n");

    return 0;
}

#include <stdio.h>

size_t GetFileSize(const char* file_name){
	FILE* fp = fopen(file_name, "r");
	fseek(fp, 0, SEEK_END);
	size_t size = ftell(fp);
	fclose(fp);
	return size; //单位是：byte
}

static void recv_cb(EV_P_ ev_io *w, int revents) {
	printf("\n\n\n");
	GWLINE
    struct conn_io *tmp, *conn_io = NULL;

    static uint8_t buf[65535];
    static uint8_t out[MAX_DATAGRAM_SIZE];

    while (1) {
		GWINFO("\n");
		GWLINE
        struct sockaddr_storage peer_addr;
        socklen_t peer_addr_len = sizeof(peer_addr);
        memset(&peer_addr, 0, peer_addr_len);

        ssize_t read = recvfrom(conns->sock, buf, sizeof(buf), 0,
                                (struct sockaddr *) &peer_addr,
                                &peer_addr_len);

        if (read < 0) {
            if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
                fprintf(stderr, "recv would block\n");
				GWLINE;
                break;
            }
			GWLINE;
            perror("failed to read");
            return;
        }

		GWLINE;
        uint8_t type;
        uint32_t version;

        uint8_t scid[QUICHE_MAX_CONN_ID_LEN];
        size_t scid_len = sizeof(scid);

        uint8_t dcid[QUICHE_MAX_CONN_ID_LEN];
        size_t dcid_len = sizeof(dcid);

        uint8_t odcid[QUICHE_MAX_CONN_ID_LEN];
        size_t odcid_len = sizeof(odcid);

        uint8_t token[MAX_TOKEN_LEN];
        size_t token_len = sizeof(token);

        int rc = quiche_header_info(buf, read, LOCAL_CONN_ID_LEN, &version,
                                    &type, scid, &scid_len, dcid, &dcid_len,
                                    token, &token_len);
        if (rc < 0) {
            fprintf(stderr, "failed to parse header: %d\n", rc);
			GWLINE;
            continue;
        }
		GWINFO("versoin = 0x%x, type = %hhu\n", version, type);

		GWINFO("scid ->\n");
		for(uint k = 0; k < scid_len; k++)
		{
			printf("%x ", scid[k]);
			if(k ==  scid_len - 1)
			{
				printf("\n");
			}
		}
		GWINFO("dcid ->\n");
		for(uint k = 0; k < dcid_len; k++)
		{
			printf("%x ", dcid[k]);
			if(k ==  dcid_len - 1)
			{
				printf("\n");
			}
		}
		GWINFO("token ->\n");
		for(uint k = 0; k < token_len; k++)
		{
			printf("%x ", token[k]);
			if(k ==  token_len - 1)
			{
				printf("\n");
			}
		}
        HASH_FIND(hh, conns->h, dcid, dcid_len, conn_io);

        if (conn_io == NULL) {
            if (!quiche_version_is_supported(version)) {
                fprintf(stderr, "version negotiation\n");

                ssize_t written = quiche_negotiate_version(scid, scid_len,
                                                           dcid, dcid_len,
                                                           out, sizeof(out));

                if (written < 0) {
                    fprintf(stderr, "failed to create vneg packet: %zd\n",
                            written);
					GWLINE;
                    continue;
                }

                ssize_t sent = sendto(conns->sock, out, written, 0,
                                      (struct sockaddr *) &peer_addr,
                                      peer_addr_len);
                if (sent != written) {
                    perror("failed to send");
					GWLINE;
                    continue;
                }

                fprintf(stderr, "sent %zd bytes\n", sent);
				GWLINE;
                continue;//第一轮收到客户单端初始化包后版本不匹配回报版本协商
            }

			printf("token_len = %zu\n", token_len);
            if (token_len == 0) {
                fprintf(stderr, "stateless retry\n");

                conn_io = create_conn(odcid, odcid_len);
                if (conn_io == NULL) {
					GWLINE;
                    continue;
                }
				GWINFO("coon_id->sock = %d, conn = %p\n", conn_io->sock, conn_io->conn);
                mint_token(dcid, dcid_len, &peer_addr, peer_addr_len,
                           token, &token_len);

                ssize_t written = quiche_retry(scid, scid_len,
                                               dcid, dcid_len,
                                               conn_io->cid, LOCAL_CONN_ID_LEN,
                                               token, token_len,
                                               version, out, sizeof(out));

                if (written < 0) {
                    fprintf(stderr, "failed to create retry packet: %zd\n",
                            written);
					GWLINE;
                    continue;
                }

                ssize_t sent = sendto(conns->sock, out, written, 0,
                                      (struct sockaddr *) &peer_addr,
                                      peer_addr_len);
                if (sent != written) {
                    perror("failed to send");
					GWLINE;
                    continue;
                }

                fprintf(stderr, "sent %zd bytes\n", sent);
				GWLINE;
                continue;//第二轮retry
            }
        }

        if (conn_io != NULL && conn_io->conn == NULL) {
            if (!validate_token(token, token_len, &peer_addr, peer_addr_len,
                                odcid, &odcid_len)) {
                fprintf(stderr, "invalid address validation token\n");
				GWLINE;
                continue;
            }

            if (accept_conn(conn_io, odcid, odcid_len) < 0) {
				GWLINE;
                continue;
            }
        }

        memcpy(&conn_io->peer_addr, &peer_addr, peer_addr_len);
        conn_io->peer_addr_len = peer_addr_len;
		//quic链接建立

        if (conn_io != NULL && conn_io->conn != NULL) {
			GWLINE;
            ssize_t done = quiche_conn_recv(conn_io->conn, buf, read);

            if (done < 0) {
                fprintf(stderr, "failed to process packet: %zd\n", done);
				GWLINE;
                continue;
            }

            fprintf(stderr, "recv %zd bytes\n", done);

            if (quiche_conn_is_established(conn_io->conn)) {
				GWLINE;
                uint64_t s = 0;

                quiche_stream_iter *readable = quiche_conn_readable(conn_io->conn);

                while (quiche_stream_iter_next(readable, &s)) {
					GWLINE
                    fprintf(stderr, "stream %" PRIu64 " is readable\n", s);

                    bool fin = false;
                    ssize_t recv_len = quiche_conn_stream_recv(conn_io->conn, s,
                                                            buf, sizeof(buf),
                                                            &fin);
                    if (recv_len < 0) {
						GWLINE;
                        break;
                    }
                    GWINFO("quiche_conn_stream_recv buf = %s\n", buf);
                    if (fin) {
 #if 0
                        size_t readedSize = 0;
                        size_t totlaSize = GetFileSize("./test.ts");
                        GWINFO("test.ts size = %zu\n", totlaSize);
                        if(totlaSize > 0)
                        {
                            FILE* pfile = fopen("./test.ts", "r");
                            if(NULL ==  pfile)
                            {
                                GWINFO("FILE OPEN FAILED\n");
                                return;
                            }
                            char pbuf[1024*10];
                            while(1)
                            { 
                                memset(pbuf,  0x0,  sizeof(pbuf));
                                size_t readSize = fread(pbuf, 1,  sizeof(pbuf),  pfile);
                                if(readSize > 0)
                                {
                                    readedSize +=  readSize;
                                    bool flag = readedSize < totlaSize ? false : true;
                                    GWINFO("gw send readSize = %zu, readedSize = %zu\n", readSize, readedSize);
                                    quiche_conn_stream_send(conn_io->conn, s, (uint8_t *) pbuf, readSize, flag);
                                }
                                else
                                {
                                    GWINFO("gw send readSize = %zu, readedSize = %zu finished\n", readSize, readedSize);
                                    break;
                                }
                            }
                            fclose(pfile);
                        }
#endif
                    }
                }

                quiche_stream_iter_free(readable);
            }
        }

    }
	GWLINE;
	//前期连接没建立，找不到coon_io的，不会写
    HASH_ITER(hh, conns->h, conn_io, tmp) {
        if (conn_io->conn == NULL) {
			GWLINE;
            break;
        }
		GWLINE;
        flush_egress(loop, conn_io);

        if (quiche_conn_is_closed(conn_io->conn)) {
			GWLINE;
            quiche_stats stats;

            quiche_conn_stats(conn_io->conn, &stats);
            fprintf(stderr, "connection closed, recv=%zu sent=%zu lost=%zu rtt=%" PRIu64 "ns cwnd=%zu\n",
                    stats.recv, stats.sent, stats.lost, stats.rtt, stats.cwnd);

            HASH_DELETE(hh, conns->h, conn_io);

			#if NEED_REMOVE_TIMER
            ev_timer_stop(loop, &conn_io->timer);
			#endif
            quiche_conn_free(conn_io->conn);
            free(conn_io);
        }
    }
	GWLINE;
}

static void timeout_cb(EV_P_ ev_timer *w, int revents) {
    struct conn_io *conn_io = w->data;
    if (conn_io->conn == NULL) {
        return;
    }

    quiche_conn_on_timeout(conn_io->conn);
    GWLINE;
    fprintf(stderr, "timeout\n");
    GWLINE;
    flush_egress(loop, conn_io);

    if (quiche_conn_is_closed(conn_io->conn)) {
        quiche_stats stats;

        quiche_conn_stats(conn_io->conn, &stats);
        fprintf(stderr, "connection closed, recv=%zu sent=%zu lost=%zu rtt=%" PRIu64 "ns cwnd=%zu\n",
                stats.recv, stats.sent, stats.lost, stats.rtt, stats.cwnd);

        HASH_DELETE(hh, conns->h, conn_io);

        ev_timer_stop(loop, &conn_io->timer);
        quiche_conn_free(conn_io->conn);
        free(conn_io);

        return;
    }
}

void thread(void *arg)
{
	return ;
	struct ev_loop *loop = (struct ev_loop*)arg;
	int k = 10000;
	while(k--)
	{
		struct conn_io *tmp, *conn_io = NULL;
		HASH_ITER(hh, conns->h, conn_io, tmp)
		{
			const uint8_t *app_proto;
			size_t app_proto_len;

			quiche_conn_application_proto(conn_io->conn, &app_proto, &app_proto_len);

			fprintf(stderr, "connection established: %.*s\n",
					(int) app_proto_len, app_proto);

			const static uint8_t r[] = "server say hello1\r\n";
			if (quiche_conn_stream_send(conn_io->conn, 5, r, sizeof(r), false) < 0) {
				fprintf(stderr, "failed to send server say hello to client\n");
				return;
			}
			else
			{
                flush_egress(loop, conn_io);
				fprintf(stderr, "success to send server say hello to client\n");
			}
            
            const static uint8_t w[] = "server say hello2\r\n";
            if (quiche_conn_stream_send(conn_io->conn, 5, w, sizeof(2), false) < 0) {
				fprintf(stderr, "failed to send server say hello to client\n");
				return;
			}
			else
			{
                flush_egress(loop, conn_io);
				fprintf(stderr, "success to send server say hello to client\n");
			}
		}
		sleep(1);
	}
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    const char *port = "443";

    const struct addrinfo hints = {
        .ai_family = PF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP
    };

    quiche_enable_debug_logging(debug_log, NULL);

    struct addrinfo *local;
    if (getaddrinfo(host, port, &hints, &local) != 0) {
        perror("failed to resolve host");
        return -1;
    }

    int sock = socket(local->ai_family, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("failed to create socket");
        return -1;
    }

    if (fcntl(sock, F_SETFL, O_NONBLOCK) != 0) {
        perror("failed to make socket non-blocking");
        return -1;
    }

    if (bind(sock, local->ai_addr, local->ai_addrlen) < 0) {
        perror("failed to connect socket");
        return -1;
    }

    config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (config == NULL) {
        fprintf(stderr, "failed to create config\n");
        return -1;
    }

    quiche_config_load_cert_chain_from_pem_file(config, "./cert.crt");
    quiche_config_load_priv_key_from_pem_file(config, "./cert.key");

    quiche_config_set_application_protos(config,
        (uint8_t *) "\x05hq-29\x05hq-28\x05hq-27\x08http/0.9", 27);

    quiche_config_set_max_idle_timeout(config, 0);
    quiche_config_set_max_udp_payload_size(config, MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(config, 100000000);
    quiche_config_set_initial_max_stream_data_bidi_local(config, 50000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(config, 50000000);
    quiche_config_set_initial_max_streams_bidi(config, 100);
    quiche_config_set_cc_algorithm(config, QUICHE_CC_RENO);

    struct connections c;
    c.sock = sock;
    c.h = NULL;

    conns = &c;

    ev_io watcher;

    struct ev_loop *loop = ev_default_loop(0);

	pthread_t id;
	int i,ret;
	ret=pthread_create(&id,NULL,(void *) thread, loop);
	if(ret!=0){
		GWINFO ("Create pthread error!\n"); 
		exit (1);
	}

    ev_io_init(&watcher, recv_cb, sock, EV_READ);
    ev_io_start(loop, &watcher);
    watcher.data = &c;

    ev_loop(loop, 0);

    freeaddrinfo(local);

    quiche_config_free(config);

    return 0;
}