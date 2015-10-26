/*
 * This file is part of dmrshark.
 *
 * dmrshark is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dmrshark is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dmrshark.  If not, see <http://www.gnu.org/licenses/>.
**/

#include DEFAULTCONFIG

#include "aprs.h"

#include <libs/config/config.h>

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <ctype.h>

static pthread_t aprs_thread;

static pthread_mutex_t aprs_mutex_thread_should_stop = PTHREAD_MUTEX_INITIALIZER;
static flag_t aprs_thread_should_stop = 0;

static pthread_mutex_t aprs_mutex_wakeup = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t aprs_cond_wakeup;

static flag_t aprs_enabled = 0;
static flag_t aprs_loggedin = 0;
static int aprs_sockfd = -1;

typedef struct aprs_queue_st {
	char callsign[12];
	dmr_data_gpspos_t gpspos;

	struct aprs_queue_st *next;
} aprs_queue_t;

static pthread_mutex_t aprs_mutex_queue = PTHREAD_MUTEX_INITIALIZER;
static aprs_queue_t *aprs_queue_first_entry = NULL;
static aprs_queue_t *aprs_queue_last_entry = NULL;

void aprs_add_to_gpspos_queue(dmr_data_gpspos_t *gpspos, char *callsign) {
	aprs_queue_t *new_entry;

	if (!aprs_enabled)
		return;

	new_entry = (aprs_queue_t *)malloc(sizeof(aprs_queue_t));
	if (new_entry == NULL) {
		console_log("aprs error: can't allocate memory for new gps position entry in the queue\n");
		return;
	}
	memcpy(&new_entry->gpspos, gpspos, sizeof(dmr_data_gpspos_t));
	strncpy(new_entry->callsign, callsign, sizeof(new_entry->callsign));

	pthread_mutex_lock(&aprs_mutex_queue);
	if (aprs_queue_first_entry == NULL)
		aprs_queue_first_entry = aprs_queue_last_entry = new_entry;
	else {
		aprs_queue_last_entry->next = new_entry;
		aprs_queue_last_entry = new_entry;
	}

	console_log(LOGLEVEL_APRS "aprs queue: added entry: callsign %s %s\n", callsign, dmr_data_get_gps_string(gpspos));
	pthread_mutex_unlock(&aprs_mutex_queue);
}

static flag_t aprs_thread_sendmsg(const char *format, ...) {
	va_list argptr;
	char buf[512];
	flag_t result = 1;

    va_start(argptr, format);

	vsnprintf(buf, sizeof(buf), format, argptr);
	console_log(LOGLEVEL_APRS LOGLEVEL_DEBUG "aprs sending message: %s", buf);
	errno = 0;
	write(aprs_sockfd, buf, strlen(buf));
	if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
		console_log(LOGLEVEL_APRS "aprs error: disconnected\n");
		aprs_loggedin = 0;
		close(aprs_sockfd);
		aprs_sockfd = -1;
		result = 0;
	}

    va_end(argptr);
    return result;
}

static void aprs_thread_connect(void) {
	struct hostent *server;
	struct sockaddr_in serveraddr;
	char *host = config_get_aprsserverhost();
	uint16_t port = config_get_aprsserverport();
	char *callsign = config_get_aprsservercallsign();
	uint16_t passcode = config_get_aprsserverpasscode();
	int flag;
	time_t connectstartedat;
	char buf[50];
	int bytes_read;
	char expected_login_reply[50];
	struct timespec ts;

	aprs_loggedin = 0;

	if (strlen(host) == 0 || port == 0 || strlen(callsign) == 0) {
		free(host);
		free(callsign);
		return;
	}

	console_log(LOGLEVEL_APRS "aprs: trying to connect to aprs-is server %s:%u...\n", host, port);

	aprs_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (aprs_sockfd < 0) {
		console_log("aprs error: failed to init socket\n");
		free(host);
		free(callsign);
		return;
	}

	flag = 1;
	setsockopt(aprs_sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
	flag = 1;
	setsockopt(aprs_sockfd, IPPROTO_TCP, SO_REUSEADDR, &flag, sizeof(int));
	flag = 1;
	setsockopt(aprs_sockfd, IPPROTO_TCP, SO_KEEPALIVE, &flag, sizeof(int));

	server = gethostbyname(host);
	if (server == NULL) {
		console_log("aprs error: failed to resolve hostname %s\n", host);
		close(aprs_sockfd);
		aprs_sockfd = -1;
		free(host);
		free(callsign);
		return;
	}

	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	memcpy(&serveraddr.sin_addr.s_addr, server->h_addr, server->h_length);
	serveraddr.sin_port = htons(port);

	if (connect(aprs_sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
		console_log(LOGLEVEL_APRS "aprs error: failed to connect\n");
		close(aprs_sockfd);
		aprs_sockfd = -1;
		free(host);
		free(callsign);
		return;
	}

	console_log(LOGLEVEL_APRS "aprs: logging in\n");
	if (aprs_thread_sendmsg("user %s pass %u\n", callsign, passcode)) {
		snprintf(expected_login_reply, sizeof(expected_login_reply), "# logresp %s verified", callsign);
		connectstartedat = time(NULL);
		while (1) {
			errno = 0;
			bytes_read = read(aprs_sockfd, buf, sizeof(buf)-1);
			if (bytes_read < 0 || (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
				console_log(LOGLEVEL_APRS "aprs: error during login\n");
				close(aprs_sockfd);
				aprs_sockfd = -1;
				break;
			}

			if (bytes_read > 0 && isprint(buf[0])) {
				buf[bytes_read] = 0;
				console_log(LOGLEVEL_APRS LOGLEVEL_DEBUG "aprs login read: %s", buf);
			}

			if (strncmp(buf, expected_login_reply, bytes_read) > 0) {
				aprs_loggedin = 1;
				console_log(LOGLEVEL_APRS "aprs: connected\n");
				break;
			}

			if (time(NULL)-connectstartedat > 10) {
				console_log(LOGLEVEL_APRS "aprs: login timeout\n");
				close(aprs_sockfd);
				aprs_sockfd = -1;
				break;
			}

			pthread_mutex_lock(&aprs_mutex_thread_should_stop);
			if (aprs_thread_should_stop) {
				pthread_mutex_unlock(&aprs_mutex_thread_should_stop);
				break;
			}
			pthread_mutex_unlock(&aprs_mutex_thread_should_stop);

			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_nsec += 1000;

			pthread_mutex_lock(&aprs_mutex_wakeup);
			pthread_cond_timedwait(&aprs_cond_wakeup, &aprs_mutex_wakeup, &ts);
			pthread_mutex_unlock(&aprs_mutex_wakeup);
		}
	}

	free(host);
	free(callsign);
}

static void aprs_thread_process(void) {
	aprs_queue_t *next_entry;

	if (aprs_sockfd < 0 || !aprs_loggedin)
		return;

	pthread_mutex_lock(&aprs_mutex_queue);
	while (aprs_queue_first_entry) {
		console_log(LOGLEVEL_APRS "aprs queue: sending entry: callsign %s %s\n", aprs_queue_first_entry->callsign, dmr_data_get_gps_string(&aprs_queue_first_entry->gpspos));

		if (aprs_thread_sendmsg("\n")) {
			next_entry = aprs_queue_first_entry->next;
			free(aprs_queue_first_entry);
			aprs_queue_first_entry = next_entry;
		} else
			break;
	}
	if (aprs_queue_first_entry == NULL)
		aprs_queue_last_entry = NULL;
	pthread_mutex_unlock(&aprs_mutex_queue);
}

static void *aprs_thread_init(void *arg) {
	struct timespec ts;
	aprs_queue_t *next_entry;
	time_t lastconnecttryat = 0;

	aprs_thread_should_stop = !aprs_enabled;
	pthread_cond_init(&aprs_cond_wakeup, NULL);

	while (1) {
		if (aprs_sockfd < 0 && time(NULL)-lastconnecttryat >= 10) {
			aprs_thread_connect();
			lastconnecttryat = time(NULL);
		}

		pthread_mutex_lock(&aprs_mutex_thread_should_stop);
		if (aprs_thread_should_stop) {
			pthread_mutex_unlock(&aprs_mutex_thread_should_stop);
			break;
		}
		pthread_mutex_unlock(&aprs_mutex_thread_should_stop);

		aprs_thread_process();

		pthread_mutex_lock(&aprs_mutex_queue);
		if (aprs_queue_first_entry == NULL) {
			pthread_mutex_unlock(&aprs_mutex_queue);

			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 1;

			pthread_mutex_lock(&aprs_mutex_wakeup);
			pthread_cond_timedwait(&aprs_cond_wakeup, &aprs_mutex_wakeup, &ts);
			pthread_mutex_unlock(&aprs_mutex_wakeup);
		}
		pthread_mutex_unlock(&aprs_mutex_queue);
	}

	pthread_mutex_lock(&aprs_mutex_queue);
	while (aprs_queue_first_entry) {
		next_entry = aprs_queue_first_entry->next;
		free(aprs_queue_first_entry);
		aprs_queue_first_entry = next_entry;
	}
	aprs_queue_last_entry = NULL;
	pthread_mutex_unlock(&aprs_mutex_queue);

	pthread_mutex_destroy(&aprs_mutex_thread_should_stop);
	pthread_mutex_destroy(&aprs_mutex_queue);
	pthread_mutex_destroy(&aprs_mutex_wakeup);
	pthread_cond_destroy(&aprs_cond_wakeup);

	pthread_exit((void*) 0);
}

void aprs_init(void) {
	pthread_attr_t attr;
	char *host = NULL;

	console_log("aprs: init\n");

	host = config_get_aprsserverhost();
	if (strlen(host) != 0) {
		aprs_enabled = 1;
		console_log("aprs: starting thread for aprs\n");

		// Explicitly creating the thread as joinable to be compatible with other systems.
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		pthread_create(&aprs_thread, &attr, aprs_thread_init, NULL);
	} else
		console_log("aprs: no server configured\n");
	free(host);
}

void aprs_deinit(void) {
	void *status = NULL;

	console_log("aprs: deinit\n");
	aprs_enabled = 0;

	// Waking up the thread if it's sleeping.
	pthread_mutex_lock(&aprs_mutex_wakeup);
	pthread_cond_signal(&aprs_cond_wakeup);
	pthread_mutex_unlock(&aprs_mutex_wakeup);

	pthread_mutex_lock(&aprs_mutex_thread_should_stop);
	aprs_thread_should_stop = 1;
	pthread_mutex_unlock(&aprs_mutex_thread_should_stop);
	console_log("aprs: waiting for aprs thread to exit\n");
	pthread_join(aprs_thread, &status);
}