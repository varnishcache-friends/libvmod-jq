/*
 * Copyright (c) 2018, Federico G. Schwindt <fgsch@lodoss.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>
#include <stdlib.h>

#include <jq.h>

#include "cache/cache_varnishd.h"

#include "vsb.h"

#include "vcc_if.h"

struct vmod_jq {
	unsigned	magic;
#define VMOD_JQ_MAGIC		0x012F0C2F
	jq_state	*state;
	jv		value;
};


static void
cleanup(void *ptr)
{
	struct vmod_jq *vp;

	CAST_OBJ(vp, ptr, VMOD_JQ_MAGIC);
	AN(jv_get_refcnt(vp->value) == 1);
	jv_free(vp->value);
	jq_teardown(&vp->state);
}

static void
error_callback(void *ptr, jv value)
{
	(void)ptr;

	AN(jv_get_refcnt(value) == 1);
	jv_free(value);
}

static int
iter_req_body(void *priv, int flush, const void *ptr,
    ssize_t len)
{
	(void)flush;

	VSB_bcat(priv, ptr, len);
	return (0);
}

VCL_BOOL
vmod_parse(VRT_CTX, struct vmod_priv *priv, VCL_ENUM from, VCL_STRING s)
{
	struct vmod_jq *vp;
	struct vsb *vsb;
	jv value;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	if (!strcmp(from, "string")) {
		if (!s || !*s) {
			VSLb(ctx->vsl, SLT_Error,
			    "jq.parse: Missing input");
			return (0);
		}
		value = jv_parse(s);
	} else if (!strcmp(from, "request")) {
		if (s && *s)
			VSLb(ctx->vsl, SLT_Debug,
			    "jq.parse: Using request, input ignored");

		CHECK_OBJ_ORNULL(ctx->req, REQ_MAGIC);
		if (!ctx->req) {
			VSLb(ctx->vsl, SLT_Error,
			    "jq.parse: Cannot parse request from"
			    " vcl_backend_*");
			return (0);
		}

		if (ctx->req->req_bodybytes <= 0) {
			VSLb(ctx->vsl, SLT_Error,
			    "jq.parse: Uncached or no request body");
			return (0);
		}

		vsb = VSB_new(NULL, NULL, ctx->req->req_bodybytes + 1, 0);
		AN(vsb);
		if (VRB_Iterate(ctx->req, iter_req_body, vsb) == -1) {
			VSLb(ctx->vsl, SLT_Error,
			    "jq.parse: Problem fetching the body");
			VSB_delete(vsb);
			return (0);
		}
		AZ(VSB_finish(vsb));
		assert(VSB_len(vsb) > 0);
		value = jv_parse(VSB_data(vsb));
		VSB_delete(vsb);
	} else
		WRONG("Illegal from");

	if (!jv_is_valid(value)) {
		value = jv_invalid_get_msg(value);
		VSLb(ctx->vsl, SLT_Error,
		    "jq.parse: %s", jv_string_value(value));
		jv_free(value);
		return (0);
	}

	CAST_OBJ(vp, priv->priv, VMOD_JQ_MAGIC);
	if (vp == NULL) {
		vp = WS_Alloc(ctx->ws, sizeof(*vp));
		AN(vp);
		INIT_OBJ(vp, VMOD_JQ_MAGIC);
		priv->priv = vp;
		priv->free = cleanup;
	} else {
		AN(jv_get_refcnt(vp->value) == 1);
		jv_free(vp->value);
	}

	vp->value = value;

	return (1);
}

VCL_STRING
vmod_get(VRT_CTX, struct vmod_priv *priv, VCL_STRING filter,
    VCL_STRING error, VCL_BOOL ascii, VCL_BOOL raw)
{
	struct vmod_jq *vp;
	jv value;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CAST_OBJ(vp, priv->priv, VMOD_JQ_MAGIC);
	if (!vp) {
		VSLb(ctx->vsl, SLT_Error,
		    "jq.get: No context");
		return (error);
	}

	if (!filter || !*filter) {
		VSLb(ctx->vsl, SLT_Error,
		    "jq.get: Missing filter");
		return (error);
	}

	if (!vp->state) {
		vp->state = jq_init();
		if (!vp->state) {
			VSLb(ctx->vsl, SLT_Error,
			    "jq.get: Out of memory");
			return (error);
		}
		jq_set_error_cb(vp->state, error_callback, NULL);
	}

	if (!jq_compile(vp->state, filter)) {
		VSLb(ctx->vsl, SLT_Error,
		    "jq.get: Invalid filter");
		return (error);
	}

	jq_start(vp->state, jv_copy(vp->value), 0);
	value = jq_next(vp->state);
	if (!jv_is_valid(value)) {
		value = jv_invalid_get_msg(value);
		VSLb(ctx->vsl, SLT_Error,
		    "jq.get: %s", jv_string_value(value));
		jv_free(value);
		return (error);
	}

	if (!raw || jv_get_kind(value) != JV_KIND_STRING) {
		value = jv_dump_string(value,
		    ascii ? JV_PRINT_ASCII : 0);
	}
	return (jv_string_value(value));
}

VCL_BOOL
vmod_is_a(VRT_CTX, struct vmod_priv *priv, VCL_ENUM e)
{
	struct vmod_jq *vp;
	jv_kind kind;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CAST_OBJ(vp, priv->priv, VMOD_JQ_MAGIC);
	if (!vp) {
		VSLb(ctx->vsl, SLT_Error,
		    "jq.is_a: No context");
		return (0);
	}

	kind = jv_get_kind(vp->value);
	if (!strcmp(e, "invalid"))
		return (kind == JV_KIND_INVALID);
	else if (!strcmp(e, "null"))
		return (kind == JV_KIND_NULL);
	else if (!strcmp(e, "false"))
		return (kind == JV_KIND_FALSE);
	else if (!strcmp(e, "true"))
		return (kind == JV_KIND_TRUE);
	else if (!strcmp(e, "number"))
		return (kind == JV_KIND_NUMBER);
	else if (!strcmp(e, "string"))
		return (kind == JV_KIND_STRING);
	else if (!strcmp(e, "array"))
		return (kind == JV_KIND_ARRAY);
	else if (!strcmp(e, "object"))
		return (kind == JV_KIND_OBJECT);

	WRONG("Illegal kind");
}

VCL_INT
vmod_length(VRT_CTX, struct vmod_priv *priv)
{
	struct vmod_jq *vp;
	jv_kind kind;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CAST_OBJ(vp, priv->priv, VMOD_JQ_MAGIC);
	if (!vp) {
		VSLb(ctx->vsl, SLT_Error,
		    "jq.length: No context");
		return (-1);
	}

	kind = jv_get_kind(vp->value);
	switch (kind) {
	case JV_KIND_INVALID:
		VSLb(ctx->vsl, SLT_Error,
		    "jq.length: Invalid object");
		return (-1);

	case JV_KIND_STRING:
		return (jv_string_length_bytes(jv_copy(vp->value)));

	case JV_KIND_ARRAY:
		return (jv_array_length(jv_copy(vp->value)));

	case JV_KIND_OBJECT:
		return (jv_object_length(jv_copy(vp->value)));

	default:
		VSLb(ctx->vsl, SLT_Debug,
		    "jq.length: %s type has no length",
		    jv_kind_name(kind));
		return (0);
	}
}
