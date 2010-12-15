/*
 * Copyright (C) 2010 Martin Willi
 * Copyright (C) 2010 revosec AG
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

#include "pki.h"

#include <asn1/asn1.h>
#include <credentials/certificates/certificate.h>
#include <credentials/certificates/x509.h>
#include <credentials/certificates/crl.h>
#include <selectors/traffic_selector.h>

#include <time.h>

/**
 * Print public key information
 */
static void print_pubkey(public_key_t *key)
{
	chunk_t chunk;

	printf("pubkey:    %N %d bits\n", key_type_names, key->get_type(key),
		   key->get_keysize(key));
	if (key->get_fingerprint(key, KEYID_PUBKEY_INFO_SHA1, &chunk))
	{
		printf("keyid:     %#B\n", &chunk);
	}
	if (key->get_fingerprint(key, KEYID_PUBKEY_SHA1, &chunk))
	{
		printf("subjkey:   %#B\n", &chunk);
	}
}

/**
 * Print private key information
 */
static void print_key(private_key_t *key)
{
	public_key_t *public;

	public = key->get_public_key(key);
	if (public)
	{
		printf("private key with:\n");
		print_pubkey(public);
		public->destroy(public);
	}
	else
	{
		printf("extracting public from private key failed\n");
	}
}

/**
 * Print X509 specific certificate information
 */
static void print_x509(x509_t *x509)
{
	enumerator_t *enumerator;
	identification_t *id;
	traffic_selector_t *block;
	chunk_t chunk;
	bool first;
	char *uri;
	int len;
	x509_flag_t flags;
	x509_cert_policy_t *policy;
	x509_policy_mapping_t *mapping;

	chunk = x509->get_serial(x509);
	printf("serial:    %#B\n", &chunk);

	first = TRUE;
	enumerator = x509->create_subjectAltName_enumerator(x509);
	while (enumerator->enumerate(enumerator, &id))
	{
		if (first)
		{
			printf("altNames:  ");
			first = FALSE;
		}
		else
		{
			printf(", ");
		}
		printf("%Y", id);
	}
	if (!first)
	{
		printf("\n");
	}
	enumerator->destroy(enumerator);

	flags = x509->get_flags(x509);
	printf("flags:     ");
	if (flags & X509_CA)
	{
		printf("CA ");
	}
	if (flags & X509_CRL_SIGN)
	{
		printf("CRLSign ");
	}
	if (flags & X509_AA)
	{
		printf("AA ");
	}
	if (flags & X509_OCSP_SIGNER)
	{
		printf("OCSP ");
	}
	if (flags & X509_AA)
	{
		printf("AA ");
	}
	if (flags & X509_SERVER_AUTH)
	{
		printf("serverAuth ");
	}
	if (flags & X509_CLIENT_AUTH)
	{
		printf("clientAuth ");
	}
	if (flags & X509_SELF_SIGNED)
	{
		printf("self-signed ");
	}
	printf("\n");

	first = TRUE;
	enumerator = x509->create_crl_uri_enumerator(x509);
	while (enumerator->enumerate(enumerator, &uri, &id))
	{
		if (first)
		{
			printf("CRL URIs:  %s", uri);
			first = FALSE;
		}
		else
		{
			printf("           %s", uri);
		}
		if (id)
		{
			printf(" (CRL issuer: %Y)", id);
		}
		printf("\n");
	}
	enumerator->destroy(enumerator);

	first = TRUE;
	enumerator = x509->create_ocsp_uri_enumerator(x509);
	while (enumerator->enumerate(enumerator, &uri))
	{
		if (first)
		{
			printf("OCSP URIs: %s\n", uri);
			first = FALSE;
		}
		else
		{
			printf("           %s\n", uri);
		}
	}
	enumerator->destroy(enumerator);

	len = x509->get_pathLenConstraint(x509);
	if (len != X509_NO_CONSTRAINT)
	{
		printf("pathlen:   %d\n", len);
	}

	first = TRUE;
	enumerator = x509->create_name_constraint_enumerator(x509, TRUE);
	while (enumerator->enumerate(enumerator, &id))
	{
		if (first)
		{
			printf("Permitted NameConstraints:\n");
			first = FALSE;
		}
		printf("           %Y\n", id);
	}
	enumerator->destroy(enumerator);
	first = TRUE;
	enumerator = x509->create_name_constraint_enumerator(x509, FALSE);
	while (enumerator->enumerate(enumerator, &id))
	{
		if (first)
		{
			printf("Excluded NameConstraints:\n");
			first = FALSE;
		}
		printf("           %Y\n", id);
	}
	enumerator->destroy(enumerator);

	first = TRUE;
	enumerator = x509->create_cert_policy_enumerator(x509);
	while (enumerator->enumerate(enumerator, &policy))
	{
		char *oid;

		if (first)
		{
			printf("CertificatePolicies:\n");
			first = FALSE;
		}
		oid = asn1_oid_to_string(policy->oid);
		if (oid)
		{
			printf("           %s\n", oid);
			free(oid);
		}
		else
		{
			printf("           %#B\n", &policy->oid);
		}
		if (policy->cps_uri)
		{
			printf("             CPS: %s\n", policy->cps_uri);
		}
		if (policy->unotice_text)
		{
			printf("             Notice: %s\n", policy->unotice_text);

		}
	}
	enumerator->destroy(enumerator);

	first = TRUE;
	enumerator = x509->create_policy_mapping_enumerator(x509);
	while (enumerator->enumerate(enumerator, &mapping))
	{
		char *issuer_oid, *subject_oid;

		if (first)
		{
			printf("PolicyMappings:\n");
			first = FALSE;
		}
		issuer_oid = asn1_oid_to_string(mapping->issuer);
		subject_oid = asn1_oid_to_string(mapping->subject);
		printf("           %s => %s\n", issuer_oid, subject_oid);
		free(issuer_oid);
		free(subject_oid);
	}
	enumerator->destroy(enumerator);

	if (x509->get_policyConstraint(x509, FALSE) != X509_NO_CONSTRAINT ||
		x509->get_policyConstraint(x509, TRUE) != X509_NO_CONSTRAINT)
	{
		printf("PolicyConstraints:\n");
		if (x509->get_policyConstraint(x509, FALSE) != X509_NO_CONSTRAINT)
		{
			printf("           requireExplicitPolicy: %d\n",
				   x509->get_policyConstraint(x509, FALSE));
		}
		if (x509->get_policyConstraint(x509, TRUE) != X509_NO_CONSTRAINT)
		{
			printf("           inhibitPolicyMapping: %d\n",
				   x509->get_policyConstraint(x509, TRUE));
		}
	}

	chunk = x509->get_authKeyIdentifier(x509);
	if (chunk.ptr)
	{
		printf("authkeyId: %#B\n", &chunk);
	}

	chunk = x509->get_subjectKeyIdentifier(x509);
	if (chunk.ptr)
	{
		printf("subjkeyId: %#B\n", &chunk);
	}
	if (x509->get_flags(x509) & X509_IP_ADDR_BLOCKS)
	{
		first = TRUE;
		printf("addresses: ");
		enumerator = x509->create_ipAddrBlock_enumerator(x509);
		while (enumerator->enumerate(enumerator, &block))
		{
			if (first)
			{
				first = FALSE;
			}
			else
			{
				printf(", ");
			}
			printf("%R", block);
		}
		enumerator->destroy(enumerator);
		printf("\n");
	}
}

/**
 * Print CRL specific information
 */
static void print_crl(crl_t *crl)
{
	enumerator_t *enumerator;
	time_t ts;
	crl_reason_t reason;
	chunk_t chunk;
	int count = 0;
	char buf[64];
	struct tm tm;

	chunk = crl->get_serial(crl);
	printf("serial:    %#B\n", &chunk);
	chunk = crl->get_authKeyIdentifier(crl);
	printf("authKeyId: %#B\n", &chunk);

	enumerator = crl->create_enumerator(crl);
	while (enumerator->enumerate(enumerator, &chunk, &ts, &reason))
	{
		count++;
	}
	enumerator->destroy(enumerator);

	printf("%d revoked certificate%s%s\n", count,
		   count == 1 ? "" : "s", count ? ":" : "");
	enumerator = crl->create_enumerator(crl);
	while (enumerator->enumerate(enumerator, &chunk, &ts, &reason))
	{
		localtime_r(&ts, &tm);
		strftime(buf, sizeof(buf), "%F %T", &tm);
		printf("    %#B %N %s\n", &chunk, crl_reason_names, reason, buf);
		count++;
	}
	enumerator->destroy(enumerator);
}

/**
 * Print certificate information
 */
static void print_cert(certificate_t *cert)
{
	time_t now, notAfter, notBefore;
	public_key_t *key;

	now = time(NULL);

	printf("cert:      %N\n", certificate_type_names, cert->get_type(cert));
	if (cert->get_type(cert) != CERT_X509_CRL)
	{
		printf("subject:  \"%Y\"\n", cert->get_subject(cert));
	}
	printf("issuer:   \"%Y\"\n", cert->get_issuer(cert));

	cert->get_validity(cert, &now, &notBefore, &notAfter);
	printf("validity:  not before %T, ", &notBefore, FALSE);
	if (now < notBefore)
	{
		printf("not valid yet (valid in %V)\n", &now, &notBefore);
	}
	else
	{
		printf("ok\n");
	}
	printf("           not after  %T, ", &notAfter, FALSE);
	if (now > notAfter)
	{
		printf("expired (%V ago)\n", &now, &notAfter);
	}
	else
	{
		printf("ok (expires in %V)\n", &now, &notAfter);
	}

	switch (cert->get_type(cert))
	{
		case CERT_X509:
			print_x509((x509_t*)cert);
			break;
		case CERT_X509_CRL:
			print_crl((crl_t*)cert);
			break;
		default:
			printf("parsing certificate subtype %N not implemented\n",
				   certificate_type_names, cert->get_type(cert));
			break;
	}
	key = cert->get_public_key(cert);
	if (key)
	{
		print_pubkey(key);
		key->destroy(key);
	}
}

/**
 * Print a credential in a human readable form
 */
static int print()
{
	credential_type_t type = CRED_CERTIFICATE;
	int subtype = CERT_X509;
	void *cred;
	char *arg, *file = NULL;

	while (TRUE)
	{
		switch (command_getopt(&arg))
		{
			case 'h':
				return command_usage(NULL);
			case 't':
				if (streq(arg, "x509"))
				{
					type = CRED_CERTIFICATE;
					subtype = CERT_X509;
				}
				else if (streq(arg, "crl"))
				{
					type = CRED_CERTIFICATE;
					subtype = CERT_X509_CRL;
				}
				else if (streq(arg, "pub"))
				{
					type = CRED_PUBLIC_KEY;
					subtype = KEY_ANY;
				}
				else if (streq(arg, "rsa-priv"))
				{
					type = CRED_PRIVATE_KEY;
					subtype = KEY_RSA;
				}
				else if (streq(arg, "ecdsa-priv"))
				{
					type = CRED_PRIVATE_KEY;
					subtype = KEY_ECDSA;
				}
				else
				{
					return command_usage( "invalid input type");
				}
				continue;
			case 'i':
				file = arg;
				continue;
			case EOF:
				break;
			default:
				return command_usage("invalid --print option");
		}
		break;
	}
	if (file)
	{
		cred = lib->creds->create(lib->creds, type, subtype,
								  BUILD_FROM_FILE, file, BUILD_END);
	}
	else
	{
		cred = lib->creds->create(lib->creds, type, subtype,
								  BUILD_FROM_FD, 0, BUILD_END);
	}
	if (!cred)
	{
		fprintf(stderr, "parsing input failed\n");
		return 1;
	}

	if (type == CRED_CERTIFICATE)
	{
		certificate_t *cert = (certificate_t*)cred;

		print_cert(cert);
		cert->destroy(cert);
	}
	if (type == CRED_PUBLIC_KEY)
	{
		public_key_t *key = (public_key_t*)cred;

		print_pubkey(key);
		key->destroy(key);
	}
	if (type == CRED_PRIVATE_KEY)
	{
		private_key_t *key = (private_key_t*)cred;

		print_key(key);
		key->destroy(key);
	}
	return 0;
}

/**
 * Register the command.
 */
static void __attribute__ ((constructor))reg()
{
	command_register((command_t)
		{ print, 'a', "print",
		"print a credential in a human readable form",
		{"[--in file] [--type rsa-priv|ecdsa-priv|pub|x509|crl]"},
		{
			{"help",	'h', 0, "show usage information"},
			{"in",		'i', 1, "input file, default: stdin"},
			{"type",	't', 1, "type of credential, default: x509"},
		}
	});
}
