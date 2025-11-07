#include "proxy_parse.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<netdb.h>//For ipv4/ipv6
#include<arpa/inet.h>//To assign address or port no
#include<unistd.h>
#include<fcntl.h>
#include<time.h>
#include<sys/wait.h>
#include<errno.h>
#include<pthread.h>
#include<semaphore.h>

#define MAX_CLIENT 10
#define MAX_BYTES 4096
#define MAX_ELEMENT_SIZE (10*(1<<10))   // 10 KB per element (example bound)
#define MAX_SIZE        (200*(1<<20))   // 200 MB cache total (example bound)

//======================== Cache Element ========================
typedef struct cache_element cache_element;
struct cache_element{//This made LRU cache with a element
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
    cache_element* next;
};

cache_element* find(char* url);

int add_cache_element(char* data,int size,char* url);
void remove_cache_element();

//Our server will run on this port no
int port_number=8080;
int proxy_socketId;

//For multi threaded server Number of the client== number of the threads
pthread_t tid[MAX_CLIENT];//Stores thread ids of the different clients(due to it is a multi threaded client)

//Semaphore and mutex lock using 
sem_t semaphore;//Semaphore have multiple values to lock 
pthread_mutex_t lock;//Lock have only two values to lock

//TO define cache head
cache_element* head;
int cache_size;
//Now all the things of the cahche are finished


//========================connectRemoteServer======================
int connectRemoteServer(char* host_addr, int port_num) {

    // Creating a socket for remote server connection (IPv4 + TCP)
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket < 0) {
        printf("Error in creating your socket\n");
        return -1;   // Return -1 if socket creation fails
    }

    // Getting host information by its domain name (e.g., www.google.com)
    struct hostent* host = gethostbyname(host_addr);
    if (host == NULL) {
        fprintf(stderr, "No such host exist\n");
        close(remoteSocket);
        return -1;   // Return -1 if host not found
    }

    // Define and initialize server address structure
    struct sockaddr_in server_addr;
    bzero((char*)&server_addr, sizeof(server_addr));   // Clear any garbage values

    server_addr.sin_family = AF_INET;                  // Using IPv4 family
    server_addr.sin_port = htons(port_num);            // Convert port number to network byte order

    // Copy host address into server_addr structure
    bcopy((char*)host->h_addr, 
          (char*)&server_addr.sin_addr.s_addr, 
          host->h_length);

    // Establish connection between proxy and remote server
    if (connect(remoteSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error in connecting\n");
        close(remoteSocket);
        return -1;   // Return -1 if connection fails
    }

    // Return the connected socket descriptor
    return remoteSocket;
}


///////==========handle_request=================================
int handle_request(int clientSocketId, ParsedRequest *request, char* tempReq){ 
    // This handle_request() function will return element from the cache 
    // If the element is not in the cache, then it will go to the server, 
    // fetch the element, insert it into the cache, and then return it to the client.

    // Create a buffer to store the complete HTTP request to send to the remote server
    char *buf = (char*) malloc(sizeof(char) * MAX_BYTES);
    if(!buf){ return -1; }
    strcpy(buf, "GET ");                          // Add GET method and a space
    strcat(buf, request->path);                   // Add path of requested file or resource
    strcat(buf, " ");                             // Add space before HTTP version
    strcat(buf, request->version);                // Add HTTP version (e.g., HTTP/1.1)
    strcat(buf, "\r\n");                          // End the request line
    size_t len = strlen(buf);                     // Get the total length of the current request line

    // Set header field Connection: close
    if (ParsedHeader_set(request, "Connection", "close") < 0) {
        printf("Set header key is not working\n");
    }

    // If no Host field is present, set it manually
    if (ParsedHeader_get(request, "Host") == NULL) {
        if (ParsedHeader_set(request, "Host", request->host) < 0) {
            printf("Set Host header key is not working\n");
        }
    }

    // Append remaining headers to the request buffer
    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
        printf("Unparse failed\n");
    }

    // Ensure final CRLF after headers
    size_t cur = strlen(buf);
    if (cur+2 < MAX_BYTES) { strcat(buf, "\r\n"); }

    // Default port number for HTTP is 80, but if another is specified, use that
    int server_port = 80;
    if (request->port != NULL) {
        server_port = atoi(request->port);
    }

    // Connect the proxy to the remote (origin) server
    int remoteSocketId = connectRemoteServer(request->host, server_port);
    if (remoteSocketId < 0) {
        free(buf);
        return -1;  // Exit if connection to remote server fails
    }

    // Send the constructed HTTP request to the remote server
    int bytes_sent = send(remoteSocketId, buf, (int)strlen(buf), 0);
    (void)bytes_sent; // not used later
    bzero(buf, MAX_BYTES);  // Clear buffer to receive the server response

    // Receive data from remote server
    int bytes_recv = recv(remoteSocketId, buf, MAX_BYTES - 1, 0);

    // Create temporary buffer to store entire response from remote server
    char *temp_buffer = (char*) malloc(sizeof(char) * MAX_BYTES);
    if(!temp_buffer){
        free(buf);
        close(remoteSocketId);
        return -1;
    }
    int temp_buffer_size = MAX_BYTES;            
    int temp_buffer_index = 0;

    // Loop until all data is received from the server
    while (bytes_recv > 0) {

        // Send the received data to the client (forward response)
        int sent_this_chunk = 0;
        while (sent_this_chunk < bytes_recv) {
            int s = send(clientSocketId, buf + sent_this_chunk, bytes_recv - sent_this_chunk, 0);
            if (s <= 0) { 
                perror("Error in sending data to the client\n"); 
                break; 
            }
            sent_this_chunk += s;
        }

        // Copy received data into temporary buffer for caching
        if (temp_buffer_index + bytes_recv + 1 > temp_buffer_size) {
            int new_size = temp_buffer_size + MAX_BYTES;  // Increase buffer size dynamically
            char* tmp = (char*)realloc(temp_buffer, new_size);
            if(!tmp){
                // OOM while growing cache copy: stop caching but continue forwarding
                break;
            }
            temp_buffer      = tmp;
            temp_buffer_size = new_size;
        }

        for (int i = 0; i < bytes_recv; i++) {
            temp_buffer[temp_buffer_index++] = buf[i];
        }

        bzero(buf, MAX_BYTES);                               // Clear buffer for next recv
        bytes_recv = recv(remoteSocketId, buf, MAX_BYTES - 1, 0);  // Receive next chunk
    }

    // Null-terminate the received response (if we copied any)
    if (temp_buffer_index < temp_buffer_size) {
        temp_buffer[temp_buffer_index] = '\0';
    }

    free(buf);

    // Add this fetched data into the cache for future requests
    if (temp_buffer_index > 0) {
        add_cache_element(temp_buffer, (int)strlen(temp_buffer), tempReq);
    }
    free(temp_buffer);

    // Close connection to remote server
    close(remoteSocketId);

    // Return success
    return 0;
}

//============check HTTP version=================
int checkHTTPversion(char *msg){
      int version= -1;
      if(strncmp(msg,"HTTP/1.1",8)==0){
        version=1;
      }
      else if(strncmp(msg,"HTTP/1.0",8)==0){
        version=1;
      }
      else{
        version=-1;
      }
      return version;
}

//===============Error message sending code================
int sendErrorMessage(int socket,int status_code){
    char str[1024];
    char currentTime[50];
    time_t now=time(0);

    struct tm data=*gmtime(&now);
    strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);
   switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}


//===========thread function========================
void* thread_fn(void* socketNew){
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("Semaphore value is: %d\n", p);

    int *t = (int *)socketNew;
    int socket = *t;

    int bytes_send_client, len; //Bytes send by the client
    char *buffer = (char*)calloc(MAX_BYTES, sizeof(char)); //Buffer where request came
    if(!buffer){ sem_post(&semaphore); return NULL; }
    bzero(buffer, MAX_BYTES); //Garbage value remove from the buffer

    // first read
    bytes_send_client = recv(socket, buffer, MAX_BYTES - 1, 0); //Received bytes
    if (bytes_send_client > 0) buffer[bytes_send_client] = '\0';

    while (bytes_send_client > 0) { //Request taken by the client(Here request came to our proxy)
        len = (int)strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") == NULL) {
            // read more while keeping room for terminator
            int n = recv(socket, buffer + len, MAX_BYTES - 1 - len, 0);
            if (n <= 0) { bytes_send_client = n; break; }
            bytes_send_client = n;
            len += n;
            buffer[len] = '\0';
        } else {
            break;
        }
    }

    //Now we can search this request into cache(so i will make copy of this request and then insert into cache)
    char* tempReq = (char*)malloc(strlen(buffer) + 1);
    if (tempReq) strcpy(tempReq, buffer);

    struct cache_element* temp = find(tempReq);

    if (temp != NULL) {
        int size = (int)temp->len; // bytes length (already in bytes)
        int pos = 0;
        char response[MAX_BYTES];

        while (pos < size) {
            int chunk = size - pos;
            if (chunk > MAX_BYTES) chunk = MAX_BYTES;
            // copy the correct chunk from cached data
            memcpy(response, temp->data + pos, chunk);
            pos += chunk;

            // send exactly the chunk size
            int sent_total = 0;
            while (sent_total < chunk) {
                int s = send(socket, response + sent_total, chunk - sent_total, 0);
                if (s <= 0) { pos = size; break; }
                sent_total += s;
            }
        }
        printf("Data retrived from the cache\n");
        // do not printf response as %s because it might be binary and not NUL-terminated
    }
    else if (bytes_send_client > 0) {
        len = (int)strlen(buffer);
        ParsedRequest *request = ParsedRequest_create();
        if (ParsedRequest_parse(request, buffer, len) < 0) {
            printf("Parsing failed");
        } else {
            bzero(buffer, MAX_BYTES);
            if (!strcmp(request->method, "GET")) {
                if (request->host && request->path && checkHTTPversion(request->version) == 1) {
                    bytes_send_client = handle_request(socket, request, tempReq);
                    if (bytes_send_client == -1) {
                        sendErrorMessage(socket, 500);
                    } else {
                        // success path: do nothing (do not send 500)
                    }
                } else {
                    printf("This code doesn't support my method apart from the GET\n");
                }
            }
            ParsedRequest_destroy(request);
        }
    }
    else if (bytes_send_client == 0) {
        printf("Client is disconnected");
    }

    shutdown(socket, SHUT_RDWR);
    close(socket);

    free(buffer);
    sem_post(&semaphore);
    sem_getvalue(&semaphore, &p);
    printf("Semaphore post value is %d\n", p);
    free(tempReq);

    return NULL;
}



//===================Main Function============================
int main(int argc, char* argv[]) {
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    sem_init(&semaphore, 0, MAX_CLIENT); //semaphore initializasation(minimum 0 and maximum MAX_CLIENT)
    pthread_mutex_init(&lock, NULL); //Lock initialize as NULL

    if (argc == 2) {  // fixed: should check argc, not argv
        port_number = atoi(argv[1]);
    } else {
        printf("Too few arguments\n");
        exit(1); //Exit from whole program
    }

    printf("Starting proxy server at port: %d\n", port_number);
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socketId < 0) {
        perror("Failed to create socket\n");
        exit(1);
    }

    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
        perror("setsockopt option is failed\n");
    }

    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number); //htons used here to understand the port to the network
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) { //This is for bind purpose
        perror("Port is not available\n");
        exit(1);
    }

    printf("Binding on the port %d\n", port_number);
    int listen_status = listen(proxy_socketId, MAX_CLIENT);
    if (listen_status < 0) {
        perror("Error in listening\n");
        exit(1);
    }

    //Now we will define the iterator that will tell how many clients connect
    int i = 0;
    int Connected_socketId[MAX_CLIENT]; //Here we will keep id of the socket which are connected

    while (1) {
        bzero((char*)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);

        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        if (client_socketId < 0) {
            printf("Not able to connect\n");
            continue; // continue instead of exit(1) to keep server alive
        } else {
            Connected_socketId[i] = client_socketId;
        }

        struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;

        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        //inet()->the function convert an address from a network formate to a presentation formate(string formate)

        printf("Client is connected with the port no %d and ip address is %s\n",
               ntohs(client_addr.sin_port), str);

        //Now we will give the new socket to the new thread so that when new client can came remaining thread
        pthread_create(&tid[i], NULL, thread_fn, (void*)&Connected_socketId[i]);
        i++;

        if (i >= MAX_CLIENT) i = 0; // prevent array overflow by reusing slots
    }

    close(proxy_socketId);
    //Now our main function is finished....I will write the thread_fn() now
    return 0;
}



//================Cache element find========================
cache_element* find(char* url){

// Checks for url in the cache if found returns pointer to the respective cache element or else returns NULL
    cache_element* site=NULL;
	//sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 
    if(head!=NULL){
        site = head;
        while (site!=NULL)
        {
            if(!strcmp(site->url,url)){
				printf("LRU Time Track Before : %ld", site->lru_time_track);
                printf("\nurl found\n");
				// Updating the time_track
				site->lru_time_track = time(NULL);
				printf("LRU Time Track After : %ld", site->lru_time_track);
				break;
            }
            site=site->next;
        }       
    }
	else {
        printf("\nurl not found\n");
	}
	//sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
    return site;
}



//=================Remove cache element==================
void remove_cache_element(){
    // If cache is not empty searches for the node which has the least lru_time_track and deletes it
    cache_element * p ;  	// Cache_element Pointer (Prev. Pointer)
	cache_element * q ;		// Cache_element Pointer (Next Pointer)
	cache_element * temp;	// Cache element to remove
    //sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 
	if( head != NULL) { // Cache != empty
		for (q = head, p = head, temp = head ; q -> next != NULL; q = q -> next) { // Iterate through entire cache and search for oldest time track
			if(( (q -> next) -> lru_time_track) < (temp -> lru_time_track)) {
				temp = q -> next;
				p = q;
			}
		}
		if(temp == head) { 
			head = head -> next; /*Handle the base case*/
		} else {
			p->next = temp->next;	
		}//If cache is not empty searches for the node which has the least lru_time_track and delete it

        // updating the cache size: struct + (data + '\0') + (url + '\0')
        cache_size -= (int)sizeof(cache_element);
        cache_size -= temp->len + 1;
        cache_size -= (int)strlen(temp->url) + 1;

		free(temp->data);     		
		free(temp->url); // Free the removed element 
		free(temp);
	} 
	//sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
}



//=========================Add cache element===================
int add_cache_element(char* data,int size,char* url){
    // Adds element to the cache
	// sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Add Cache Lock Acquired %d\n", temp_lock_val);

    // Size of the new element which will be added to the cache
    // (payload + '\0') + (url + '\0') + struct
    int element_size = size + 1 + (int)strlen(url) + 1 + (int)sizeof(cache_element);

    if(element_size > MAX_ELEMENT_SIZE){
		//sem_post(&cache_lock);
        // If element size is greater than MAX_ELEMENT_SIZE we don't add the element to the cache
        temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        return 0;
    }
    else
    {   
        // We keep removing elements from cache until we get enough space to add the element
        // IMPORTANT: remove_cache_element() also locks internally in this code.
        // To avoid deadlock, RELEASE the lock before calling it, then re-acquire.
        while(cache_size + element_size > MAX_SIZE){
            pthread_mutex_unlock(&lock);
            remove_cache_element();
            pthread_mutex_lock(&lock);
        }

        cache_element* element = (cache_element*) malloc(sizeof(cache_element)); // Allocating memory for the new cache element
        if(!element){
            temp_lock_val = pthread_mutex_unlock(&lock);
		    printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
            return 0;
        }

        element->data = (char*)malloc(size+1); // Allocating memory for the response to be stored in the cache element
        if(!element->data){
            free(element);
            temp_lock_val = pthread_mutex_unlock(&lock);
		    printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
            return 0;
        }
		memcpy(element->data, data, (size_t)size);  // binary-safe copy
        element->data[size] = '\0';

        element->url = (char*)malloc((int)strlen(url)+1); // Allocating memory for the request to be stored in the cache element (as a key)
        if(!element->url){
            free(element->data);
            free(element);
            temp_lock_val = pthread_mutex_unlock(&lock);
		    printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
            return 0;
        }
		strcpy( element -> url, url );

		element->lru_time_track=time(NULL);    // Updating the time_track
        element->next=head; 
        element->len=size;
        head=element;

        cache_size+=element_size;

        temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
		//sem_post(&cache_lock);
        return 1;
    }
    return 0;
}
