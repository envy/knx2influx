#define _GNU_SOURCE

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "knx.h"

#define MULTICAST_PORT            3671 // [Default 3671]
#define MULTICAST_IP              "224.0.23.12" // [Default IPAddress(224, 0, 23, 12)]
#define INTERFACE_IP              "10.1.36.13"


///////////////////////////////////////////////////////////////////////////////

static int socket_fd;
static struct ip_mreq command = {};

void exithandler()
{
	int loop = 1;
	if (setsockopt(socket_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &command, sizeof(command)) < 0)
	{
		perror("setsockopt (IP_DROP_MEMBERSHIP): ");
	}
	close(socket_fd);
}

void process_packet(uint8_t *buf, size_t len)
{
	knx_ip_pkt_t *knx_pkt = (knx_ip_pkt_t *)buf;

	printf("ST: 0x%02x\n", ntohs(knx_pkt->service_type));

	if (knx_pkt->header_len != 0x06 && knx_pkt->protocol_version != 0x10 && knx_pkt->service_type != KNX_ST_ROUTING_INDICATION)
		return;

	cemi_msg_t *cemi_msg = (cemi_msg_t *)knx_pkt->pkt_data;

	printf("MT: 0x%02x\n", cemi_msg->message_code);

	if (cemi_msg->message_code != KNX_MT_L_DATA_IND)
		return;

	printf("ADDI: 0x%02x\n", cemi_msg->additional_info_len);

	cemi_service_t *cemi_data = &cemi_msg->data.service_information;

	if (cemi_msg->additional_info_len > 0)
		cemi_data = (cemi_service_t *)(((uint8_t *)cemi_data) + cemi_msg->additional_info_len);

	printf("C1: 0x%02x   C2: 0x%02x   DT: 0x%02x\n", cemi_data->control_1.byte, cemi_data->control_2.byte, cemi_data->control_2.bits.dest_addr_type);

	if (cemi_data->control_2.bits.dest_addr_type != 0x01)
		return;

	printf("HC: 0x%02x   EFF: 0x%02x\n", cemi_data->control_2.bits.hop_count, cemi_data->control_2.bits.extended_frame_format);

	printf("Source: 0x%02x%02x (%u.%u.%u)\n", cemi_data->source.bytes.high, cemi_data->source.bytes.low, cemi_data->source.pa.area, cemi_data->source.pa.line, cemi_data->source.pa.member);
	printf("Dest:   0x%02x%02x (%u/%u/%u)\n", cemi_data->destination.bytes.high, cemi_data->destination.bytes.low, cemi_data->destination.ga.area, cemi_data->destination.ga.line, cemi_data->destination.ga.member);

	knx_command_type_t ct = (knx_command_type_t)(((cemi_data->data[0] & 0xC0) >> 6) | ((cemi_data->pci.apci & 0x03) << 2));

	printf("CT: 0x%02x\n", ct);

	for (int i = 0; i < cemi_data->data_len; ++i)
	{
		printf(" 0x%02x", cemi_data->data[i]);
	}
	printf("\n==\n");

  // Call callbacks
	//for (int i = 0; i < registered_callback_assignments; ++i)
	{
		//printf("Testing: 0x%02x%02x\n", callback_assignments[i].address.bytes.high, callback_assignments[i].address.bytes.low);
		//if (cemi_data->destination.value == callback_assignments[i].address.value)
	    {
			printf("Found match\n");
			//if (callbacks[callback_assignments[i].callback_id].cond && !callbacks[callback_assignments[i].callback_id].cond())
			{
        		printf("But it's disabled\n");
				//continue;
			}
			uint8_t data[cemi_data->data_len];
			memcpy(data, cemi_data->data, cemi_data->data_len);
			data[0] = data[0] & 0x3F;
			message_t msg = {};
			msg.ct = ct;
			msg.received_on = cemi_data->destination;
			msg.data_len = cemi_data->data_len;
			msg.data = data;
			//callbacks[callback_assignments[i].callback_id].fkt(msg, callbacks[callback_assignments[i].callback_id].arg);
			//continue;
		}
	}
}

int main(int argc, char *argv)
{
	struct sockaddr_in sin = {};
	sin.sin_family = PF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(MULTICAST_PORT);
	if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket: ");
		exit(EXIT_FAILURE);
	}
	printf("Our socket fd is %d\n", socket_fd);

	int loop = 1;
	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &loop, sizeof(loop)) < 0)
	{
		perror("setsockopt (SO_REUSEADDR): ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	if (bind(socket_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		perror("bind: ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	loop = 1;
	if (setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0)
	{
		perror("setsockopt (IP_MULTICAST_LOOP): ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	command.imr_multiaddr.s_addr = inet_addr(MULTICAST_IP);
	command.imr_interface.s_addr = inet_addr(INTERFACE_IP);

	if (command.imr_multiaddr.s_addr == -1)
	{
		perror(MULTICAST_IP" is not a valid multicast address: ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	if (setsockopt(socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &command, sizeof(command)) < 0)
	{
		perror("setsockopt (IP_ADD_MEMBERSHIP): ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	atexit(exithandler);

	uint8_t buf[512];
	ssize_t rec = 0;

	while(1)
	{
		int sin_len = sizeof(sin);
		if ((rec = recvfrom(socket_fd, buf, 512, 0, (struct sockaddr *) &sin, &sin_len)) == -1)
		{
			perror("recfrom: ");
			break;
		}		
		printf("Got %d bytes: ", rec);
		for (ssize_t i = 0; i < rec; ++i)
			printf("%02x ", buf[i]);
		printf("\n");
		process_packet(buf, rec);
	}

	return 0;
}

