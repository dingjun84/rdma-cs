/**
 * @file client.c
 * @brief A RDMA client.
 *
 * This is the RDMA client that is to be paired with the server.c file.
 * @author Austin Pohlmann 
 */
#include "rdma_cs.h"

/**
 * @brief Structure used to pass information to the listening thread.
 */
struct listen_info {
	struct ibv_mr *mr; 		/**< This client's memory region */
	struct rdma_cm_id *id;	/**< This client's id */
};

/**
 * @brief Semaphore to synchronize with the server
 */
sem_t servercom;
/**
 * @brief The head of the list containing information on all open memory regions on the server
 */
struct client *clist_head=NULL;
/**
 * @brief The current number of open memory regions
 */
unsigned long clients = 0;

/*
// Speed testing
struct timeval te; // get current time
long long time_one, time_two; // caculate milliseconds

long long getTime(){
	gettimeofday(&te, NULL); 
	return te.tv_sec*1000LL + te.tv_usec/1000;
}
*/

void connect_four(struct rdma_cm_id *, struct rdma_event_channel *, char *, short int);
void *server_com(void *);
void add_client(struct client);
void remove_client(unsigned long);
struct client *get_client();
int contains(int);

int main(int argc, char **argv){
	// Get server address and port from arguments
	if(argc != 3){
		printf("Invalid arguements: %s <address> <port>\n", argv[0]);
		return -1;
	}
	char *ip = argv[1];
	short port = atoi(argv[2]);
	sem_init(&servercom, 0, 0);
	// Create the event channel
	struct rdma_event_channel *event_channel = rdma_create_event_channel();
	if(event_channel == NULL)
		stop_it("rdma_create_event_channel()", errno, stderr);
	// Create the ID
	struct rdma_cm_id *cm_id;
	if(rdma_create_id(event_channel, &cm_id, "qwerty", RDMA_PS_TCP))
		stop_it("rdma_create_id()", errno, stderr);
	// Connect to the server
	connect_four(cm_id, event_channel, ip, port);
	// Register memory region
	struct ibv_mr *mr = ibv_reg_mr(cm_id->qp->pd, malloc(REGION_LENGTH), REGION_LENGTH,
	 IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
	if(mr == NULL)
		stop_it("ibv_reg_mr()", errno, stderr);
	// Exchange addresses and rkeys with the server
	uint32_t rkey;
	uint64_t remote_addr;
	size_t server_mr_length;
	swap_info(cm_id, mr, &rkey, &remote_addr, &server_mr_length, stdout);
	// Create a file pointer for file output later
	FILE *output_file;
	char filename[50];
	// The real good
	void *buffer = malloc(MAX_INLINE_DATA);
	uint8_t opcode;
	unsigned long long length;
	unsigned long long offset;
	int i;
	unsigned char *byte;
	// Make the listener thread before the real good starts
	struct listen_info info;
	info.mr = mr;
	info.id = cm_id;
	pthread_t listen_thread;
	if(pthread_create(&listen_thread, NULL, server_com, &info)){
		stop_it("pthread_create()", errno, stderr);
	}
	struct client* remote_id;
	while(1){
		page1:
		printf("----------------------\n"
			"| RDMA client        |\n"
			"----------------------\n"
			"1) Disconnect        |\n"
			"2) Write inline      |\n"
			"3) Write             |\n"
			"4) Read              |\n"
			"5) Open Server MR    |\n"
			"6) Close server MR   |\n"
			"7) View open MRs     |\n"
			"8) Request another MR|\n");
		if(clients)
			printf("9) Page 2            |\n");
		printf("> ");
		scanf("%c",&opcode);
		opcode = opcode%48;
		fgetc(stdin);
		//opcode = fgetc(stdin)%48;
		//while(fgetc(stdin) != '\n')
		//	fgetc(stdin);
		if(opcode == DISCONNECT){
			// Send disconnect signal to server
			rdma_send_op(cm_id, opcode, stdout);
			get_completion(cm_id, SEND, 0, stdout);
			break;
		} else if(opcode == WRITE_INLINE){
			// RDMA write inline
			printf("Server memory region is %u bytes long. "
				"Choosing a relative point (0 - %u) to start writing to.\n> ",
				(unsigned int)server_mr_length, (unsigned int)server_mr_length-MAX_INLINE_DATA);
			scanf("%llu", &offset);fgetc(stdin);
			if(offset+MAX_INLINE_DATA> server_mr_length){
				printf("Invalid offset.\n");
				continue;
			}
			printf("Enter the data to be sent to the server. Max length is %u bytes.\n> ",
				MAX_INLINE_DATA);
			memset(buffer, 0, MAX_INLINE_DATA);
			if(fgets(buffer, MAX_INLINE_DATA, stdin) == NULL){
				printf("Unknow error occured.");
				rdma_send_op(cm_id, DISCONNECT, stdout);
				get_completion(cm_id, SEND, 0, stdout);
				break;
			}
			rdma_write_inline(cm_id, buffer, remote_addr+offset, rkey, stdout);
			get_completion(cm_id, SEND, 1, stdout);
		} else if(opcode == WRITE){
			// RDMA write
			printf("Server memory region is %u bytes long. "
				"Choosing a relative point (0 - %u) to start writing to.\n> ",
				(unsigned int)server_mr_length, (unsigned int)server_mr_length - 1);
			scanf("%llu", &offset);fgetc(stdin);
			if(offset >= server_mr_length){
				printf("Invalid offset.\n");
				continue;
			}
			printf("Enter the data to be sent to the server. Max length is %llu bytes.\n> ",
				server_mr_length - offset);
			if(fgets(mr->addr, MAX_INLINE_DATA, stdin) == NULL){
				printf("Unknow error occured.");
				rdma_send_op(cm_id, DISCONNECT, stdout);
				get_completion(cm_id, SEND, 0, stdout);
				break;
			}
			rdma_post_write(cm_id, "qwerty", mr->addr, strlen(mr->addr),
				mr, IBV_SEND_SIGNALED, remote_addr+offset, rkey);
			get_completion(cm_id, SEND, 1, stdout);
		} else if(opcode == OPEN_MR){
			rdma_send_op(cm_id, opcode, stdout);
			get_completion(cm_id, SEND, 0, stdout);
		} else if(opcode == CLOSE_MR){
			rdma_send_op(cm_id, opcode, stdout);
			get_completion(cm_id, SEND, 0, stdout);
		} else if(opcode == REQUEST_MR){
			//time_one = getTime();
			rdma_send_op(cm_id, opcode, stdout);
			get_completion(cm_id, SEND, 0, stdout);
			sem_wait(&servercom);
			//time_two = getTime();
			//printf("%lldms\n", time_two-time_one);
		} else if(opcode == REQUEST_PAGE){

		} else if(opcode == READ){
			// RDMA read
			printf("Would you like to print the data to console or write to a file? (p for print, w for write)\n> ");
			scanf("%c", &opcode);
			if(opcode == 'w'){
				printf("Enter a filename: \n> ");
				scanf("%s", filename);
				output_file = fopen(filename, "w");
			} else {
				output_file = stdout;
			}
			printf("Server memory region is %u bytes long. "
				"Choosing a relative point (0 - %u) to start reading from, followed by "
				"how many bytes you wish to read (0-%u).\n> ", (unsigned int)server_mr_length,
				(unsigned int)server_mr_length-1, REGION_LENGTH);
			scanf("%llu", &offset);fgetc(stdin);
			printf("> ");
			scanf("%llu", &length);fgetc(stdin);
			if(offset+length > server_mr_length || length > REGION_LENGTH){
				printf("Invalid offset and/or length.\n");
				continue;
			}
			if(rdma_post_read(cm_id, "qwerty", mr->addr, length, mr, IBV_SEND_SIGNALED,
				remote_addr + offset, rkey))
				stop_it("rdma_post_read()", errno, stderr);
			get_completion(cm_id, SEND, 1, stdout);
			// Print data in hex 1 byte at a time
			fprintf(output_file, "Data: ");
			byte = (unsigned char *)mr->addr;
			for(i=0;i<length;i++)
				fprintf(output_file, "%02x ", byte[i]);
			printf("\n");
		} else if(opcode == 9 && clients) {
			goto page2;
		} else {
			printf("Unknown operation, try again buddy.\n");
		}

	}
	goto disconnect;
	while(1){
		page2:
		printf("----------------------\n"
			"| RDMA client(page 2)|\n"
			"----------------------\n"
			"1) Write inline      |\n"
			"2) Write             |\n"
			"3) Read              |\n"
			"4) Go back           |\n"
			"> ");
		scanf("%c",&opcode);
		opcode = opcode%48;
		fgetc(stdin);
		if(opcode == 1){
			remote_id = get_client();
			// RDMA write inline
			printf("Server memory region is %u bytes long. "
				"Choosing a relative point (0 - %u) to start writing to.\n> ",
				(unsigned int)remote_id->length, (unsigned int)remote_id->length-MAX_INLINE_DATA);
			scanf("%llu", &offset);fgetc(stdin);
			if(offset+MAX_INLINE_DATA> remote_id->length){
				printf("Invalid offset.\n");
				continue;
			}
			printf("Enter the data to be sent to the server. Max length is %u bytes.\n> ",
				MAX_INLINE_DATA);
			memset(buffer, 0, MAX_INLINE_DATA);
			if(fgets(buffer, MAX_INLINE_DATA, stdin) == NULL){
				printf("Unknow error occured.");
				rdma_send_op(cm_id, DISCONNECT, stdout);
				get_completion(cm_id, SEND, 0, stdout);
				break;
			}
			rdma_write_inline(cm_id, buffer, (remote_id->remote_addr)+offset, remote_id->rkey, stdout);
			get_completion(cm_id, SEND, 1, stdout);
		} else if(opcode == 2){
			remote_id = get_client();
			// RDMA write
			printf("Server memory region is %u bytes long. "
				"Choosing a relative point (0 - %u) to start writing to.\n> ",
				(unsigned int)remote_id->length, (unsigned int)remote_id->length - 1);
			scanf("%llu", &offset);fgetc(stdin);
			if(offset >= remote_id->length){
				printf("Invalid offset.\n");
				continue;
			}
			printf("Enter the data to be sent to the server. Max length is %llu bytes.\n> ",
				remote_id->length - offset);
			if(fgets(mr->addr, MAX_INLINE_DATA, stdin) == NULL){
				printf("Unknow error occured.");
				rdma_send_op(cm_id, DISCONNECT, stdout);
				get_completion(cm_id, SEND, 0, stdout);
				break;
			}
			rdma_post_write(cm_id, "qwerty", mr->addr, strlen(mr->addr),
				mr, IBV_SEND_SIGNALED, (remote_id->remote_addr)+offset, remote_id->rkey);
			get_completion(cm_id, SEND, 1, stdout);
		} else if(opcode == 3){
			remote_id = get_client();
			// RDMA read
			printf("Would you like to print the data to console or write to a file? (p for print, w for write)\n> ");
			scanf("%c", &opcode);
			if(opcode == 'w'){
				printf("Enter a filename: \n> ");
				scanf("%s", filename);
				output_file = fopen(filename, "w");
			} else {
				output_file = stdout;
			}
			printf("Server memory region is %u bytes long. "
				"Choosing a relative point (0 - %u) to start reading from, followed by "
				"how many bytes you wish to read (0-%u).\n> ", (unsigned int)server_mr_length,
				(unsigned int)server_mr_length-1, REGION_LENGTH);
			scanf("%llu", &offset);fgetc(stdin);
			printf("> ");
			scanf("%llu", &length);fgetc(stdin);
			if(offset+length > server_mr_length || length > REGION_LENGTH){
				printf("Invalid offset and/or length.\n");
				continue;
			}
			if(rdma_post_read(cm_id, "qwerty", mr->addr, length, mr, IBV_SEND_SIGNALED,
				(remote_id->remote_addr)+offset, remote_id->rkey))
				stop_it("rdma_post_read()", errno, stderr);
			get_completion(cm_id, SEND, 1, stdout);
			// Print data in hex 1 byte at a time
			fprintf(output_file, "Data: ");
			byte = (unsigned char *)mr->addr;
			for(i=0;i<length;i++)
				fprintf(output_file, "%02x ", byte[i]);
			printf("\n");
		} else if(opcode == 4){
			goto page1;
		} else {
			printf("Unknown operation, try again buddy.\n");
		}
	}
	// Disconnect
	disconnect:
	obliterate(cm_id, NULL, mr, event_channel, stdout);
	return 0;
}

/**
 * @brief Connect to a server at the given ip and port.
 *
 * @return @c NULL
 * @param cm_id the cm_id associated with this client
 * @param ec the event channel to use
 * @param ip the ip to connect to
 * @param port the port to connect to
 */
void connect_four(struct rdma_cm_id *cm_id, struct rdma_event_channel *ec, char *ip, 
	short int port){
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	inet_aton(ip, &(sin.sin_addr));
	// Resolve the server's address
	if(rdma_resolve_addr(cm_id, NULL, (struct sockaddr *)&sin, 10000))
		stop_it("rdma_resolve_addr()", errno, stderr);
	// Wait for the address to resolve
	cm_event(ec, RDMA_CM_EVENT_ADDR_RESOLVED, stdout);
	// Create queue pair
	struct ibv_qp_init_attr *init_attr = malloc(sizeof(*init_attr));
	memset(init_attr, 0, sizeof(*init_attr));
	init_attr->qp_type = IBV_QPT_RC;
	init_attr->cap.max_send_wr  = MAX_SEND_WR;
	init_attr->cap.max_recv_wr  = MAX_RECV_WR;
	init_attr->cap.max_send_sge = MAX_SEND_SGE;
	init_attr->cap.max_recv_sge = MAX_RECV_SGE;
	init_attr->cap.max_inline_data = MAX_INLINE_DATA;
	if(rdma_create_qp(cm_id, NULL, init_attr))
		stop_it("rdma_create_qp()", errno, stderr);
	// Resolve the route to the server
	if(rdma_resolve_route(cm_id, 10000))
		stop_it("rdma_resolve_route()", errno, stderr);
	// Wait for the route to resolve
	cm_event(ec, RDMA_CM_EVENT_ROUTE_RESOLVED, stdout);
	// Send a connection request to the server
	struct rdma_conn_param *conn_params = malloc(sizeof(*conn_params));
	printf("Connecting...\n");
	memset(conn_params, 0, sizeof(*conn_params));
	conn_params->retry_count = 8;
	conn_params->rnr_retry_count = 8;
	conn_params->responder_resources = 10;
	conn_params->initiator_depth = 10;
	if(rdma_connect(cm_id, conn_params))
		stop_it("rdma_connect()", errno, stderr);
	// Wait for the server to accept the connection
	cm_event(ec, RDMA_CM_EVENT_ESTABLISHED, stdout);
}

/**
 * @brief Listen for messages from the server.
 * 
 * @return @c NULL
 * @param info a @c struct @c listen_info object with the needed information
 */
void *server_com(void *info){
	struct listen_info *linfo = info;
	struct rdma_cm_id *cm_id = linfo->id;
	struct ibv_mr *mr = linfo->mr;
	int opcode;
	struct client *client_data;
	while(1){
		// Wait for signal from the server
		rdma_recv(cm_id, mr, stdout);
		opcode = get_completion(cm_id, RECV, 0, stderr);
		if(opcode == DISCONNECT){
			// Send a disconnect signal to the server
			fprintf(stdout, "\nServer issued a disconnect request.\n");
			rdma_send_op(cm_id, opcode, stdout);
			get_completion(cm_id, SEND, 0, stdout);
			break;
		} else if (opcode == 0){
			return NULL;
		} else if (opcode == REQUEST_MR){
			client_data = mr->addr;
			if(contains(client_data->cid))
				continue;
			add_client(*client_data);
			printf("Id: %lu \n\tMR size: %lu\n\tRemote address: 0x%0llx\n\tRkey: 0x%0x\n", client_data->cid, (unsigned long)client_data->length,
				(long long)client_data->remote_addr, (unsigned int)client_data->rkey);
		} else if (opcode == SEMA_POST){
			sem_post(&servercom);
		}
	}
	obliterate(cm_id, NULL, mr, cm_id->channel, stdout);
	exit(0);
	return NULL;
}

/**
 * @brief Add information about an open memory region to the list
 *
 * @return @c NULL
 * @param node the node to add to the list
 */
void add_client(struct client node){
	struct client *current;
	clients++;
	if(clist_head == NULL){
		clist_head = malloc(sizeof(struct client));
		current = clist_head;
	} else {
		current = clist_head;
		while(current->next != NULL)
			current = current->next;
		current->next = malloc(sizeof(struct client));
		current = current->next;
	}
	memset(current, 0, sizeof(*current));
	current->cid = node.cid;
	current->rkey = node.rkey;
	current->remote_addr = node.remote_addr;
	current->length = node.length;
	current->next = NULL;
}
/**
 * @brief Remove information about an open memory region from the list
 *
 * @return @c NULL
 * @param id the client's id number associated with the memory region to be removed
 */
void remove_client(unsigned long id){
	struct client *ichi;
	struct client *ni;
	ichi = clist_head;
	if(clist_head->cid == id){
		if(clist_head->next == NULL){
			free(clist_head);
			clist_head = NULL;
		} else {
			clist_head = clist_head->next;
			free(ichi);
		}
	} else {
		ni = ichi->next;
		while(ni != NULL){
			if(ni->cid == id){
				ichi->next = ni->next;
				free(ni);
				break;
			} else {
				ichi = ni;
				ni = ni->next;
			}
		}
	}
	clients--;
}

/**
 *@brief the get cm_id of the desired open memory region
 *
 * @return the @c struct @c client node of the chosen memory region
 */ 
struct client *get_client(){
	struct client* node = clist_head;
	while(node){
		printf("ID: %5lu\tLength: %7luMB\n", node->cid, node->length);
		node = node->next;
	}
	int choice;
	printf("Enter a client's ID: ");
	scanf("%d", &choice);
	fgetc(stdin);
	node = clist_head;
	while(node){
		if(node->cid == choice)
			return node;
		node = node->next;
	}
	printf("Client not found.\n");
	return NULL;
}

/**
 * @brief Check if the list of memory regions contains a certain id number
 *
 * @return 1 for @c true, 0 for @c false
 * @param id the client id number of the memory region to search for
 */
int contains(int id){
	struct client* node = clist_head;
	while(node){
		if(node->cid == id)
			return 1;
		node = node->next;
	}
	return 0;
}