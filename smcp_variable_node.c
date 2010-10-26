#include "assert_macros.h"
#include "smcp_node.h"
#include "smcp_variable_node.h"
#include <stdlib.h>

void
smcp_variable_node_dealloc(smcp_variable_node_t x) {
	free(x);
}

smcp_variable_node_t
smcp_variable_node_alloc() {
	smcp_variable_node_t ret =
	    (smcp_variable_node_t)calloc(sizeof(struct smcp_variable_node_s),
		1);

	ret->node.finalize = (void (*)(smcp_node_t)) &
	    smcp_variable_node_dealloc;
	return ret;
}


smcp_status_t
smcp_variable_request_handler(
	smcp_daemon_t		self,
	smcp_node_t			node,
	smcp_method_t		method,
	const char*			path,
	coap_header_item_t	headers[],
	const char*			content,
	size_t				content_length
) {
	smcp_status_t ret = SMCP_STATUS_NOT_FOUND;

	require(node, bail);

	if(path && path[0] != 0) {
		ret = SMCP_STATUS_NOT_FOUND;
		goto bail;
	}


	if(method == SMCP_METHOD_PUT)
		method = SMCP_METHOD_POST;

	if(method == SMCP_METHOD_POST) {
		smcp_content_type_t content_type = SMCP_CONTENT_TYPE_TEXT_PLAIN;

		coap_header_item_t *next_header;
		for(next_header = headers;
		    smcp_header_item_get_key(next_header);
		    next_header = smcp_header_item_next(next_header)) {
			if(smcp_header_item_key_equal(next_header,
					COAP_HEADER_CONTENT_TYPE))
				content_type = *(unsigned char*)smcp_header_item_get_value(
					next_header);
		}
		ret = SMCP_STATUS_NOT_IMPLEMENTED;
		if(((smcp_variable_node_t)node)->post_func) {
			ret = (*((smcp_variable_node_t)node)->post_func)(
				    ((smcp_variable_node_t)node),
				headers,
				    (char*)content,
				content_length,
				content_type
			    );
		}
	} else if(method == SMCP_METHOD_GET) {
		char replyContent[SMCP_MAX_CONTENT_LENGTH];
		size_t replyContentLength = sizeof(replyContent);
		smcp_content_type_t replyContentType =
		    SMCP_CONTENT_TYPE_TEXT_PLAIN;
		coap_header_item_t replyHeaders[2] = {};

		ret = SMCP_STATUS_NOT_IMPLEMENTED;
		if(((smcp_variable_node_t)node)->get_func) {
			ret = (*((smcp_variable_node_t)node)->get_func)(
				    (smcp_variable_node_t)node,
				headers,
				replyContent,
				&replyContentLength,
				&replyContentType
			    );
		}

		if(ret == SMCP_STATUS_OK) {
			util_add_header(replyHeaders, 1, COAP_HEADER_CONTENT_TYPE,
				    (const void*)&replyContentType, 1);
			smcp_daemon_send_response(self,
				SMCP_RESULT_CODE_OK,
				replyHeaders,
				replyContent,
				replyContentLength);
		}
	} else {
		ret = SMCP_STATUS_NOT_IMPLEMENTED;
	}

bail:
	return ret;
}

smcp_variable_node_t
smcp_node_init_variable(
	smcp_variable_node_t self, smcp_node_t node, const char* name
) {
	smcp_variable_node_t ret = NULL;

	require(node, bail);
	require((node->type == SMCP_NODE_DEVICE), bail);

	require(self || (self = smcp_variable_node_alloc()), bail);

	ret = (smcp_variable_node_t)self;

	ret->node.type = SMCP_NODE_VARIABLE;
	ret->node.name = name;
	ret->node.unhandled_request = &smcp_variable_request_handler;

	if(node) {
		bt_insert(
			    (void**)&((smcp_device_node_t)node)->variables,
			ret,
			    (bt_compare_func_t)smcp_node_compare,
			    (bt_delete_func_t)smcp_node_delete,
			NULL
		);
		ret->node.parent = node;
	}

bail:
	return ret;
}
