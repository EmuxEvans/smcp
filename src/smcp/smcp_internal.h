/*
 *  smcp_internal.h
 *  SMCP
 *
 *  Created by Robert Quattlebaum on 10/15/10.
 *  Copyright 2010 deepdarc. All rights reserved.
 *
 */

#if __CONTIKI__ && !CONTIKI_TARGET_MINIMAL_NET
#if !defined(timeval)
#define timeval timeval
struct timeval {
	time_t		tv_sec;
	suseconds_t tv_usec;
};
#endif
#else
#include <sys/time.h>
#endif

#include "smcp_pairing.h"

#pragma mark -
#pragma mark Class Definitions

struct smcp_response_handler_s {
	struct bt_item_s			bt_item;
	coap_transaction_id_t		tid;
	struct timeval				expiration;
	int							flags;
	struct smcp_timer_s			timer;
	smcp_response_handler_func	callback;
	void*						context;
};

typedef struct smcp_response_handler_s *smcp_response_handler_t;


// Consider members of this struct to be private!
struct smcp_daemon_s {
#if SMCP_USE_BSD_SOCKETS
	int						fd;
	int						mcfd;
#elif __CONTIKI__
	struct uip_udp_conn*	udp_conn;
#endif

	struct smcp_node_s		root_node;
	smcp_timer_t			timers;
	smcp_response_handler_t handlers;
	uint16_t				port;
	PAIRING_STATE;

#if SMCP_USE_BSD_SOCKETS
	struct sockaddr*		current_inbound_saddr;
	socklen_t				current_inbound_socklen;
#elif defined(__CONTIKI__)
	const uip_ipaddr_t *	current_inbound_toaddr;
	uint16_t				current_inbound_toport;
#endif

	coap_transaction_id_t	current_inbound_request_tid;
	char*					current_inbound_request_token;
	uint16_t				current_inbound_request_token_len;
	uint8_t					cascade_count;
	bool					did_respond, is_processing_message;

	coap_header_item_t		current_inbound_headers[SMCP_MAX_HEADERS + 1];
	uint8_t					current_inbound_header_count;
};
