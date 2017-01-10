#include "rdma_cs.h"
#include <pthread.h>

enum client_status{
	OPEN,
	CLOSED
};

struct pnode {
	pthread_t id;
	unsigned short type;
	struct pnode *next;
} *tlist_head;

struct cnode {
	struct rdma_cm_id *id;
	uint32_t rkey;
	uint64_t remote_addr;
	size_t length;
	enum client_status status;
	struct cnode *next;
} *clist_head;


sem_t clist_sem;
sem_t tlist_sem;
short port;
FILE *log_p;

void binding_of_isaac(struct rdma_cm_id *, short);
void *hey_listen(void *);
void *secret_agent(void *);

int main(int argc, char **argv){
	// Create log directory
	struct stat st = {0};
	if (stat("server_logs", &st) == -1) {
	    mkdir("server_logs", 0700);
	}
	// Create log file
	char filename[100];
	strcpy(filename, SERVER_LOG_PATH);
	time_t rawtime;
	struct tm *timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strcat(filename, asctime(timeinfo));
	int i;
	for(i=0;i<strlen(filename);i++){
		if(*(filename+i) == ' ' || *(filename+i) == ':')
			*(filename+i) = '-';
	}
	filename[strlen(filename)-1] = 0;
	strcat(filename, ".log");
	log_p = fopen(filename , "a");
	fprintf(log_p, "Server started on: %s\n", asctime(timeinfo));
	// Get port from arguments
	if(argc == 2)
		port = atoi(argv[1]);
	else
		port = 0;
	// Create event channel
	struct rdma_event_channel *event_channel = rdma_create_event_channel();
	if(event_channel == NULL)
		stop_it("rdma_create_event_channel()", errno, log_p);
	// Create the ID
	struct rdma_cm_id *cm_id;
	if(rdma_create_id(event_channel, &cm_id, "qwerty", RDMA_PS_TCP))
		stop_it("rdma_create_id()", errno, log_p);
	// Bind to the port
	binding_of_isaac(cm_id, port);
	// Store required info that the listener needs
	// Store the pthread's id and purpose
	tlist_head = malloc(sizeof(struct pnode));
	clist_head = malloc(sizeof(struct cnode));
	tlist_head->type =0;
	// Initialize the client and thread list access semaphores
	sem_init(&clist_sem, 0, 1);
	sem_init(&tlist_sem, 0, 1);
	// Spawn listener thread
	if(pthread_create(&tlist_head->id, NULL, hey_listen, cm_id))
		stop_it("pthread_create()", errno, log_p);
	char opcode;
	struct cnode *clients;
	// Handle server side operations
	while(1){
		printf("---------------\n"
			"| RDMA server |\n"
			"---------------\n"
			"1) Shut down\n"
			"2) View connected clients\n"
			"> ");
		opcode = fgetc(stdin)%48;
		while(fgetc(stdin) != '\n')
			fgetc(stdin);
		if(opcode==1){
			printf("Shutting down server...\n");
			break;
		} else if (opcode==2){
			sem_wait(&clist_sem);
			for(clients=clist_head;clients != NULL; clients = clients->next){
				printf("--------Client id: %p\n"
					"--Client MR length: %llu\n"
					"--Client MR status: %s\n", clients->id, (unsigned long long)clients->length,
					clients->status == OPEN ? "open" : "closed");
			}
			sem_post(&clist_sem);
		}
	}
	fclose(log_p);
	return 0;
}

// Bind to the specified port. If zero, bind to a random open port.
void binding_of_isaac(struct rdma_cm_id *cm_id, short port){
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	if(rdma_bind_addr(cm_id, (struct sockaddr *)&sin))
		stop_it("rdma_bind_addr()", errno, log_p);
	fprintf(log_p, "RDMA device bound to port %u.\n", ntohs(rdma_get_src_port(cm_id)));
}

void *hey_listen(void *cmid){
	// Standard initializing
	struct rdma_cm_id *cm_id = cmid;
	struct rdma_event_channel *ec = cm_id->channel;
	struct pnode *tlist = tlist_head;
	struct cnode *clist = clist_head;
	while(1){
		// Listen for connection requests
		fprintf(log_p, "Listening for connection requests...\n");
		if(rdma_listen(cm_id, 1))
			stop_it("rdma_listen()", errno, log_p);
		// Make an ID specific to the client that connected
		clist->id = cm_event(ec, RDMA_CM_EVENT_CONNECT_REQUEST, log_p, &clist_sem);
		clist->length = SERVER_MR_SIZE;
		sem_post(&clist_sem);
		cm_event(ec, RDMA_CM_EVENT_ESTABLISHED, log_p, NULL);
		clist->status = CLOSED;
		// Store new thread's id and purpose
		sem_wait(&tlist_sem);
		tlist->next = malloc(sizeof(struct pnode));
		tlist = tlist->next;
		memset(tlist, 0, sizeof(struct pnode));
		tlist->type = 1;
		// Spawn an agent thread for the new conenction
		
		if(pthread_create(&tlist->id, NULL, secret_agent, clist)){
			stop_it("pthread_create()", errno, log_p);
		}
		sem_post(&tlist_sem);
		sem_wait(&clist_sem);
		clist->next = malloc(sizeof(struct cnode));
		clist = clist->next;
		memset(clist, 0, sizeof(struct cnode));
		sem_post(&clist_sem);
		// Remake the cm_id
		if(rdma_destroy_id(cm_id))
			stop_it("rdma_destroy_id()", errno, log_p);
		ec = rdma_create_event_channel();
		if(rdma_create_id(ec, &cm_id, "qwerty", RDMA_PS_TCP))
			stop_it("rdma_create_id()", errno, log_p);
		// Bind to the port
		binding_of_isaac(cm_id, port);
		// Rinse and repeat
	}
	return 0;
}

void *secret_agent(void *clist){
	struct cnode *clist_me = clist;
	struct rdma_cm_id *cm_id = clist_me->id;
	struct ibv_mr *mr = ibv_reg_mr(cm_id->qp->pd, malloc(SERVER_MR_SIZE),SERVER_MR_SIZE,
	 IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
	if(mr == NULL)
		stop_it("ibv_reg_mr()", errno, log_p);
	// Exchange addresses and rkeys with the client
	uint32_t rkey;
	uint64_t remote_addr;
	swap_info(cm_id, mr, &rkey, &remote_addr, NULL, log_p);
	// The real good
	uint32_t opcode;
	while(1){
		rdma_recv(cm_id, mr, log_p);
		opcode = get_completion(cm_id, RECV, 1, log_p);
		if(opcode == DISCONNECT){
			fprintf(log_p, "Client issued a disconnect.\n");
			break;
		} 
	}
	// Disconnect
	obliterate(NULL, cm_id, mr, cm_id->channel, log_p);
	return 0;
}