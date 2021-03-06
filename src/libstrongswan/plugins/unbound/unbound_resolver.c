/*
 * Copyright (C) 2011-2012 Reto Guadagnini
 * Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <unbound.h>
#include <errno.h>
#include <ldns/ldns.h>
#include <string.h>

#include <library.h>
#include <utils/debug.h>

#include "unbound_resolver.h"
#include "unbound_response.h"

/* DNS resolver configuration and DNSSEC trust anchors */
#define RESOLV_CONF_FILE	"/etc/resolv.conf"
#define TRUST_ANCHOR_FILE	IPSEC_CONFDIR "/ipsec.d/dnssec.keys"

typedef struct private_resolver_t private_resolver_t;

/**
 * private data of a unbound_resolver_t object.
 */
struct private_resolver_t {

	/**
	 * Public data
	 */
	resolver_t public;

	/**
	 * private unbound resolver handle (unbound context)
	 */
	struct ub_ctx *ctx;
};

/**
 * query method implementation
 */
METHOD(resolver_t, query, resolver_response_t*,
	private_resolver_t *this, char *domain, rr_class_t rr_class,
	rr_type_t rr_type)
{
	unbound_response_t *response = NULL;
	struct ub_result *result = NULL;
	int ub_retval;

	ub_retval = ub_resolve(this->ctx, domain, rr_type, rr_class, &result);
	if (ub_retval)
	{
		DBG1(DBG_LIB, "unbound resolver error: %s", ub_strerror(ub_retval));
		ub_resolve_free(result);
		return NULL;
	}

	response = unbound_response_create_frm_libub_response(result);
	if (!response)
	{
		DBG1(DBG_LIB, "unbound resolver failed to create response");
		ub_resolve_free(result);
		return NULL;
	}
	ub_resolve_free(result);

	return (resolver_response_t*)response;
}

/**
 * destroy method implementation
 */
METHOD(resolver_t, destroy, void,
	private_resolver_t *this)
{
	ub_ctx_delete(this->ctx);
	free(this);
}

/*
 * Described in header.
 */
resolver_t *unbound_resolver_create(void)
{
	private_resolver_t *this;
	int ub_retval = 0;
	char *resolv_conf_file;
	char *trust_anchor_file;

	resolv_conf_file = lib->settings->get_str(lib->settings,
						"libstrongswan.plugins.unbound.resolv_conf",
						RESOLV_CONF_FILE);

	trust_anchor_file = lib->settings->get_str(lib->settings,
						"libstrongswan.plugins.unbound.trust_anchors",
						TRUST_ANCHOR_FILE);

	INIT(this,
		.public = {
			.query = _query,
			.destroy = _destroy,
		},
	);

	this->ctx = ub_ctx_create();
	if (!this->ctx)
	{
		DBG1(DBG_LIB, "failed to create unbound resolver context");
		destroy(this);
		return NULL;
	}

	DBG1(DBG_CFG, "loading unbound resolver config from '%s'", resolv_conf_file);
	ub_retval = ub_ctx_resolvconf(this->ctx, resolv_conf_file);
	if (ub_retval)
	{
		DBG1(DBG_CFG, "failed to read the resolver config: %s (%s)",
					   ub_strerror(ub_retval), strerror(errno));
		destroy(this);
		return NULL;
	}

	DBG1(DBG_CFG, "loading unbound trust anchors from '%s'", trust_anchor_file);
	ub_retval = ub_ctx_add_ta_file(this->ctx, trust_anchor_file);
	if (ub_retval)
	{
		DBG1(DBG_CFG, "failed to load trust anchors: %s (%s)",
					   ub_strerror(ub_retval), strerror(errno));
	}

	return &this->public;
}

