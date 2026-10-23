/* Beken bk_avdk_smp release/v3.1.1.10, commit
 * 398cab28b938e69ba2071a597daf00845dd080b6.
 * DHCP DNS boundary shared by AP/CP before the project patches.
 * Copyright Beken. Apache-2.0. Socket I/O is supplied by the host harness.
 */
static int send_response(int sock, struct sockaddr *addr, char *msg, int len)
{
	return 0;
}

#define ERROR_REFUSED 5
static int process_dns_message(char *msg, int len, struct sockaddr_in *fromaddr)
{
	struct dns_header *hdr;
	char *endp = msg + len;
	int nq;

	if (len < sizeof(struct dns_header)) {
		dhcp_e("DNS request is not complete, hence ignoring it\r\n");
		return -1;
	}

	hdr = (struct dns_header *)msg;

	dhcp_d("DNS transaction id: 0x%x\r\n", htons(hdr->id));

	if (hdr->flags.fields.qr) {
		dhcp_e("ignoring this dns message (not a query)\r\n");
		return -1;
	}

	nq = ntohs(hdr->num_questions);
	dhcp_d("we were asked %d questions\r\n", nq);

	if (nq <= 0) {
		dhcp_e("ignoring this dns msg (not a query or 0 questions)\r\n");
		return -1;
	}

	/* make the header represent a response */
	hdr->flags.fields.qr = 1;
	hdr->flags.fields.opcode = 0;
	/* Errors are never authoritative (unless they are
	   NXDOMAINS, which this is not) */
	hdr->flags.fields.aa = 0;
	hdr->flags.fields.tc = 0;
	hdr->flags.fields.rd = 1;
	hdr->flags.fields.ra = 0;
	hdr->flags.fields.rcode = ERROR_REFUSED;
	hdr->flags.num = htons(hdr->flags.num);
	/* number of entries in questions section */
	hdr->num_questions  = htons(0x01);
	hdr->answer_rrs = 0; /* number of resource records in answer section */
	hdr->authority_rrs = 0;
	hdr->additional_rrs = 0;
	SEND_RESPONSE(dhcps.dnssock, (struct sockaddr *)fromaddr,
		      msg, endp - msg);

	return -1;
}

static int process_dhcp_message(char *msg, int len)
{
}
