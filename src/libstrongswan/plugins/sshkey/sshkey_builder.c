/*
 * Copyright (C) 2013 Tobias Brunner
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

#include "sshkey_builder.h"

#include <asn1/oid.h>
#include <asn1/asn1.h>
#include <bio/bio_reader.h>
#include <utils/debug.h>

#define ECDSA_PREFIX "ecdsa-sha2-"

/**
 * Parse an EC domain parameter identifier as defined in RFC 5656
 */
static chunk_t parse_ec_identifier(chunk_t identifier)
{
	chunk_t oid = chunk_empty;

	if (chunk_equals(identifier, chunk_from_str("nistp256")))
	{
		oid = asn1_build_known_oid(OID_PRIME256V1);
	}
	else if (chunk_equals(identifier, chunk_from_str("nistp384")))
	{
		oid = asn1_build_known_oid(OID_SECT384R1);
	}
	else if (chunk_equals(identifier, chunk_from_str("nistp521")))
	{
		oid = asn1_build_known_oid(OID_SECT521R1);
	}
	else
	{
		char ascii[64];

		if (snprintf(ascii, sizeof(ascii), "%.*s", (int)identifier.len,
					 identifier.ptr) < sizeof(ascii))
		{
			oid = asn1_wrap(ASN1_OID, "m", asn1_oid_from_string(ascii));
		}
	}
	return oid;
}

/**
 * Load a generic public key from an SSH key blob
 */
static sshkey_public_key_t *parse_public_key(chunk_t blob)
{
	bio_reader_t *reader;
	chunk_t format;

	reader = bio_reader_create(blob);
	if (!reader->read_data32(reader, &format))
	{
		DBG1(DBG_LIB, "invalid key format in SSH key");
		reader->destroy(reader);
		return NULL;
	}
	if (chunk_equals(format, chunk_from_str("ssh-rsa")))
	{
		chunk_t n, e;

		if (!reader->read_data32(reader, &e) ||
			!reader->read_data32(reader, &n))
		{
			DBG1(DBG_LIB, "invalid RSA key in SSH key");
			reader->destroy(reader);
			return NULL;
		}
		reader->destroy(reader);
		return lib->creds->create(lib->creds, CRED_PUBLIC_KEY, KEY_RSA,
						BUILD_RSA_MODULUS, n, BUILD_RSA_PUB_EXP, e, BUILD_END);
	}
	else if (format.len > strlen(ECDSA_PREFIX) &&
			 strneq(format.ptr, ECDSA_PREFIX, strlen(ECDSA_PREFIX)))
	{
		chunk_t ec_blob, identifier, q, oid, encoded;
		sshkey_public_key_t *key;

		ec_blob = reader->peek(reader);
		reader->destroy(reader);
		reader = bio_reader_create(ec_blob);
		if (!reader->read_data32(reader, &identifier) ||
			!reader->read_data32(reader, &q))
		{
			DBG1(DBG_LIB, "invalid ECDSA key in SSH key");
			reader->destroy(reader);
			return NULL;
		}
		oid = parse_ec_identifier(identifier);
		if (!oid.ptr)
		{
			DBG1(DBG_LIB, "invalid ECDSA key identifier in SSH key");
			reader->destroy(reader);
			return NULL;
		}
		reader->destroy(reader);
		/* build key from subjectPublicKeyInfo */
		encoded = asn1_wrap(ASN1_SEQUENCE, "mm",
						asn1_wrap(ASN1_SEQUENCE, "mm",
							asn1_build_known_oid(OID_EC_PUBLICKEY), oid),
						asn1_bitstring("c", q));
		key = lib->creds->create(lib->creds, CRED_PUBLIC_KEY,
							KEY_ECDSA, BUILD_BLOB_ASN1_DER, encoded, BUILD_END);
		chunk_free(&encoded);
		return key;
	}
	DBG1(DBG_LIB, "unsupported SSH key format %.*s", (int)format.len,
		 format.ptr);
	reader->destroy(reader);
	return NULL;
}

/**
 * See header.
 */
sshkey_public_key_t *sshkey_public_key_load(key_type_t type, va_list args)
{
	chunk_t blob = chunk_empty;

	while (TRUE)
	{
		switch (va_arg(args, builder_part_t))
		{
			case BUILD_BLOB_SSHKEY:
				blob = va_arg(args, chunk_t);
				continue;
			case BUILD_END:
				break;
			default:
				return NULL;
		}
		break;
	}
	if (blob.ptr && type == KEY_ANY)
	{
		return parse_public_key(blob);
	}
	return NULL;
}
