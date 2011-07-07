/*
 * Copyright (C) 2011 Andreas Steffen, HSR Hochschule fuer Technik Rapperswil
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

#include "imc_scanner_state.h"

#include <imc/imc_agent.h>
#include <pa_tnc/pa_tnc_msg.h>
#include <ietf/ietf_attr.h>
#include <ietf/ietf_attr_pa_tnc_error.h>
#include <ietf/ietf_attr_port_filter.h>

#include <tncif_names.h>
#include <tncif_pa_subtypes.h>

#include <pen/pen.h>
#include <utils/lexparser.h>
#include <debug.h>

#include <stdio.h>

/* IMC definitions */

static const char imc_name[] = "Scanner";

#define IMC_VENDOR_ID	PEN_ITA
#define IMC_SUBTYPE		PA_SUBTYPE_ITA_SCANNER

static imc_agent_t *imc_scanner;
 
/**
 * see section 3.7.1 of TCG TNC IF-IMC Specification 1.2
 */
TNC_Result TNC_IMC_Initialize(TNC_IMCID imc_id,
							  TNC_Version min_version,
							  TNC_Version max_version,
							  TNC_Version *actual_version)
{
	if (imc_scanner)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has already been initialized", imc_name);
		return TNC_RESULT_ALREADY_INITIALIZED;
	}
	imc_scanner = imc_agent_create(imc_name, IMC_VENDOR_ID, IMC_SUBTYPE,
								imc_id, actual_version);
	if (!imc_scanner)
	{
		return TNC_RESULT_FATAL;
	}
	if (min_version > TNC_IFIMC_VERSION_1 || max_version < TNC_IFIMC_VERSION_1)
	{
		DBG1(DBG_IMC, "no common IF-IMC version");
		return TNC_RESULT_NO_COMMON_VERSION;
	}
	return TNC_RESULT_SUCCESS;
}

/**
 * see section 3.7.2 of TCG TNC IF-IMC Specification 1.2
 */
TNC_Result TNC_IMC_NotifyConnectionChange(TNC_IMCID imc_id,
										  TNC_ConnectionID connection_id,
										  TNC_ConnectionState new_state)
{
	imc_state_t *state;

	if (!imc_scanner)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	switch (new_state)
	{
		case TNC_CONNECTION_STATE_CREATE:
			state = imc_scanner_state_create(connection_id);
			return imc_scanner->create_state(imc_scanner, state);
		case TNC_CONNECTION_STATE_DELETE:
			return imc_scanner->delete_state(imc_scanner, connection_id);
		default:
			return imc_scanner->change_state(imc_scanner, connection_id,
											 new_state, NULL);
	}
}

/**
 * Determine all TCP and UDP server sockets listening on physical interfaces
 */
static bool do_netstat(ietf_attr_port_filter_t *attr)
{
	FILE *file;
	char buf[BUF_LEN];
	chunk_t line, token;
	int n = 0;
	bool success = FALSE;
	const char loopback_v4[] = "127.0.0.1";
	const char loopback_v6[] = "::1";

	/* Open a pipe stream for reading the output of the netstat commmand */
	file = popen("/bin/netstat -n -l -p -4 -6 --inet", "r");
	if (!file)
	{
		DBG1(DBG_IMC, "Failed to run netstat command");
		return FALSE;
	}

	/* Read the output a line at a time */
	while (fgets(buf, BUF_LEN-1, file))
	{
		u_char *pos;
		u_int8_t new_protocol, protocol;
		u_int16_t new_port, port;
		int i;
		enumerator_t *enumerator;
		bool allowed, found = FALSE;

		DBG2(DBG_IMC, "%.*s", strlen(buf)-1, buf);

		if (n++ < 2)
		{
			/* skip the first two header lines */
			continue;
		}
		line = chunk_create(buf, strlen(buf));

		/* Extract the IP protocol type */
		if (!extract_token(&token, ' ', &line))
		{
			DBG1(DBG_IMC, "Protocol field in netstat output not found");
			goto end;
		}
		if (match("tcp", &token) || match("tcp6", &token))
		{
			new_protocol = IPPROTO_TCP;
		}
		else if (match("udp", &token) || match("udp6", &token))
		{
			new_protocol = IPPROTO_UDP;
		}
		else
		{
			DBG1(DBG_IMC, "Skipped unknown IP protocol in netstat output");
			continue;
		}

		/* Skip the Recv-Q and Send-Q fields */
		for (i = 0; i < 3; i++)
		{
			if (!eat_whitespace(&line) || !extract_token(&token, ' ', &line))
			{
				token = chunk_empty;
				break;
			}
		}
		if (token.len == 0)
		{
			DBG1(DBG_IMC, "Local Address field in netstat output not found");
			goto end;
		}

		/* Find the local port appended to the local address */
		pos = token.ptr + token.len;
		while (*--pos != ':' && --token.len);
		if (*pos != ':')
		{
			DBG1(DBG_IMC, "Local port field in netstat output not found");
			goto end;
		}
		token.len--;

		/* ignore ports of IPv4 and IPv6 loopback interfaces */
		if ((token.len == strlen(loopback_v4) &&
				memeq(loopback_v4, token.ptr, token.len)) ||
			(token.len == strlen(loopback_v6) &&
				memeq(loopback_v6, token.ptr, token.len)))
		{
			continue;
		}

		/* convert the port string to an integer */
		new_port = atoi(pos+1);

		/* check if the there is already a port entry */
		enumerator = attr->create_port_enumerator(attr);
		while (enumerator->enumerate(enumerator, &allowed, &protocol, &port))
		{
			if (new_port == port && new_protocol == protocol)
			{
				found = TRUE;
			}
		}
		enumerator->destroy(enumerator);
		
		/* Skip the duplicate port entry */
		if (found)
		{
			continue;
		}

		/* Add new port entry */
		attr->add_port(attr, FALSE, new_protocol, new_port);
	}

	/* Successfully completed the parsing of the netstat output */
	success = TRUE;

end:
	/* Close the pipe stream */
	pclose(file);
	return success;
}

static TNC_Result send_message(TNC_ConnectionID connection_id)
{
	pa_tnc_msg_t *msg;
	pa_tnc_attr_t *attr;
	ietf_attr_port_filter_t *attr_port_filter;
	TNC_Result result;

	attr = ietf_attr_port_filter_create();
	attr->set_noskip_flag(attr, TRUE);
	attr_port_filter = (ietf_attr_port_filter_t*)attr;
	if (!do_netstat(attr_port_filter))
	{
		attr->destroy(attr);
		return TNC_RESULT_FATAL;
	}
	msg = pa_tnc_msg_create();
	msg->add_attribute(msg, attr);
	msg->build(msg);
	result = imc_scanner->send_message(imc_scanner, connection_id,
									   msg->get_encoding(msg));	
	msg->destroy(msg);

	return result;
}

/**
 * see section 3.7.3 of TCG TNC IF-IMC Specification 1.2
 */
TNC_Result TNC_IMC_BeginHandshake(TNC_IMCID imc_id,
								  TNC_ConnectionID connection_id)
{
	if (!imc_scanner)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	return send_message(connection_id);
}

/**
 * see section 3.7.4 of TCG TNC IF-IMC Specification 1.2
 */
TNC_Result TNC_IMC_ReceiveMessage(TNC_IMCID imc_id,
								  TNC_ConnectionID connection_id,
								  TNC_BufferReference msg,
								  TNC_UInt32 msg_len,
								  TNC_MessageType msg_type)
{
	pa_tnc_msg_t *pa_tnc_msg;
	pa_tnc_attr_t *attr;
	enumerator_t *enumerator;
	TNC_Result result;
	bool fatal_error = FALSE;

	if (!imc_scanner)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}

	/* parse received PA-TNC message and automatically handle any errors */ 
	result = imc_scanner->receive_message(imc_scanner, connection_id,
									   chunk_create(msg, msg_len), msg_type,
									   &pa_tnc_msg);

	/* no parsed PA-TNC attributes available if an error occurred */
	if (!pa_tnc_msg)
	{
		return result;
	}

	/* analyze PA-TNC attributes */
	enumerator = pa_tnc_msg->create_attribute_enumerator(pa_tnc_msg);
	while (enumerator->enumerate(enumerator, &attr))
	{
		ietf_attr_pa_tnc_error_t *error_attr;
		pa_tnc_error_code_t error_code;
		chunk_t msg_info, attr_info;
		u_int32_t offset;

		if (attr->get_vendor_id(attr) != PEN_IETF &&
			attr->get_type(attr) != IETF_ATTR_PA_TNC_ERROR)
		{
			continue;
		}

		error_attr = (ietf_attr_pa_tnc_error_t*)attr;
		error_code = error_attr->get_error_code(error_attr);
		msg_info = error_attr->get_msg_info(error_attr);
		DBG1(DBG_IMC, "received PA-TNC error '%N' concerning message %#B",
			 pa_tnc_error_code_names, error_code, &msg_info);

		switch (error_code)
		{
			case PA_ERROR_INVALID_PARAMETER:
				offset = error_attr->get_offset(error_attr);
				DBG1(DBG_IMC, "  occurred at offset of %u bytes", offset);
				break;
			case PA_ERROR_ATTR_TYPE_NOT_SUPPORTED:
				attr_info = error_attr->get_attr_info(error_attr);
				DBG1(DBG_IMC, "  unsupported attribute %#B", &attr_info);
				break;
			default:
				break;
		}
		fatal_error = TRUE;
	}
	enumerator->destroy(enumerator);
	pa_tnc_msg->destroy(pa_tnc_msg);

	/* if no error occurred then always return the same response */
	return fatal_error ? TNC_RESULT_FATAL : send_message(connection_id);
}

/**
 * see section 3.7.5 of TCG TNC IF-IMC Specification 1.2
 */
TNC_Result TNC_IMC_BatchEnding(TNC_IMCID imc_id,
							   TNC_ConnectionID connection_id)
{
	if (!imc_scanner)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	return TNC_RESULT_SUCCESS;
}

/**
 * see section 3.7.6 of TCG TNC IF-IMC Specification 1.2
 */
TNC_Result TNC_IMC_Terminate(TNC_IMCID imc_id)
{
	if (!imc_scanner)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	imc_scanner->destroy(imc_scanner);
	imc_scanner = NULL;

	return TNC_RESULT_SUCCESS;
}

/**
 * see section 4.2.8.1 of TCG TNC IF-IMC Specification 1.2
 */
TNC_Result TNC_IMC_ProvideBindFunction(TNC_IMCID imc_id,
									   TNC_TNCC_BindFunctionPointer bind_function)
{
	if (!imc_scanner)
	{
		DBG1(DBG_IMC, "IMC \"%s\" has not been initialized", imc_name);
		return TNC_RESULT_NOT_INITIALIZED;
	}
	return imc_scanner->bind_functions(imc_scanner, bind_function);
}