/**
 * @file message.c
 *
 * @brief Implementation of message_t.
 *
 */

/*
 * Copyright (C) 2005 Jan Hutter, Martin Willi
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

#include <stdlib.h>

#include "message.h"

#include <types.h>
#include <daemon.h>
#include <sa/ike_sa_id.h>
#include <encoding/generator.h>
#include <encoding/parser.h>
#include <utils/linked_list.h>
#include <utils/allocator.h>
#include <utils/logger_manager.h>
#include <encoding/payloads/encodings.h>
#include <encoding/payloads/payload.h>
#include <encoding/payloads/encryption_payload.h>


typedef struct supported_payload_entry_t supported_payload_entry_t;

/**
 * Supported payload entry used in message_rule_t.
 * 
 */
struct supported_payload_entry_t {
	/**
	 * Payload type.
	 */
	 payload_type_t payload_type;
	 
	 /**
	  * Minimal occurence of this payload.
	  */
	 size_t min_occurence;

	 /**
	  * Max occurence of this payload.
	  */	 
	 size_t max_occurence;
	 
	 /**
	  * TRUE if payload has to get encrypted
	  */
	 bool encrypted;
};

typedef struct message_rule_t message_rule_t;

/**
 * Message Rule used to find out which payloads 
 * are supported by each message type.
 * 
 */
struct message_rule_t {
	/**
	 * Type of message.
	 */
	exchange_type_t exchange_type;
	
	/**
	 * Is message a request or response.
	 */
	bool is_request;

	/**
	 * Message contains encrypted content.
	 */
	bool encrypted_content;
	
	 /**
	  * Number of supported payloads.
	  */
	 size_t supported_payloads_count;
	 
	/**
	 * Pointer to first supported payload entry.
	 */
	 supported_payload_entry_t *supported_payloads;
};

/**
 * Message rule for IKE_SA_INIT from initiator.
 */
static supported_payload_entry_t supported_ike_sa_init_i_payloads[] =
{
	{SECURITY_ASSOCIATION,1,1,FALSE},
	{KEY_EXCHANGE,1,1,FALSE},
	{NONCE,1,1,FALSE},
};

/**
 * Message rule for IKE_SA_INIT from responder.
 */
static supported_payload_entry_t supported_ike_sa_init_r_payloads[] =
{
	{SECURITY_ASSOCIATION,1,1,FALSE},
	{KEY_EXCHANGE,1,1,FALSE},
	{NONCE,1,1,FALSE},
};

/**
 * Message rule for IKE_AUTH from initiator.
 */
static supported_payload_entry_t supported_ike_auth_i_payloads[] =
{
	{ID_INITIATOR,1,1,TRUE},
	{CERTIFICATE,0,1,TRUE},
	{CERTIFICATE_REQUEST,0,1,TRUE},
	{ID_RESPONDER,0,1,TRUE},
	{AUTHENTICATION,1,1,TRUE},
	{SECURITY_ASSOCIATION,1,1,TRUE},
	{TRAFFIC_SELECTOR_INITIATOR,1,1,TRUE},
	{TRAFFIC_SELECTOR_RESPONDER,1,1,TRUE},
};

/**
 * Message rule for IKE_AUTH from responder.
 */
static supported_payload_entry_t supported_ike_auth_r_payloads[] =
{
	{CERTIFICATE,0,1,TRUE},
	{ID_RESPONDER,0,1,TRUE},
	{AUTHENTICATION,1,1,TRUE},
	{SECURITY_ASSOCIATION,1,1,TRUE},
	{TRAFFIC_SELECTOR_INITIATOR,1,1,TRUE},
	{TRAFFIC_SELECTOR_RESPONDER,1,1,TRUE},
};

/**
 * Message rules, defines allowed payloads.
 */
static message_rule_t message_rules[] = {
	{IKE_SA_INIT,TRUE,FALSE,(sizeof(supported_ike_sa_init_i_payloads)/sizeof(supported_payload_entry_t)),supported_ike_sa_init_i_payloads},
	{IKE_SA_INIT,FALSE,FALSE,(sizeof(supported_ike_sa_init_r_payloads)/sizeof(supported_payload_entry_t)),supported_ike_sa_init_r_payloads},
	{IKE_AUTH,TRUE,TRUE,(sizeof(supported_ike_auth_i_payloads)/sizeof(supported_payload_entry_t)),supported_ike_auth_i_payloads},
	{IKE_AUTH,FALSE,TRUE,(sizeof(supported_ike_auth_r_payloads)/sizeof(supported_payload_entry_t)),supported_ike_auth_r_payloads}
};

typedef struct payload_entry_t payload_entry_t;

/**
 * Entry for a payload in the internal used linked list.
 * 
 */
struct payload_entry_t {
	/**
	 * Type of payload.
	 */
	payload_type_t payload_type;
	/**
	 * Data struct holding the data of given payload.
	 */
	void *data_struct;
};


typedef struct private_message_t private_message_t;

/**
 * Private data of an message_t object.
 */
struct private_message_t {

	/**
	 * Public part of a message_t object.
	 */
	message_t public;

	/**
	 * Minor version of message.
	 */
	u_int8_t major_version;
	
	/**
	 * Major version of message.
	 */
	u_int8_t minor_version;
	
	/**
	 * First Payload in message.
	 */
	payload_type_t first_payload;

	/**
	 * Assigned exchange type.
	 */
	exchange_type_t exchange_type;


	/**
	 * TRUE if message is request.
	 * FALSE if message is reply.
	 */
	bool is_request;
	
	/**
	 * Message ID of this message.
	 */
	u_int32_t message_id;
	
	/**
	 * ID of assigned IKE_SA.
	 */
	ike_sa_id_t *ike_sa_id;
	
	/**
	 * Assigned UDP packet.
	 * 
	 * Stores incoming packet or last generated one.
	 */
	packet_t *packet;
	 
	/**
	 * Linked List where payload data are stored in.
	 */
	linked_list_t *payloads;
	
	 /**
	  * Assigned parser to parse Header and Body of this message.
	  */
	parser_t *parser;
	
	/**
	 * Assigned logger.
	 */
	logger_t *logger;
	
	/**
	 * Gets a list of supported payloads of this message type
	 * 
	 * @param this							calling object
	 * @param[out] message_rule				pointer is set to the message_rule of current message type
	 * 
	 * @return
	 * 										- SUCCESS
	 *										- NOT_FOUND if no message rule 
	 * 										for specific message type could be found
	 */
	status_t (*get_message_rule) (private_message_t *this, message_rule_t **message_rule);
	
	status_t (*get_supported_payload_entry) (private_message_t *this, message_rule_t *message_rule,payload_type_t payload_type, supported_payload_entry_t **payload_entry);
	
	/**
	 * Encrypts all payloads which has to get encrypted.
	 * 
	 * @param this							calling object
	 * @param crypter		crypter_t object
	 * @param signer		signer_t object
	 */
	status_t (*encrypt_payloads) (private_message_t *this,crypter_t *crypter, signer_t* signer);
	
	/**
	 * Decrypts all payloads which has to get decrypted.
	 * 
	 * @param this			calling object
	 * @param crypter		crypter_t object
	 * @param signer		signer_t object
	 */
	status_t (*decrypt_payloads) (private_message_t *this,crypter_t *crypter, signer_t* signer);	
};

/**
 * Implementation of private_message_t.get_supported_payloads.
 */

static  status_t get_message_rule (private_message_t *this, message_rule_t **message_rule)
{
	int i;
		
	for (i = 0; i < (sizeof(message_rules) / sizeof(message_rule_t)); i++)
	{
		if ((this->exchange_type == message_rules[i].exchange_type) &&
			(this->is_request == message_rules[i].is_request))
		{
			/* found rule for given exchange_type*/
			*message_rule = &(message_rules[i]);
			return SUCCESS;
		}
	}
	*message_rule = NULL;
	return NOT_FOUND;
}

static status_t get_supported_payload_entry (private_message_t *this, message_rule_t *message_rule,payload_type_t payload_type, supported_payload_entry_t **payload_entry)
{
	int i;
	
	for (i = 0; i < message_rule->supported_payloads_count;i++)
	{
		if (message_rule->supported_payloads[i].payload_type == payload_type)
		{
			*payload_entry = &(message_rule->supported_payloads[i]);
			return SUCCESS;
		}
	}
	
	*payload_entry = NULL;
	return NOT_FOUND;
}

/**
 * Implementation of message_t.set_ike_sa_id.
 */
static void set_ike_sa_id (private_message_t *this,ike_sa_id_t *ike_sa_id)
{
	this->ike_sa_id = ike_sa_id->clone(ike_sa_id);
}

/**
 * Implementation of message_t.get_ike_sa_id.
 */
static status_t get_ike_sa_id (private_message_t *this,ike_sa_id_t **ike_sa_id)
{
	if (this->ike_sa_id == NULL)
	{
		return FAILED;
	}
	*ike_sa_id = this->ike_sa_id->clone(this->ike_sa_id);
	return SUCCESS;
}

/**
 * Implementation of message_t.set_message_id.
 */
static void set_message_id (private_message_t *this,u_int32_t message_id)
{
	this->message_id = message_id;
}

/**
 * Implementation of message_t.get_message_id.
 */
static u_int32_t get_message_id (private_message_t *this)
{
	return this->message_id;
}

/**
 * Implementation of message_t.get_responder_spi.
 */
static u_int64_t get_responder_spi (private_message_t *this)
{
	return (this->ike_sa_id->get_responder_spi(this->ike_sa_id));
}

/**
 * Implementation of message_t.set_major_version.
 */
static void set_major_version (private_message_t *this,u_int8_t major_version)
{
	this->major_version = major_version;
}


/**
 * Implementation of message_t.set_major_version.
 */
static u_int8_t get_major_version (private_message_t *this)
{
	return this->major_version;
}

/**
 * Implementation of message_t.set_minor_version.
 */
static void set_minor_version (private_message_t *this,u_int8_t minor_version)
{
	this->minor_version = minor_version;
}

/**
 * Implementation of message_t.get_minor_version.
 */
static u_int8_t get_minor_version (private_message_t *this)
{
	return this->minor_version;
}

/**
 * Implementation of message_t.set_exchange_type.
 */
static void set_exchange_type (private_message_t *this,exchange_type_t exchange_type)
{
	this->exchange_type = exchange_type;
}

/**
 * Implementation of message_t.get_exchange_type.
 */
static exchange_type_t get_exchange_type (private_message_t *this)
{
	return this->exchange_type;
}

/**
 * Implementation of message_t.set_request.
 */
static void set_request (private_message_t *this,bool request)
{
	this->is_request = request;
}

/**
 * Implementation of message_t.get_request.
 */
static exchange_type_t get_request (private_message_t *this)
{
	return this->is_request;
}

/**
 * Implementation of message_t.add_payload.
 */
static void add_payload(private_message_t *this, payload_t *payload)
{
	payload_t *last_payload;
	if (this->payloads->get_count(this->payloads) > 0)
	{
		this->payloads->get_last(this->payloads,(void **) &last_payload);
		last_payload->set_next_type(last_payload, payload->get_type(payload));
	}
	else
	{
		this->first_payload = payload->get_type(payload);
	}
	payload->set_next_type(payload, NO_PAYLOAD);
	this->payloads->insert_last(this->payloads, (void*)payload);

	this->logger->log(this->logger, CONTROL|MORE, "added payload of type %s to message", 
						mapping_find(payload_type_m, payload->get_type(payload)));
}

/**
 * Implementation of message_t.set_source.
 */
static void set_source(private_message_t *this, host_t *host)
{
	if (this->packet->source != NULL)
	{
		this->packet->source->destroy(this->packet->source);	
	}
	this->packet->source = host;
}

/**
 * Implementation of message_t.set_destination.
 */
static void set_destination(private_message_t *this, host_t *host)
{
	if (this->packet->destination != NULL)
	{
		this->packet->destination->destroy(this->packet->destination);	
	}
	this->packet->destination = host;
}

/**
 * Implementation of message_t.get_source.
 */
static void get_source(private_message_t *this, host_t **host)
{
	*host = this->packet->source;
}

/**
 * Implementation of message_t.get_destination.
 */
static void get_destination(private_message_t *this, host_t **host)
{
	*host = this->packet->destination;
}

/**
 * Implementation of message_t.get_destination.
 */
static iterator_t *get_payload_iterator(private_message_t *this)
{
	return this->payloads->create_iterator(this->payloads, TRUE);
}


/**
 * Implementation of message_t.generate.
 */
static status_t generate(private_message_t *this, crypter_t *crypter, signer_t* signer, packet_t **packet)
{
	generator_t *generator;
	ike_header_t *ike_header;
	payload_t *payload, *next_payload;
	iterator_t *iterator;
	status_t status;
	
	
	this->logger->log(this->logger, CONTROL, "generating message, contains %d payloads", 
						this->payloads->get_count(this->payloads));
	
	if (this->exchange_type == EXCHANGE_TYPE_UNDEFINED)
	{
		this->logger->log(this->logger, ERROR, "exchange type is not defined");
		return INVALID_STATE;
	}
	
	if (this->packet->source == NULL ||
		this->packet->destination == NULL) 
	{
		this->logger->log(this->logger, ERROR, "source/destination not defined");
		return INVALID_STATE;
	}
	
	status = this->encrypt_payloads(this,crypter,signer);
	if (status != SUCCESS)
	{
		this->logger->log(this->logger, ERROR, "Could not encrypt payloads");
		return status;
	}

	/* build ike header */
	ike_header = ike_header_create();

	ike_header->set_exchange_type(ike_header, this->exchange_type);
	ike_header->set_message_id(ike_header, this->message_id);
	ike_header->set_response_flag(ike_header, !this->is_request);
	ike_header->set_initiator_flag(ike_header, this->ike_sa_id->is_initiator(this->ike_sa_id));
	ike_header->set_initiator_spi(ike_header, this->ike_sa_id->get_initiator_spi(this->ike_sa_id));
	ike_header->set_responder_spi(ike_header, this->ike_sa_id->get_responder_spi(this->ike_sa_id));


	generator = generator_create();
	
	payload = (payload_t*)ike_header;

	iterator = this->payloads->create_iterator(this->payloads, TRUE);
	
	/* generate every payload, except last one */
	while(iterator->has_next(iterator))
	{
		iterator->current(iterator, (void**)&next_payload);
		payload->set_next_type(payload, next_payload->get_type(next_payload));
		generator->generate_payload(generator, payload);

		payload = next_payload;
	}
	iterator->destroy(iterator);
	
	/* build last payload */
	payload->set_next_type(payload, NO_PAYLOAD);

	generator->generate_payload(generator, payload);
	ike_header->destroy(ike_header);
		
	/* build packet */
	if (this->packet->data.ptr != NULL)
	{
		allocator_free(this->packet->data.ptr);
	}	
	generator->write_to_chunk(generator, &(this->packet->data));
	generator->destroy(generator);
	
	/* append integrity checksum if necessary */
	if (payload->get_type(payload) == ENCRYPTED)
	{
		encryption_payload_t *encryption_payload = (encryption_payload_t*)payload;
		status = encryption_payload->build_signature(encryption_payload, this->packet->data);
		if (status != SUCCESS)
		{
			return status;
		}
	}
	
	/* clone packet for caller */
	*packet = this->packet->clone(this->packet);
	
	this->logger->log(this->logger, CONTROL, "message generated successfully");
	return SUCCESS;
}

/**
 * Implements message_t.parse_header.
 */
static status_t parse_header(private_message_t *this)
{
	ike_header_t *ike_header;
	status_t status;
	
	
	this->logger->log(this->logger, CONTROL, "parsing header of message");
	
	this->parser->reset_context(this->parser);
	status = this->parser->parse_payload(this->parser,HEADER,(payload_t **) &ike_header);
	if (status != SUCCESS)
	{
		this->logger->log(this->logger, ERROR, "Header could not be parsed");
		return status;
		
	}
	
	/* verify payload */
	status = ike_header->payload_interface.verify(&(ike_header->payload_interface));
	if (status != SUCCESS)
	{
		this->logger->log(this->logger, ERROR, "Header verification failed");
		ike_header->destroy(ike_header);
		return status;
	}	
	
	if (this->ike_sa_id != NULL)
	{
		this->ike_sa_id->destroy(this->ike_sa_id);
	}
	
	this->ike_sa_id = ike_sa_id_create(ike_header->get_initiator_spi(ike_header),
									   ike_header->get_responder_spi(ike_header),
									   ike_header->get_initiator_flag(ike_header));

	this->exchange_type = ike_header->get_exchange_type(ike_header);
	this->message_id = ike_header->get_message_id(ike_header);
	this->is_request = (!(ike_header->get_response_flag(ike_header)));
	this->major_version = ike_header->get_maj_version(ike_header);
	this->minor_version = ike_header->get_min_version(ike_header);
	this->first_payload = ike_header->payload_interface.get_next_type(&(ike_header->payload_interface));
	
	
	this->logger->log(this->logger, CONTROL, "parsing header successfully");
	
	ike_header->destroy(ike_header);	
	return SUCCESS;	
}

/**
 * Implements message_t.parse_body.
 */
static status_t parse_body(private_message_t *this, crypter_t *crypter, signer_t *signer)
{
	status_t status = SUCCESS;
	payload_type_t current_payload_type = this->first_payload;
		
	this->logger->log(this->logger, CONTROL, "parsing body of message, first payload %s",
						mapping_find(payload_type_m, current_payload_type));

	while ((current_payload_type != NO_PAYLOAD))
	{
		payload_t *current_payload;
		
		this->logger->log(this->logger, CONTROL|MORE, "start parsing payload of type %s", 
							mapping_find(payload_type_m, current_payload_type));
		
		status = this->parser->parse_payload(this->parser,current_payload_type,(payload_t **) &current_payload);
		if (status != SUCCESS)
		{
			this->logger->log(this->logger, ERROR, "payload type %s could not be parsed",mapping_find(payload_type_m,current_payload_type));
			return status;
		}
		
		status = current_payload->verify(current_payload);
		if (status != SUCCESS)
		{
			this->logger->log(this->logger, ERROR, "payload type %s could not be verified",mapping_find(payload_type_m,current_payload_type));
			current_payload->destroy(current_payload);
			status = VERIFY_ERROR;
			return status;
		}
		
		/* get next payload type */
		current_payload_type = current_payload->get_next_type(current_payload);
		
		this->payloads->insert_last(this->payloads,current_payload);
	}
	
	status = this->decrypt_payloads(this,crypter,signer);
	if (status != SUCCESS)
	{
		this->logger->log(this->logger, ERROR, "Could not decrypt payloads");
		return status;
	}
	
	return SUCCESS;

}

/**
 * implements message_t.verify
 */
static status_t verify(private_message_t *this)
{
	int i;
	status_t status;
	iterator_t *iterator;
	message_rule_t *message_rule;
	
	this->logger->log(this->logger, CONTROL|MORE, "verifying message");
	
	status = this->get_message_rule(this, &message_rule);
	if (status != SUCCESS)
	{
		this->logger->log(this->logger, ERROR, "Message rule could not be retrieved");
		return status;
	}
	
	iterator = this->payloads->create_iterator(this->payloads,TRUE);
	/* check for payloads with wrong count*/
	for (i = 0; i < message_rule->supported_payloads_count;i++)
	{
		size_t min_occurence = message_rule->supported_payloads[i].min_occurence;
		size_t max_occurence = message_rule->supported_payloads[i].max_occurence;
		payload_type_t payload_type = message_rule->supported_payloads[i].payload_type;
		size_t found_payloads = 0;
	
		iterator->reset(iterator);
			
		while(iterator->has_next(iterator))
		{
			payload_t *current_payload;
			iterator->current(iterator,(void **)&current_payload);
			
			if (current_payload->get_type(current_payload) == payload_type)
			{
				found_payloads++;
				if (found_payloads > max_occurence)
				{
					this->logger->log(this->logger, ERROR, "Payload of type %s more than %d times (%d) occured in current message",
									  mapping_find(payload_type_m,current_payload->get_type(current_payload)),max_occurence,found_payloads);
					iterator->destroy(iterator);
					return NOT_SUPPORTED;					
				}
			}
		}
		if (found_payloads < min_occurence)
		{
			this->logger->log(this->logger, ERROR, "Payload of type %s not occured %d times",
							  mapping_find(payload_type_m,payload_type),min_occurence);
			iterator->destroy(iterator);
			return NOT_SUPPORTED;
		}
	}
	iterator->destroy(iterator);
	
	return SUCCESS;
}


static status_t decrypt_payloads (private_message_t *this,crypter_t *crypter, signer_t* signer)
{
	bool current_payload_was_encrypted = FALSE;
	status_t status;
	message_rule_t *message_rule;
	iterator_t *iterator;
	int payload_number = 1;
	
	status = this->get_message_rule(this, &message_rule);
	if (status != SUCCESS)
	{
		this->logger->log(this->logger, ERROR, "No message rule for current message type");
		return status;
	}

	iterator = this->payloads->create_iterator(this->payloads,TRUE);

	while(iterator->has_next(iterator))
	{
		payload_t *current_payload;
		supported_payload_entry_t *payload_entry;

		/* get current payload */		
		iterator->current(iterator,(void **)&current_payload);
		
		this->logger->log(this->logger, CONTROL | MOST, "Process payload of type %s.",mapping_find(payload_type_m,current_payload->get_type(current_payload)));
		
		if (current_payload->get_type(current_payload) == ENCRYPTED)
		{
			encryption_payload_t *encryption_payload;
			iterator_t *encrypted_payload_iterator;
			payload_t *current_encrypted_payload;
	
			if (!message_rule->encrypted_content)
			{
				this->logger->log(this->logger, ERROR | MORE, "Encrypted payload not allowed for this message type");
				iterator->destroy(iterator);
				/* encrypted payload is not last one */
				return FAILED;
			}
			
			if (payload_number != this->payloads->get_count(this->payloads))
			{
				this->logger->log(this->logger, ERROR | MORE, "Encrypted payload is not last one");
				iterator->destroy(iterator);
				/* encrypted payload is not last one */
				return FAILED;
			}
			
			this->payloads->remove_last(this->payloads,(void **)&encryption_payload);
			
			/* encrypt payload */			
			encryption_payload->set_transforms(encryption_payload, crypter, signer);
			status = encryption_payload->verify_signature(encryption_payload, this->packet->data);
			if (status != SUCCESS)
			{
				this->logger->log(this->logger, ERROR, "encryption payload signature invalid");
				iterator->destroy(iterator);
				return status;
			}
			status = encryption_payload->decrypt(encryption_payload);
			if (status != SUCCESS)
			{
				this->logger->log(this->logger, ERROR, "parsing decrypted encryption payload failed");
				iterator->destroy(iterator);
				return status;
			}
			
			current_payload_was_encrypted = TRUE;
			
			encrypted_payload_iterator = encryption_payload->create_payload_iterator(encryption_payload, TRUE);
			
			if (!encrypted_payload_iterator->has_next(encrypted_payload_iterator))
			{
				iterator->remove(iterator);
				encrypted_payload_iterator->destroy(encrypted_payload_iterator);
				encryption_payload->destroy(encryption_payload);
				break;
			}
			
			/* encryption_payload is replaced with first encrypted payload*/
			encrypted_payload_iterator->current(encrypted_payload_iterator,(void **)&current_encrypted_payload);
			this->logger->log(this->logger, CONTROL | MORE, "Replace encrypted payload with payload of type %s.",mapping_find(payload_type_m,current_encrypted_payload->get_type(current_encrypted_payload)));
			iterator->replace(iterator,NULL,(void *) current_encrypted_payload);
			
			/* all encrypted payloads are added to the payload list */
			while (encrypted_payload_iterator->has_next(encrypted_payload_iterator))
			{
				this->logger->log(this->logger, CONTROL | MORE, "Insert unencrypted payload of type %s at end of list.",mapping_find(payload_type_m,current_encrypted_payload->get_type(current_encrypted_payload)));
				encrypted_payload_iterator->current(encrypted_payload_iterator,(void **)&current_encrypted_payload);				
				this->payloads->insert_last(this->payloads,current_encrypted_payload);
			}
			
			encrypted_payload_iterator->destroy(encrypted_payload_iterator);
			encryption_payload->destroy(encryption_payload);											
		}

		status = this->get_supported_payload_entry(this,message_rule,current_payload->get_type(current_payload),&payload_entry);
		
		if (status != SUCCESS)
		{
			/* payload type not supported */
			this->logger->log(this->logger, ERROR | MORE, "Payload type %s not allowed",mapping_find(payload_type_m,current_payload->get_type(current_payload)));
			iterator->destroy(iterator);
			return status;
		}
		
		if (payload_entry->encrypted != current_payload_was_encrypted)
		{
			/* payload type not supported */
			this->logger->log(this->logger, ERROR | MORE, "Payload type %s should be %s!", 
								mapping_find(payload_type_m,current_payload->get_type(current_payload)),
								(payload_entry->encrypted) ? "encrypted": "not encrypted");
			iterator->destroy(iterator);
			return status;
		}
		payload_number++;
	}
	iterator->destroy(iterator);
	
	return this->public.verify(&(this->public));
	
}


static status_t encrypt_payloads (private_message_t *this,crypter_t *crypter, signer_t* signer)
{
	status_t status;
	message_rule_t *message_rule;
	encryption_payload_t *encryption_payload = NULL;
	linked_list_t *all_payloads;
	
	status = this->get_message_rule(this, &message_rule);
	if (status != SUCCESS)
	{
		return status;
	}
	
	if (!message_rule->encrypted_content)
	{
		/* message contains no content to encrypt */
		return SUCCESS;
	}
	
	all_payloads = linked_list_create();
	
	/* first copy all payloads in a temporary list */
	while (this->payloads->get_count(this->payloads) > 0)
	{
		void *current_payload;
		this->payloads->remove_first(this->payloads,&current_payload);
		all_payloads->insert_last(all_payloads,current_payload);
	}
	
	while (all_payloads->get_count(all_payloads) > 0)
	{
		payload_t *current_payload;
		bool to_encrypt = FALSE;
		supported_payload_entry_t *supported_payload_entry;
		
		all_payloads->remove_first(all_payloads,(void **)&current_payload);
		
		status = this->get_supported_payload_entry(this,message_rule,current_payload->get_type(current_payload),&supported_payload_entry);
		/* for payload types which are not found in supported payload list, it is presumed 
		 * that they don't have to be encrypted */
		if ((status == SUCCESS) && (supported_payload_entry->encrypted))
		{
			to_encrypt = TRUE;
			break;
		}
		
		if (to_encrypt)
		{
			if (encryption_payload == NULL)
			{
				encryption_payload = encryption_payload_create();
			}
			encryption_payload->add_payload(encryption_payload,current_payload);
		}
		else
		{
			this->payloads->insert_last(this->payloads,current_payload);
		}
	}

	status = SUCCESS;
	if (encryption_payload != NULL)
	{
		encryption_payload->set_transforms(encryption_payload,crypter,signer);
		status = encryption_payload->encrypt(encryption_payload);
		this->public.add_payload(&(this->public), (payload_t*)encryption_payload);
	}
	
	all_payloads->destroy(all_payloads);
	
	return status;
}

/**
 * Implements message_t's destroy function.
 * See #message_s.destroy.
 */
static void destroy (private_message_t *this)
{
	iterator_t *iterator;
	
	this->packet->destroy(this->packet);

	if (this->ike_sa_id != NULL)
	{
		this->ike_sa_id->destroy(this->ike_sa_id);
	}
	
	iterator = this->payloads->create_iterator(this->payloads, TRUE);
	while (iterator->has_next(iterator))
	{
		payload_t *payload;
		iterator->current(iterator, (void**)&payload);	
		this->logger->log(this->logger, CONTROL|MOST, "Destroying payload of type %s", 
							mapping_find(payload_type_m, payload->get_type(payload)));
		payload->destroy(payload);
	}
	iterator->destroy(iterator);
	this->payloads->destroy(this->payloads);
	this->parser->destroy(this->parser);
	charon->logger_manager->destroy_logger(charon->logger_manager, this->logger);
	
	allocator_free(this);
}

/*
 * Described in Header-File
 */
message_t *message_create_from_packet(packet_t *packet)
{
	private_message_t *this = allocator_alloc_thing(private_message_t);

	/* public functions */
	this->public.set_major_version = (void(*)(message_t*, u_int8_t))set_major_version;
	this->public.get_major_version = (u_int8_t(*)(message_t*))get_major_version;
	this->public.set_minor_version = (void(*)(message_t*, u_int8_t))set_minor_version;
	this->public.get_minor_version = (u_int8_t(*)(message_t*))get_minor_version;
	this->public.set_message_id = (void(*)(message_t*, u_int32_t))set_message_id;
	this->public.get_message_id = (u_int32_t(*)(message_t*))get_message_id;
	this->public.get_responder_spi = (u_int64_t(*)(message_t*))get_responder_spi;	
	this->public.set_ike_sa_id = (void(*)(message_t*, ike_sa_id_t *))set_ike_sa_id;
	this->public.get_ike_sa_id = (status_t(*)(message_t*, ike_sa_id_t **))get_ike_sa_id;
	this->public.set_exchange_type = (void(*)(message_t*, exchange_type_t))set_exchange_type;
	this->public.get_exchange_type = (exchange_type_t(*)(message_t*))get_exchange_type;
	this->public.set_request = (void(*)(message_t*, bool))set_request;
	this->public.get_request = (bool(*)(message_t*))get_request;
	this->public.add_payload = (void(*)(message_t*,payload_t*))add_payload;
	this->public.generate = (status_t (*) (message_t *,crypter_t*,signer_t*,packet_t**)) generate;
	this->public.set_source = (void (*) (message_t*,host_t*)) set_source;
	this->public.get_source = (void (*) (message_t*,host_t**)) get_source;
	this->public.set_destination = (void (*) (message_t*,host_t*)) set_destination;
	this->public.get_destination = (void (*) (message_t*,host_t**)) get_destination;
	this->public.get_payload_iterator = (iterator_t * (*) (message_t *)) get_payload_iterator;
	this->public.parse_header = (status_t (*) (message_t *)) parse_header;
	this->public.parse_body = (status_t (*) (message_t *,crypter_t*,signer_t*)) parse_body;
	this->public.verify =  (status_t (*) (message_t*)) verify;
	this->public.destroy = (void(*)(message_t*))destroy;
		
	/* private values */
	this->exchange_type = EXCHANGE_TYPE_UNDEFINED;
 	this->is_request = TRUE;
 	this->ike_sa_id = NULL;
 	this->first_payload = NO_PAYLOAD;
 	this->message_id = 0;

	/* private functions */
	this->get_message_rule = get_message_rule;
	this->get_supported_payload_entry = get_supported_payload_entry;
	this->encrypt_payloads = encrypt_payloads;
	this->decrypt_payloads = decrypt_payloads;

	/* private values */
	if (packet == NULL)
	{
		packet = packet_create();	
	}
	this->packet = packet;
	this->payloads = linked_list_create();
	
	/* parser is created from data of packet */
 	this->parser = parser_create(this->packet->data);
		
	this->logger = charon->logger_manager->create_logger(charon->logger_manager, MESSAGE, NULL);

	return (&this->public);
}

/*
 * Described in Header-File
 */
message_t *message_create()
{
	return message_create_from_packet(NULL);
}
