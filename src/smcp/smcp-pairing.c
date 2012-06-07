/*	@file smcp_pairing.c
**	@author Robert Quattlebaum <darco@deepdarc.com>
**
**	Copyright (C) 2011,2012 Robert Quattlebaum
**
**	Permission is hereby granted, free of charge, to any person
**	obtaining a copy of this software and associated
**	documentation files (the "Software"), to deal in the
**	Software without restriction, including without limitation
**	the rights to use, copy, modify, merge, publish, distribute,
**	sublicense, and/or sell copies of the Software, and to
**	permit persons to whom the Software is furnished to do so,
**	subject to the following conditions:
**
**	The above copyright notice and this permission notice shall
**	be included in all copies or substantial portions of the
**	Software.
**
**	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
**	KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
**	WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
**	PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
**	OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
**	OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
**	OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
**	SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef VERBOSE_DEBUG
#define VERBOSE_DEBUG 0
#endif

#include "assert_macros.h"

#if !SMCP_DEBUG_TIMERS && !VERBOSE_DEBUG
#undef assert_printf
#define assert_printf(fmt, ...) do { } while(0)
#endif

#include "smcp.h"
#include "smcp-logging.h"

#if SMCP_ENABLE_PAIRING

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ll.h"

#include "smcp-pairing.h"
#include "smcp-node.h"
#include "smcp-timer.h"
#include "smcp-internal.h"
#include "smcp-variable_node.h"

#include "smcp-helpers.h"
#include "url-helpers.h"

#pragma mark -
#pragma mark Paring

static smcp_status_t
smcp_pairing_node_request_handler(
	smcp_pairing_node_t	pairing,
	smcp_method_t	method
);

static smcp_status_t
smcp_pairing_path_request_handler(
	smcp_node_t		node,
	smcp_method_t	method
);

smcp_status_t
smcp_pair_with_sockaddr(
	smcp_t	self,
	const char*		path,
	const char*		uri,
	SMCP_SOCKET_ARGS,
	int				flags,
	uintptr_t*		idVal
) {
	smcp_status_t ret = SMCP_STATUS_OK;

	smcp_node_t path_node = NULL;
	smcp_pairing_node_t pairing = NULL;

	require_action(path, bail, ret = SMCP_STATUS_INVALID_ARGUMENT);
	require_action(uri, bail, ret = SMCP_STATUS_INVALID_ARGUMENT);

	while(path[0] == '/' && path[1] != 0) path++;

	path_node = smcp_node_find(
		self->root_pairing_node,
		path,
		strlen(path)
	);

	if(!path_node) {
		path_node = smcp_node_alloc();
		require_action(path_node, bail, ret = SMCP_STATUS_MALLOC_FAILURE);
		path_node = smcp_node_init(path_node, self->root_pairing_node, strdup(path));
		path_node->request_handler = &smcp_pairing_path_request_handler;
	}

	require_action(path_node, bail, ret = SMCP_STATUS_FAILURE);

	pairing = calloc(1, sizeof(*pairing));

	require_action(pairing, bail, ret = SMCP_STATUS_MALLOC_FAILURE);

	smcp_node_init(&pairing->node, path_node, strdup(uri));

	pairing->node.request_handler = (smcp_inbound_handler_func)&smcp_pairing_node_request_handler;

	pairing->flags = flags;

#if SMCP_USE_BSD_SOCKETS
	if(saddr)
		memcpy(&pairing->saddr,saddr,sizeof(pairing->saddr));
#elif CONTIKI
	if(toaddr) {
		memcpy(&pairing->toaddr,toaddr,sizeof(pairing->toaddr));
		pairing->toport = toport;
	}
#endif

	if(idVal)
		*idVal = (uintptr_t)pairing;

#if SMCP_CONF_USE_SEQ
#if DEBUG
	pairing->seq = pairing->ack = 0;
#else
	pairing->seq = pairing->ack = SMCP_FUNC_RANDOM_UINT32();
#endif
	assert(pairing->seq == pairing->ack);
#endif

bail:
	return ret;
}

smcp_status_t
smcp_pair_with_uri(
	smcp_t	self,
	const char*		path,
	const char*		uri,
	int				flags,
	uintptr_t*		idVal
) {
#if SMCP_USE_BSD_SOCKETS
	return smcp_pair_with_sockaddr(self, path, uri, NULL, 0, flags, idVal);
#elif CONTIKI
	return smcp_pair_with_sockaddr(self, path, uri, 0, 0, flags, idVal);
#else
	return SMCP_STATUS_NOT_IMPLEMENTED;
#endif
}


smcp_pairing_node_t
smcp_get_first_pairing_for_path(
	smcp_t self, const char* path
) {
	smcp_pairing_node_t ret = NULL;
	smcp_node_t path_node = NULL;

	while(path[0] == '/' && path[1] != 0) path++;

	path_node = smcp_node_find(
		self->root_pairing_node,
		path,
		strlen(path)
	);

	require(path_node,bail);
	require(path_node->children,bail);

	ret = bt_first(path_node->children);

bail:
	return ret;
}

smcp_pairing_node_t
smcp_next_pairing(
	smcp_t self, smcp_pairing_node_t pairing
) {
	smcp_pairing_node_t ret = NULL;

	require(pairing != NULL, bail);

	ret = bt_next((void*)pairing);

bail:
	return ret;
}

static smcp_status_t
smcp_retry_custom_event(smcp_pairing_node_t pairing) {
	smcp_status_t status;
	const smcp_t const self = smcp_get_current_instance();
	smcp_event_tracker_t event = pairing->currentEvent;

	coap_content_type_t content_type;

	status = smcp_outbound_begin(self,COAP_METHOD_POST,COAP_TRANS_TYPE_CONFIRMABLE);
	require_noerr(status,bail);

#if SMCP_USE_BSD_SOCKETS
	if(((struct sockaddr*)&pairing->saddr)->sa_family)
		status = smcp_outbound_set_destaddr((struct sockaddr*)&pairing->saddr, sizeof(pairing->saddr));
#elif CONTIKI
	if(pairing->toport)
		status = smcp_outbound_set_destaddr(&pairing->toaddr,pairing->toport);
#endif
	require_noerr(status,bail);

	status = smcp_outbound_set_uri(smcp_pairing_get_uri_cstr(pairing),0);
	require_noerr(status,bail);

	status = smcp_outbound_add_option(SMCP_HEADER_ORIGIN, smcp_pairing_get_path_cstr(pairing), HEADER_CSTR_LEN);
	require_noerr(status,bail);

#if SMCP_CONF_USE_SEQ
	char cseq[24];
#if __AVR__
	snprintf_P(
		cseq,
		sizeof(cseq),
		PSTR("%lX POST"),
		(long unsigned int)pairing->seq
	);
#else
	snprintf(cseq,
		sizeof(cseq),
		"%lX POST",
		(long unsigned int)pairing->seq
	);
#endif
	status = smcp_outbound_add_option(SMCP_HEADER_CSEQ, cseq, HEADER_CSTR_LEN);
	require_noerr(status,bail);
#endif

	if(event->contentFetcher) {
		size_t len = 0;
		char* ptr;

		status = (*event->contentFetcher)(
			event->contentFetcherContext,
			NULL,
			NULL,
			&content_type
		);
		require_noerr(status,bail);

		status = smcp_outbound_add_option(COAP_HEADER_CONTENT_TYPE, (void*)&content_type, 1);
		require_noerr(status,bail);

		ptr = smcp_outbound_get_content_ptr(&len);
		event->contentFetcher(
			event->contentFetcherContext,
			ptr,
			&len,
			&content_type
		);
		smcp_outbound_set_content_len(len);
	}

	status = smcp_outbound_send();
	require_noerr(status,bail);

#if SMCP_CONF_PAIRING_STATS
	pairing->send_count++;
#endif

bail:
	return status;
}

static void
smcp_event_tracker_release(smcp_event_tracker_t event) {
	assert(event!=NULL);
	DEBUG_PRINTF("Event:%p: release",event);
	DEBUG_PRINTF("Event:%p: refCount = %d",event,event->refCount-1);
	if(0==--event->refCount) {
		DEBUG_PRINTF("Event:%p: dealloc\n",event);
		if(event->contentFetcher)
			(*event->contentFetcher)(
				event->contentFetcherContext,
				NULL,
				NULL,
				NULL
			);
		free(event);
	}
}

static void
smcp_event_response_handler(
	int statuscode, smcp_pairing_node_t pairing
) {
#if SMCP_CONF_PAIRING_STATS
	if(pairing->last_error != statuscode) {
		pairing->last_error = statuscode;
		smcp_trigger_event_with_node(
			smcp_get_current_instance(),
			&pairing->node,
			"err"
		);
	}
	if(statuscode && ((statuscode < 200) || (statuscode >= 300))) {
		// Looks like we failed to live up to this pairing.
		pairing->errors++;
		smcp_trigger_event_with_node(
			smcp_get_current_instance(),
			&pairing->node,
			"ec"
		);
	}
#endif
	// expire the event.
	smcp_event_tracker_t event = pairing->currentEvent;
	DEBUG_PRINTF("Event:%p: smcp_event_response_handler(): statuscode=%d",event,statuscode);
	pairing->currentEvent = NULL;
	smcp_event_tracker_release(event);
}

static smcp_status_t
smcp_retry_event(smcp_pairing_node_t pairing) {
	smcp_status_t status;
	const smcp_t const self = smcp_get_current_instance();
	char* original_path = NULL;
	struct coap_header_s* packet;

	status = smcp_outbound_begin(self,COAP_METHOD_POST,COAP_TRANS_TYPE_CONFIRMABLE);
	require_noerr(status,bail);

	self->is_responding = true;
	self->force_current_outbound_code = true;

#if SMCP_USE_BSD_SOCKETS
	if(((struct sockaddr*)&pairing->saddr)->sa_family)
		status = smcp_outbound_set_destaddr((struct sockaddr*)&pairing->saddr, sizeof(pairing->saddr));
#elif CONTIKI
	if(pairing->toport)
		status = smcp_outbound_set_destaddr(&pairing->toaddr,pairing->toport);
#endif
	require_noerr(status,bail);

	status = smcp_outbound_set_uri(smcp_pairing_get_uri_cstr(pairing),0);
	require_noerr(status,bail);

	status = smcp_outbound_add_option(
		SMCP_HEADER_ORIGIN,
		smcp_pairing_get_path_cstr(pairing),
		HEADER_CSTR_LEN
	);

	require_noerr(status,bail);

#if SMCP_CONF_USE_SEQ
	char cseq[24];
#if __AVR__
	snprintf_P(
		cseq,
		sizeof(cseq),
		PSTR("%lX POST"),
		(long unsigned int)pairing->seq
	);
#else
	snprintf(cseq,
		sizeof(cseq),
		"%lX POST",
		(long unsigned int)pairing->seq
	);
#endif
	status = smcp_outbound_add_option(SMCP_HEADER_CSEQ, cseq, HEADER_CSTR_LEN);
	require_noerr(status,bail);
#endif

#if HAS_ALLOCA
	packet = alloca(4+strlen(smcp_pairing_get_path_cstr(pairing))+5);
#else
	{
		static char i_really_wish_we_supported_alloca[4+SMCP_MAX_PATH_LENGTH+5];
		packet = (struct coap_header_s*)i_really_wish_we_supported_alloca;
	}
#endif

	packet->code = COAP_METHOD_GET;
	packet->tt = COAP_TRANS_TYPE_CONFIRMABLE;
	packet->version = COAP_VERSION;
	self->is_processing_message = true;
	self->did_respond = false;
	self->inbound.packet = packet;
	self->inbound.content_len = 0;
	self->inbound.content_ptr = NULL;
	self->inbound.last_option_key = 0;
	self->inbound.is_fake = true;

	{
		uint8_t* option = packet->options;
		coap_option_key_t prev_key = 0;
		uint8_t option_count = 0;
		char* path = original_path;
		char* component;

		original_path = strdup(smcp_pairing_get_path_cstr(pairing));

		while(url_path_next_component(&path, &component)) {
			DEBUG_PRINTF("Pushing path component \"%s\"",component);
			option = coap_encode_option(
				option,
				&option_count,
				prev_key,
				COAP_HEADER_URI_PATH,
				(uint8_t*)component,
				strlen(component)
			);
			prev_key = COAP_HEADER_URI_PATH;
		}
		if(option_count>=0xF) {
			packet->option_count=0xF;
			*option++ = 0xF0;
		} else {
			packet->option_count=option_count;
		}

		self->inbound.content_ptr = (char*)option;
	}

	self->inbound.this_option = self->inbound.packet->options;
	self->inbound.options_left = self->inbound.packet->option_count;
	status = smcp_handle_request(self);
	require_noerr(status,bail);

#if SMCP_CONF_PAIRING_STATS
	pairing->send_count++;
#endif

bail:
	self->is_processing_message = false;
	self->did_respond = false;
	free(original_path);
	return status;
}

smcp_status_t
smcp_trigger_event(
	smcp_t self,
	const char* path
) {
	smcp_status_t ret = 0;
	smcp_pairing_node_t iter;
	smcp_event_tracker_t event = NULL;

	require_action(self!=NULL,bail,ret=SMCP_STATUS_BAD_ARGUMENT);

	if(!path) path = "";

	// Move past any preceding slashes.
	while(path && path[0] == '/' && path[1] == 0) path++;

	for(iter = smcp_get_first_pairing_for_path(self, path);
	    iter;
	    iter = smcp_next_pairing(self, iter)
	) {
		coap_transaction_id_t tid = (coap_transaction_id_t)smcp_get_next_tid(self,NULL);
		smcp_status_t status;

		if(!event) {
			// Allocate this lazily.
			event = calloc(1,sizeof(*event));

			require_action(event!=NULL, bail, ret=SMCP_STATUS_MALLOC_FAILURE);

			// This is just a placeholder event.
			event->contentFetcher = NULL;
			event->contentFetcherContext = NULL;
			event->refCount = 1;
			DEBUG_PRINTF("Event:%p: alloc",event);
		}

		if(iter->currentEvent) {
			DEBUG_PRINTF("Event:%p: Previous event, %p, is still around!",event,iter->currentEvent);
			smcp_invalidate_transaction(self, iter->last_tid);
		}

		iter->currentEvent = event;

		status = smcp_begin_transaction(
			self,
			tid,
			(iter->flags&SMCP_PARING_FLAG_RELIABILITY_MASK)?30000:10,
			0, // Flags
			(void*)&smcp_retry_event,
			(void*)&smcp_event_response_handler,
			(void*)iter
		);
		if(status)
			continue;

		iter->last_tid = tid;
		event->refCount++;
		DEBUG_PRINTF("Event:%p: retain",event);

#if SMCP_CONF_PAIRING_STATS
		iter->fire_count++;
#endif
	}

bail:
	if(event)
		smcp_event_tracker_release(event);
	return ret;
}

smcp_status_t
smcp_trigger_event_with_node(
	smcp_t		self,
	smcp_node_t			node,
	const char*			subpath
) {
	char path[SMCP_MAX_PATH_LENGTH + 1];

	path[sizeof(path) - 1] = 0;

	smcp_node_get_path(node, path, sizeof(path));

	if(subpath) {
		if(path[0])
			strncat(path, "/", sizeof(path) - 1);
		strncat(path, subpath, sizeof(path) - 1);
	}

	{   // Remove trailing slashes.
		size_t len = strlen(path);
		while(len && path[len - 1] == '/')
			path[--len] = 0;
	}

	return smcp_trigger_event(
		self,
		path
	);
}


smcp_status_t
smcp_trigger_custom_event(
	smcp_t		self,
	const char*			path,
	smcp_content_fetcher_func contentFetcher,
	void* context
) {
	smcp_status_t ret = 0;
	smcp_pairing_node_t iter;
	smcp_event_tracker_t event = NULL;

	if(!path) path = "";

	// Move past any preceding slashes.
	while(path && *path == '/') path++;

	for(iter = smcp_get_first_pairing_for_path(self, path);
	    iter;
	    iter = smcp_next_pairing(self, iter)
	) {
		coap_transaction_id_t tid = (coap_transaction_id_t)smcp_get_next_tid(self,NULL);
		smcp_status_t status;
		if(!event) {
			// Allocate this lazily.
			event = calloc(1,sizeof(*event));

			require_action(event!=NULL, bail, ret=SMCP_STATUS_MALLOC_FAILURE);

			event->contentFetcher = contentFetcher;
			event->contentFetcherContext = context;
			event->refCount = 1;
			DEBUG_PRINTF("Event:%p: alloc",event);
		}

		if(iter->currentEvent) {
			DEBUG_PRINTF("Event:%p: Previous event, %p, is still around!",event,iter->currentEvent);
			smcp_invalidate_transaction(self, iter->last_tid);
		}

		iter->currentEvent = event;

		status = smcp_begin_transaction(
			self,
			tid,
			(iter->flags&SMCP_PARING_FLAG_RELIABILITY_MASK)?30000:10,
			0, // Flags
			(void*)&smcp_retry_custom_event,
			(void*)&smcp_event_response_handler,
			(void*)iter
		);
		if(status)
			continue;

		iter->last_tid = tid;
		event->refCount++;
		DEBUG_PRINTF("Event:%p: retain",event);

#if SMCP_CONF_PAIRING_STATS
		iter->fire_count++;
#endif
	}

bail:
	if(event)
		smcp_event_tracker_release(event);
	return ret;
}

smcp_status_t
smcp_trigger_custom_event_with_node(
	smcp_t		self,
	smcp_node_t			node,
	const char*			subpath,
	smcp_content_fetcher_func contentFetcher,
	void* context
) {
	char path[SMCP_MAX_PATH_LENGTH + 1];

	path[sizeof(path) - 1] = 0;

	smcp_node_get_path(node, path, sizeof(path));

	if(subpath) {
		if(path[0])
			strncat(path, "/", sizeof(path) - 1);
		strncat(path, subpath, sizeof(path) - 1);
	}

	{   // Remove trailing slashes.
		size_t len = strlen(path);
		while(len && path[len - 1] == '/')
			path[--len] = 0;
	}

	return smcp_trigger_custom_event(
		self,
		path,
		contentFetcher,
		context
	);
}

smcp_status_t
smcp_refresh_variable(
	smcp_t self, smcp_variable_node_t node
) {
	smcp_status_t ret = 0;

	require_action(self != NULL, bail, ret = SMCP_STATUS_INVALID_ARGUMENT);
	require_action(node != NULL, bail, ret = SMCP_STATUS_INVALID_ARGUMENT);

#if 1
	ret = smcp_trigger_event_with_node(
		self,
		(smcp_node_t)node,
		NULL
	);
#else
	ret = smcp_trigger_custom_event_with_node(
		self,
		(smcp_node_t)node,
		NULL,
		(void*)node->get_func,
		node
	);
#endif

bail:
	return ret;
}

extern smcp_status_t
smcp_delete_pairing(smcp_t self, smcp_pairing_node_t pairing) {
	smcp_node_t parent = (smcp_node_t)pairing->node.parent;
	if(pairing->currentEvent) {
		smcp_event_tracker_release(pairing->currentEvent);
		smcp_invalidate_transaction(self, pairing->last_tid);
	}
	smcp_node_delete(&pairing->node);
	if(parent && !parent->children) {
		smcp_node_delete(parent);
	}
	return SMCP_STATUS_OK;
}


#pragma mark -
#pragma mark Pairing Node Interface

static smcp_status_t
smcp_pairing_path_request_handler(
	smcp_node_t		node,
	smcp_method_t	method
) {
	smcp_status_t ret = 0;
	smcp_t const self = smcp_get_current_instance();
	uintptr_t idVal;

	if(	method == COAP_METHOD_POST
		|| method == COAP_METHOD_PUT
	) {
		const char* value = NULL;
		size_t value_len;
#if HAS_ALLOCA
		char* path = NULL;
		char* uri = NULL;
#else
		static char path[SMCP_MAX_PATH_LENGTH];
		static char uri[SMCP_MAX_URI_LENGTH];
#endif
		if(node == self->root_pairing_node) {
			require(
				COAP_HEADER_URI_PATH==smcp_inbound_next_option((const uint8_t**)&value, &value_len),
				bail
			);
#if HAS_ALLOCA
			path = alloca(value_len+1);
#endif
			memcpy(path,value,value_len);
			path[value_len] = 0;

		} else if(node->parent == self->root_pairing_node) {
#if HAS_ALLOCA
			path = (char*)node->name;
#else
			strcpy(path,(char*)node->name);
#endif
		}
		if(node->parent == self->root_pairing_node || node->parent == self->root_pairing_node) {
			require_action(
				COAP_HEADER_URI_PATH==smcp_inbound_next_option((const uint8_t**)&value, &value_len),
				bail,
				ret = SMCP_STATUS_NOT_ALLOWED
			);
#if HAS_ALLOCA
			uri = alloca(value_len+1);
#endif
			memcpy(uri,value,value_len);
			uri[value_len] = 0;
		}

#if SMCP_USE_BSD_SOCKETS
		ret = smcp_pair_with_sockaddr(
			self,
			path,
			uri,
			smcp_inbound_get_saddr(),
			smcp_inbound_get_socklen(),
			SMCP_PARING_FLAG_RELIABILITY_PART,
			&idVal
		);
#elif CONTIKI
		ret = smcp_pair_with_sockaddr(
			self,
			path,
			uri,
			smcp_inbound_get_ipaddr(),
			smcp_inbound_get_ipport(),
			SMCP_PARING_FLAG_RELIABILITY_PART,
			&idVal
		);
#else
		ret = smcp_pair_with_uri(
			self,
			path,
			uri,
			SMCP_PARING_FLAG_RELIABILITY_PART,
			&idVal
		);
#endif
		smcp_outbound_begin_response(COAP_RESULT_201_CREATED);
		smcp_outbound_add_option(COAP_HEADER_LOCATION_PATH, self->root_pairing_node->name, HEADER_CSTR_LEN);
		smcp_outbound_add_option(COAP_HEADER_LOCATION_PATH, path, HEADER_CSTR_LEN);
		smcp_outbound_add_option(COAP_HEADER_LOCATION_PATH, uri, HEADER_CSTR_LEN);
		smcp_outbound_send();
	} else {
		ret = smcp_default_request_handler(node, method);
	}

bail:
	return ret;
}

static smcp_status_t
smcp_pairing_node_request_handler(
	smcp_pairing_node_t		pairing,
	smcp_method_t	method
) {
	smcp_status_t ret = 0;
	smcp_t const self = smcp_get_current_instance();

	enum {
		PATH_ROOT,
		PATH_DEST_URI,
		PATH_FLAGS,
#if SMCP_CONF_PAIRING_STATS
		PATH_FIRE_COUNT,
		PATH_SEND_COUNT,
		PATH_ERROR_COUNT,
		PATH_LAST_ERROR,
#endif
#if SMCP_CONF_USE_SEQ
		PATH_SEQ,
		PATH_ACK,
#endif
	} path = PATH_ROOT;

	if(smcp_inbound_option_strequal(COAP_HEADER_URI_PATH,"d"))
		path = PATH_DEST_URI;
	else if(smcp_inbound_option_strequal(COAP_HEADER_URI_PATH,"f"))
		path = PATH_FLAGS;
#if SMCP_CONF_PAIRING_STATS
	else if(smcp_inbound_option_strequal(COAP_HEADER_URI_PATH,"fc"))
		path = PATH_FIRE_COUNT;
	else if(smcp_inbound_option_strequal(COAP_HEADER_URI_PATH,"tx"))
		path = PATH_SEND_COUNT;
	else if(smcp_inbound_option_strequal(COAP_HEADER_URI_PATH,"ec"))
		path = PATH_ERROR_COUNT;
	else if(smcp_inbound_option_strequal(COAP_HEADER_URI_PATH,"err"))
		path = PATH_LAST_ERROR;
#endif
#if SMCP_CONF_USE_SEQ
	else if(smcp_inbound_option_strequal(COAP_HEADER_URI_PATH,"seq"))
		path = PATH_SEQ;
	else if(smcp_inbound_option_strequal(COAP_HEADER_URI_PATH,"ack"))
		path = PATH_ACK;
#endif

	if(path!=PATH_ROOT)
		smcp_inbound_next_option(NULL,NULL);

	{
		coap_option_key_t key;
		const uint8_t* value;
		size_t value_len;
		while((key=smcp_inbound_next_option(&value, &value_len))!=COAP_HEADER_INVALID) {
			require_action(key!=COAP_HEADER_URI_PATH,bail,ret=SMCP_STATUS_NOT_FOUND);

			require_action(
				!COAP_HEADER_IS_REQUIRED(key),
				bail,
				ret=SMCP_STATUS_BAD_OPTION
			);
		}
	}

	if(method == COAP_METHOD_GET) {
		if(path==PATH_DEST_URI) {
			smcp_outbound_begin_response(HTTP_TO_COAP_CODE(HTTP_RESULT_CODE_CONTENT));
			smcp_outbound_append_content(smcp_pairing_get_uri_cstr(pairing), HEADER_CSTR_LEN);
			ret = smcp_outbound_send();
		} else if(path==PATH_FLAGS) {
			smcp_outbound_begin_response(HTTP_TO_COAP_CODE(HTTP_RESULT_CODE_CONTENT));
			smcp_outbound_set_var_content_unsigned_int(pairing->flags);
			ret = smcp_outbound_send();
#if SMCP_CONF_USE_SEQ
		} else if(path==PATH_SEQ) {
			smcp_outbound_begin_response(HTTP_TO_COAP_CODE(HTTP_RESULT_CODE_CONTENT));
			smcp_outbound_set_var_content_unsigned_int(pairing->seq);
			ret = smcp_outbound_send();
		} else if(path==PATH_ACK) {
			smcp_outbound_begin_response(HTTP_TO_COAP_CODE(HTTP_RESULT_CODE_CONTENT));
			smcp_outbound_set_var_content_unsigned_int(pairing->ack);
			ret = smcp_outbound_send();
#endif
#if SMCP_CONF_PAIRING_STATS
		} else if(path==PATH_FIRE_COUNT) {
			smcp_outbound_begin_response(HTTP_TO_COAP_CODE(HTTP_RESULT_CODE_CONTENT));
			smcp_outbound_set_var_content_unsigned_int(pairing->fire_count);
			ret = smcp_outbound_send();
		} else if(path==PATH_SEND_COUNT) {
			smcp_outbound_begin_response(HTTP_TO_COAP_CODE(HTTP_RESULT_CODE_CONTENT));
			smcp_outbound_set_var_content_unsigned_int(pairing->send_count);
			ret = smcp_outbound_send();
		} else if(path==PATH_ERROR_COUNT) {
			smcp_outbound_begin_response(HTTP_TO_COAP_CODE(HTTP_RESULT_CODE_CONTENT));
			smcp_outbound_set_var_content_unsigned_int(pairing->errors);
			ret = smcp_outbound_send();
		} else if(path==PATH_LAST_ERROR) {
			smcp_outbound_begin_response(HTTP_TO_COAP_CODE(HTTP_RESULT_CODE_CONTENT));
			smcp_outbound_set_var_content_int(pairing->last_error);
			ret = smcp_outbound_send();
#endif
		} else if(path==PATH_ROOT) {
			const char rcontent[] = (
				"<d>;n=\"Dest URI\","
				"<f>;n=\"Flags\""
#if SMCP_CONF_PAIRING_STATS
				",<fc>;n=\"Fire Count\","
				"<tx>;n=\"Send Count\","
				"<ec>;n=\"Error Count\","
				"<err>;n=\"Last Error\""
#endif
#if SMCP_CONF_USE_SEQ
				",<seq>,"
				"<ack>"
#endif
			);
			smcp_outbound_begin_response(HTTP_TO_COAP_CODE(HTTP_RESULT_CODE_CONTENT));
			smcp_outbound_set_content_type(COAP_CONTENT_TYPE_APPLICATION_LINK_FORMAT);
			smcp_outbound_append_content(rcontent, HEADER_CSTR_LEN);
			ret = smcp_outbound_send();
		} else {
			ret = smcp_default_request_handler(&pairing->node, method);
		}
	} else if(method == COAP_METHOD_POST && method == COAP_METHOD_PUT) {
		if(path==PATH_FLAGS) {
			// TODO: Fix this!
			ret = SMCP_STATUS_NOT_ALLOWED;
		} else {
			ret = SMCP_STATUS_NOT_ALLOWED;
		}
	} else if(method == COAP_METHOD_DELETE) {
		if(path==PATH_ROOT) {
			ret = smcp_delete_pairing(self, pairing);
			require_noerr(ret, bail);
			smcp_outbound_begin_response(COAP_RESULT_202_DELETED);
			smcp_outbound_send();
		} else {
			ret = SMCP_STATUS_NOT_ALLOWED;
		}
	} else {
		ret = SMCP_STATUS_NOT_IMPLEMENTED;
	}

bail:
	return ret;
}

smcp_root_pairing_node_t smcp_pairing_init(
	smcp_t self, smcp_node_t parent, const char* name
) {
	if(!name)
		name = SMCP_PAIRING_DEFAULT_ROOT_PATH;
	require((self->root_pairing_node = smcp_node_init(NULL, parent, name)), bail);
	self->root_pairing_node->request_handler = &smcp_pairing_path_request_handler;

bail:
	return self->root_pairing_node;
}

#endif // #if SMCP_ENABLE_PAIRING
