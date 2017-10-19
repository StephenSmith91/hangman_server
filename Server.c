/* Header files */
#define _GNU_SOURCE

#include <stdio.h> 
#include <stdlib.h>
#include <stdbool.h>
#include <stdlib.h> 
#include <errno.h> 
#include <string.h> 
#include <netdb.h> 
#include <sys/types.h> 
#include <netinet/in.h> 
#include <sys/socket.h> 
#include <unistd.h>
#include <sys/wait.h> 
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>     /* pthread functions and data structures     */
#include <semaphore.h>	/* semaphore functions for Leaderboard section*/


/* Defines */
#define PORT 12345    /* the default port client will be connecting to */
#define MAXDATASIZE 100 /* max number of bytes we can get at once */
#define BACKLOG 10     /* how many pending connections queue will hold */
#define CHAR_SIZE 22	/* Max Char size for password and username */
#define NUMBER_CLIENTS 11 /* Number of usernames and passwords */
#define NUMBER_OF_WORDS 288 /* Number of words in hangman_txt */
#define NUMBER_OF_LETTERS 22
#define PLAY_GAME 1
#define LEADERBOARD 2
#define QUIT 3

#define NUM_HANDLER_THREADS 10/* number of threads used to service requests */


void EventLoop(int new_fd);


/* Global Varaibles */
volatile sig_atomic_t keeprunning = 1; // this flag gets set to false when ctl+c is pressed
int served_clients = -1;
int sockfd;
pthread_t pthr_id[NUM_HANDLER_THREADS]; /* thread IDs  */   
int thr_id[NUM_HANDLER_THREADS][2];  /* col 1: thread IDs. col 2: 1/0 (serving client)/(waiting for client) */     
pthread_t parent_thread_id; /* parent thread ID  */

/* global mutex for our program. assignment initializes it. */
/* note that we use a RECURSIVE mutex, since a handler      */
/* thread might try to lock it twice consecutively.         */
pthread_mutex_t request_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

/* global condition variable for our program. assignment initializes it. */
pthread_cond_t  got_request   = PTHREAD_COND_INITIALIZER;

int num_requests = 0;   /* number of pending requests, initially none */

/* global sempahore mutex, assignment initialsies it */
sem_t rw;

/* global reader mutex */
pthread_mutex_t reader = PTHREAD_MUTEX_INITIALIZER;

int read_count = 0;



/* Structs */

/* Authentication struct - stores passwords and usernames */
//typedef struct Leaderboard;
typedef struct ThreadState threadState_t;// info of a threads client - deallocate if don't have a client

struct authentication{
	char usernames[NUMBER_CLIENTS][CHAR_SIZE+1];
	char passwords[NUMBER_CLIENTS][CHAR_SIZE+1];
}authentication;

/* Word struct - stores all the words for hangman */
struct Words{
	char object[NUMBER_OF_WORDS][NUMBER_OF_LETTERS];
	char objectType[NUMBER_OF_WORDS][NUMBER_OF_LETTERS];
}words;

/* Thread state - information about the threads current client if it has one */
struct ThreadState{
	int socket_id;
	int current_player; // index to the clients place in the leader board
}ThreadState;


/* format of a single request. */
struct request {
    int number;             /* number of the request                  */
	int socket_id;
    struct request* next;   /* pointer to next request, NULL if none. */
};

struct request* requests = NULL;     /* head of linked list of requests. */
struct request* last_request = NULL; /* pointer to last request.         */

/*Leaderboard struct - stores Leaderboard information */
struct Leaderboard{
	char clientNames[NUMBER_CLIENTS][CHAR_SIZE+1];
	int gamesWon[NUMBER_CLIENTS];
	int gamesPlayed[NUMBER_CLIENTS];
}Leaderboard;


/* Function Prototypes */
void Instantiate_LeaderBoard();

void LoadFiles();
void LoadCredentils();
void LoadWords();

void SetupSocket(int argc, char* argv[], int* portNumber, int* sockfd, struct sockaddr_in *my_addr);
void CheckForNewClient(int* sockfd, int* new_fd,\
	struct sockaddr_in *my_addr, struct sockaddr_in *their_addr, socklen_t* sin_size);
void Listen_Accept(int *sockfd, int *new_fd,\
	struct sockaddr_in *my_addr, struct sockaddr_in *their_addr, socklen_t* sin_size);
threadState_t*  AuthenticateClients(int* new_fd);

void GetClientChoice(int* choice, int new_fd);
bool CheckCorrectInput(int choice, int new_fd);

void RecvNumberFrom_Client(int sockfd, int* number);

int SelectRandomNumber();
char* concat(const char *s1, const char *s2);
void signalhandler(int);

void ShowLeaderBoard(int new_fd, int games_played, struct ThreadState *thread_state_ptr);
bool PlayGame(int new_fd);

/* Funciton Implementations */


/*
 * function add_request(): add a request to the requests list
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     request number, linked list mutex.
 * output:    none.
 */
void add_request(int request_num, int socket, pthread_mutex_t* p_mutex, pthread_cond_t*  p_cond_var)
{
    int rc;                         /* return code of pthreads functions.  */
    struct request* a_request;      /* pointer to newly added request.     */

    /* create structure with new request */
	a_request = (struct request*)malloc(sizeof(struct request));
    if (!a_request) { /* malloc failed?? */
	fprintf(stderr, "add_request: out of memory\n");
	exit(1);
}
a_request->number = request_num;
a_request->socket_id = socket;
a_request->next = NULL;

    /* lock the mutex, to assure exclusive access to the list */
rc = pthread_mutex_lock(p_mutex);

    /* add new request to the end of the list, updating list */
    /* pointers as required */
    if (num_requests == 0) { /* special case - list is empty */
requests = a_request;
last_request = a_request;
}
else {
	last_request->next = a_request;
	last_request = a_request;
}

    /* increase total number of pending requests by one. */
num_requests++;

    /* unlock mutex */
rc = pthread_mutex_unlock(p_mutex);

    /* signal the condition variable - there's a new request to handle */
rc = pthread_cond_signal(p_cond_var);
}



/*
 * function get_request(): gets the first pending request from the requests list
 *                         removing it from the list.
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     request number, linked list mutex.
 * output:    pointer to the removed request, or NULL if none.
 * memory:    the returned request need to be freed by the caller.
 */
struct request* get_request(pthread_mutex_t* p_mutex)
{
    int rc;                         /* return code of pthreads functions.  */
    struct request* a_request;      /* pointer to request.                 */

    /* lock the mutex, to assure exclusive access to the list */
	rc = pthread_mutex_lock(p_mutex);

	if (num_requests > 0) {
		a_request = requests;
		requests = a_request->next;
        if (requests == NULL) { /* this was the last request on the list */
		last_request = NULL;
	}
        /* decrease the total number of pending requests */
	num_requests--;
}
    else { /* requests list is empty */
a_request = NULL;
}

    /* unlock mutex */
rc = pthread_mutex_unlock(p_mutex);

    /* return the request to the caller. */
return a_request;
}

/*
 * function handle_request(): handle a single given request.
 * algorithm: prints a message stating that the given thread handled
 *            the given request.
 * input:     request pointer, id of calling thread.
 * output:    none.
 */
void handle_request(struct request* a_request, int thread_id)
{
	if (a_request) {
		printf("Thread '%d' handled request '%d'\n",
			thread_id, a_request->number);
		fflush(stdout);
		EventLoop(a_request->socket_id);
	}
}

/*
 * function handle_requests_loop(): infinite loop of requests handling
 * algorithm: forever, if there are requests to handle, take the first
 *            and handle it. Then wait on the given condition variable,
 *            and when it is signaled, re-do the loop.
 *            increases number of pending requests by one.
 * input:     id of thread, for printing purposes.
 * output:    none.
 */
void* handle_requests_loop(void* data)
{
    int rc;                         /* return code of pthreads functions.  */
    struct request* a_request;      /* pointer to a request.               */
    int thread_id = *((int*)data);  /* thread identifying number           */


    /* lock the mutex, to access the requests list exclusively. */
	rc = pthread_mutex_lock(&request_mutex);

    /* do forever.... */
	while (1) {

        if (num_requests > 0) { /* a request is pending */
		a_request = get_request(&request_mutex);
            if (a_request) { /* got a request - handle it and free it */
                /* unlock mutex - so other threads would be able to handle */
                /* other reqeusts waiting in the queue paralelly.          */
		rc = pthread_mutex_unlock(&request_mutex);
		handle_request(a_request, thread_id);
                free(a_request);  							//need to be able to release this mem
                /* and lock the mutex again. */
                rc = pthread_mutex_lock(&request_mutex);
            }
        }
        else {
            /* wait for a request to arrive. note the mutex will be */
            /* unlocked here, thus allowing other threads access to */
            /* requests list.                                       */

        	rc = pthread_cond_wait(&got_request, &request_mutex);
            /* and after we return from pthread_cond_wait, the mutex  */
            /* is locked again, so we don't need to lock it ourselves */

        }
    }
}




/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Instantiate_ThreadState ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Instantiate struct members of cleintStates to 0
*/
void Instantiate_LeaderBoard(){
	int jj =0;
	/* Instantiate struct members to zero initially */
	for(int ii = 0; ii < NUMBER_CLIENTS; ii++){
		Leaderboard.gamesPlayed[ii] = 0;
		Leaderboard.gamesWon[ii] = 0;
	}
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ LoadFiles~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Interface for loading textfiles functions
*/
void LoadFiles(){
	Instantiate_LeaderBoard();
	LoadCredentils();
	LoadWords();	
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ LoadCredentials~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Loads Authentication data into struct
Opens the Authenticate.txt file with fopen(), reads data with fgets() and closes file with 
fclose()
*/
void LoadCredentils(){
	FILE *fp;
	char* filename = "Authentication.txt"; /* filename, if in diff loc use: "C:\\temp\\test.txt"*/
	char buffer[20];					   /* buffer to hold usernames and passwords */
	int i = 0;
	char* chuckaway1 = (char*)malloc(20*sizeof(char));
	char* chuckaway2 = (char*)malloc(20*sizeof(char));
	int our_number;

	/* TODO: Allocate memory using Malloc instead */

	/* Allocate Space for usernames and passwords */ 

	// if((char *)malloc(CHAR_SIZE*sizeof(char)) == NULL){
	// 	printf("Malloc \n");
	// 	exit(1);
	// }

	/* "r" == read from existing file */
	if((fp = fopen(filename, "r")) == NULL){
		printf("Could not read from filename\n");
	}

	/* fgets() returns null when nothing left but white space, when reading in strings, tabs
	are not counted as new lines, so need to use sscanf to split up the line
	The 1st element of of the list is just "Passwords and Usernames," so can be disregarded 
	*/
	while(fgets(buffer, CHAR_SIZE, fp) != NULL){

		if(i>0){ /* 1st Line of text is unecessary */

			sscanf(buffer, "%s %s", authentication.usernames[i-1], authentication.passwords[i-1]);//authentication.passwords[i-1]


			strcpy(Leaderboard.clientNames[i-1], authentication.usernames[i-1]);

		}else{
			sscanf(buffer, "%s,%s", chuckaway1, chuckaway2);
		}		
		i++;


	}
	fclose(fp);

	free(chuckaway1);
	free(chuckaway2);
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ LoadWords~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Loads all the words from hangman_text.txt file. Does string filtering to remove colons and new-line
characters. The 1st and 2nd words are stored in the struct 'words' members; objects, and objectType,
respectivley 
*/
void LoadWords(){
	FILE *fp;
	char* filename = "hangman_text.txt"; /* filename, if in diff loc use: "C:\\temp\\test.txt"*/
	char buffer[NUMBER_OF_LETTERS];					   /* buffer to hold usernames and passwords */
	int i = 0;

	if((fp = fopen(filename, "r")) == NULL){
		printf("Could not read from filename\n");
	}

	/* %[^,],%[^,\n]":   %[^,] means to get everything but the coma, an put it in the first strin
	,%[^,\n] means get everything but hte coma and the new line charachter and put it in the second 
	*/
	while(fgets(buffer, NUMBER_OF_LETTERS, fp) != NULL){
		sscanf(buffer, "%[^,],%[^,\n]", words.objectType[i], words.object[i]);
		i++;
	}
	fclose(fp);
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Setup Socket~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Sets up Server socket

Inputs:
		all socket information needed to do this
*/
void SetupSocket(int argc, char* argv[], int* portNumber, int* sockfd, struct sockaddr_in *my_addr){

	if(argc < 2){
		*portNumber = PORT;
	}else *portNumber = atoi(argv[1]);

	/* generate the socket */
	if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	/* generate the end point */
	(*my_addr).sin_family = AF_INET;         /* host byte order */
	(*my_addr).sin_port = htons(*portNumber);     /* short, network byte order */
	(*my_addr).sin_addr.s_addr = INADDR_ANY; /* auto-fill with my IP */
		/* bzero(&(my_addr.sin_zero), 8);   ZJL*/     /* zero the rest of the struct */

	/* bind the socket to the end point */
	if (bind(*sockfd, (struct sockaddr *)my_addr, sizeof(struct sockaddr)) \
		== -1) {
		perror("bind");
	exit(1);
}
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ AuthenticateClients~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Recieves username and password from client and checks that they match a pair from the authentication file
Uses a for loop to find which client they are. Once found updates currentplayer to the client list number.
Send 1 for authentic, 0 for non-authentic, back to the client
In event is not authentic, close new_fd, server socket and then exit

Inputs:
		int* sockfd : The server's socket file descriptor
		int*new_fd  : The clients file descriptor

Outputs:
		Nothing, but program won't continue if don't get the correct credentials
*/
threadState_t*  AuthenticateClients(int* new_fd){
	bool authentic = false;
	char username[9];
	char password[9];
	int numbytes;
	threadState_t* thread_state_ptr;

	/* Recieve username and password from client */
	if ((numbytes=recv(*new_fd, &username, 9*sizeof(char), 0)) == -1) {
		perror("recv");
		exit(1);
	}

	if ((numbytes=recv(*new_fd, &password, 9*sizeof(char), 0)) == -1) {
		perror("recv");
		exit(1);
	}

	printf("Authenticate: Client sent:%s\n", username);


	/* Check if Client is authentic */
	for(int ii = 0; ii < NUMBER_CLIENTS; ii++){
		if((strcmp(username, authentication.usernames[ii]) == 0)  && \
			(strcmp(password, authentication.passwords[ii]) == 0)){
			authentic = true;

		thread_state_ptr = (threadState_t*)malloc(sizeof(ThreadState));

			(*thread_state_ptr).current_player = ii; // get index of current player to leader board

			thr_id[(*thread_state_ptr).current_player][1] = 1; // thread is serving a client

			printf("Authenticate: Severfile:%s\n", authentication.usernames[(*thread_state_ptr).current_player]);

			break;
		}
	}
	

	/* Send client the result */
	if(authentic){
		send(*new_fd, "Conf from Server", 18*sizeof(char), 0);
	}else{
		send(*new_fd, "N", 5*sizeof(char), 0);
		close(*new_fd);
		printf("disconnected\n"); /* need would like to send a msg client specific */
	} 
	return thread_state_ptr;
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ GetClientChoice ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Recieve Clients Choice from their game menu, converts from network to host short integer

Inputs:
		int* choice : address of their choice
		int new_fd  : Client file descriptor

Outputs:
		Nothing, but changes value of choice in the main loop
*/
void GetClientChoice(int* choice, int new_fd){
	int numbytes;
	uint16_t recieved;

	if ((numbytes=recv(new_fd, &recieved, sizeof(uint16_t), 0)) == -1) {
		perror("recv:");
		exit(1);
	}
	*choice = ntohs(recieved);
	printf("GetClientChoice: choice = %d\n", *choice);
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ CheckCorrectInput ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Checks the whether the input Client gave for thier game menu is valid, 
sends 1 for valid, 0 for invalid back to the client

Inputs:
		int choice : choice of the client
		int new_fd : clients file discriptor

Outputs:
		bool  	   : Whether the client sent a correct response
*/
bool CheckCorrectInput(int choice, int new_fd){	
	int answer = 1;
	uint16_t network_byte_order_short;

	if((choice >= 1) && (choice <=3)){
		network_byte_order_short = htons(answer);
		send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);
		return true;
	}else{
		answer = 0;
		network_byte_order_short = htons(answer);
		send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);
		return false;
	} 
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ RecvNumberFrom_Client ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Generic function that handles Recieving integers from Client

Inputs:
		int new_fd  : client file discriptor
		int* number : address of variable wish to store integer in

Outputs:
		nothing, but changes value of number, which function calls
*/
void RecvNumberFrom_Client(int new_fd, int* number){
	uint16_t buffer = 0;
	int numbytes;

	/* Recieve from Server if there is any information */
	if ((numbytes=recv(new_fd, &buffer, sizeof(uint16_t), 0)) == -1) {
		perror("recv:");
		exit(1);
	}
	*number = ntohs(buffer);
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ SelectRandomNumber ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*/
int SelectRandomNumber(){
	int random_number;

	random_number = rand() % (NUMBER_OF_WORDS + 1);

	return random_number;
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ concat ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Puts to strings together, and puts a null terminating charcater at the end, eg '/0'
*/
char* concat(const char *s1, const char *s2){
	int jj = 0;
	char *result = malloc(strlen(s1) + strlen(s2) +1);
	// strcpy(result, s1);
	// strcat(result, s2);
	


	//char result [strlen(s2)+strlen(s1)+1];

	for(int ii = 0; ii < strlen(s1)+strlen(s2); ii++){
		if(ii < strlen(s1)){
			result[ii] = s1[ii];
		}else{
			result[ii] = s2[jj];
			jj++;
		}
		
	}
	result[strlen(s1)+strlen(s2)+1] = '\0';
	return result;
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ FindWordLength ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Finds the words specified by the random index and returns their combined length
*/
void FindWordLength(int randomIndex, int* wordLength){
	char letters[20];

	wordLength[0] = strlen(words.object[randomIndex]);
	wordLength[1] = strlen(words.objectType[randomIndex]);

	wordLength[2] = wordLength[0] + wordLength[1];

	/*
	printf("wordlength = %d\n", wordlength);
	printf("%s\n", words.object[randomIndex]);printf("%s\n", words.objectType[randomIndex]);
	*/
	
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ CalcGuessLeft ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Calculates number of guess
*/
int CalcGuessLeft(int wordLength){
	int guess_left;

	if((wordLength + 10) < 26){
		guess_left = wordLength + 10;
	}else{
		guess_left = 26;
	}
	return guess_left;
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ShowLeaderBoard ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*/
void ShowLeaderBoard(int new_fd, int games_played, struct ThreadState *thread_state_ptr){
	int toSend, answer = 0, result = 0;
	uint16_t network_byte_order_short;
	// char Name[11];

	if(games_played <= 0){ //need to have local version so don't read leaderboard

		/* Send 0 to say no info at this time */
		network_byte_order_short = htons(answer);
	send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);

	printf("Im at LeaderBoard - no games_played\n");
	return;
}

	/*Critical Readers section */
pthread_mutex_lock(&reader);

read_count++;
if(read_count == 1){
		sem_wait(&rw); // 1st reader blocks until something in leader board 
	}
	pthread_mutex_unlock(&reader);

	/* Read Now */

	/* {1. Make/Get List of all current players
	   2. For each player put thier statistics (calc as well) in a struct/array} do before this funct
	   		should only need to update this table (set list) & then put each col in a new sorted list
	   3. Sort set order list into sorted (new) List according to criteria
	   	3.1 in ascending order of games won
	   	3.2 if 2 or more players have the same games won, then player with higher percentage
	   		should be displayed last
	   	3.3 If 2 or more players have the same games won & percentages, then disp in alphabetical order
	   4. Send to client 1, to say info at this time
	   answer = 1;
	   5. Send to client, number of players on the list
	   6. Send to client, Name of fist person
	   7. Send to client, their games won
	   8. Send to client, thier games played
	   9. Repeat Steps 4-6 for each player
	*/
	printf("Im at LeaderBoard - games_played is = %d\n", games_played);
	printf("\nClient Name: %s\n", authentication.usernames[(*thread_state_ptr).current_player]);


	//1. Go through leaderboard struct and keep indexes of players who have played a game
	int player_index[NUMBER_CLIENTS]; int players_played = 0; int a;int t;

	for(int ll = 0; ll < NUMBER_CLIENTS; ll++){
		if (Leaderboard.gamesPlayed[ll] > 0){
			player_index[players_played] = ll;
			players_played++;
			printf("Player indexes who have played: %d\n", ll);
		}
	}
	printf("Players who have played: %d\n", players_played);

	//order the player_index array so that it reflects game rules.
	for (int k = 0; k < players_played-1; k++){
		printf("k=%d\n", k);
		for(t = k+1; t < players_played; t++){
			printf("t=%d\n", t);
			if(Leaderboard.gamesWon[player_index[k]] > Leaderboard.gamesWon[player_index[t]]){
				a = player_index[k];
				player_index[k] = player_index[t];
				player_index[t] = a;
			}else if(Leaderboard.gamesWon[player_index[k]] == Leaderboard.gamesWon[player_index[t]]){
				if(Leaderboard.gamesPlayed[player_index[k]] > Leaderboard.gamesPlayed[player_index[t]]){
					a = player_index[k];
					player_index[k] = player_index[t];
					player_index[t] = a;

				}else if(Leaderboard.gamesPlayed[player_index[k]] == Leaderboard.gamesPlayed[player_index[t]]){
					if(Leaderboard.clientNames[player_index[k]][0] > Leaderboard.clientNames[player_index[t]][0]){
						a = player_index[k];
						player_index[k] = player_index[t];
						player_index[t] = a;

					}
				}
			}

		}
	}

		/* Send winning confirmation */
	answer = 1;
	network_byte_order_short = htons(answer);
	send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);

		/* Synching */
	//RecvNumberFrom_Client(new_fd, &result);
	/* Synching finished */

	/* Send how many players */ //STILL TODO for multithread
	toSend = players_played;
	network_byte_order_short = htons(toSend);
	send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);




	for(int k = 0; k < players_played; k++){
		printf("Order of players %s\n", Leaderboard.clientNames[player_index[k]]);


	/*Send to Client thier name */
	//memset(Name, '\0', sizeof(Name));
	//strcpy(Name, Leaderboard.clientNames[(*thread_state_ptr).current_player]);
		send(new_fd, Leaderboard.clientNames[player_index[k]], 11*sizeof(char), 0); 
	//printf("\nClient Name: %s\n", Leaderboard.clientNames[(*thread_state_ptr).current_player]);

	/* Send to client thier number of games won */
		toSend = Leaderboard.gamesWon[player_index[k]];
		network_byte_order_short = htons(toSend);
		send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);

	/*Send to client their number of games played */
		toSend = Leaderboard.gamesPlayed[player_index[k]] ;
		network_byte_order_short = htons(toSend);
		send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);
	}

	/* end readers section */
	pthread_mutex_lock(&reader);
	read_count--;
	if(read_count == 0){
		sem_post(&rw);
	}
	pthread_mutex_unlock(&reader);

}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ PlayGame ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*/
bool PlayGame(int new_fd){
// exit now if ctl+c presssed
	if(!keeprunning){
		return false;
	}

	bool won = false;
	int letters_not__, letters_left_to_guess, numbytes, toSend;	
	int i = 0;
	int randomIndex; //random index of 'words' struct
	int wordLength[3]; // contains word1 length, word2length, and their total length
	int guess_left; // available guesses left
	char guess [2]; //guess from client
	char alreadyGuessed[30]; // letters already guessed by client
	char wordfromthe_Server[30];
	uint16_t network_byte_order_short; 
	char* wordtocheck;


	// For debuggin purposes
	printf("Im at PlayGame. ");
	fflush(stdout); /* make sure this gets printed to screen */

	
	randomIndex = SelectRandomNumber();  // Generate a random number
	FindWordLength(randomIndex, wordLength); //stores numbers in wordlength
	letters_left_to_guess = wordLength[2];   // intially, letters_left_to_guess
	guess_left = CalcGuessLeft(wordLength[2]); // calc guesses left from eqn given


	// Initialize alreadGuessed to zero //
	for(int jj = 0; jj < 30; jj++){
		alreadyGuessed[jj] = ' ';
	}alreadyGuessed[29] = '\0'; //important to put ending character at end of string


	// Intialise wordfromthe_Server
	for(int ii = 0; ii < 30; ii++){
		wordfromthe_Server[ii] = ' ';
	}
	// Format string to send to the client to send '_' wherever there is a letter
	for(int ll = 0; ll < (wordLength[2] + 1); ll++){
		if((ll < wordLength[0]) || (ll > wordLength[0])){
			wordfromthe_Server[ll] = '_';
		}else{
			wordfromthe_Server[ll] = ' ';
		}
	}
	wordfromthe_Server[(wordLength[2] + 1)] = '\0'; //important to put ending character at end of string


	// Send intial guesses left to client
	toSend = guess_left;
	network_byte_order_short = htons(toSend);
	send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);
	printf("guess left = %d\n", guess_left);


	// // START GAME LOOP //

	// //while client hasn't guessed all the letters or ran out of guess
	while((guess_left > 0) && (letters_left_to_guess > 0) && keeprunning){

		// send to client  - thier guesses
		if (send(new_fd, alreadyGuessed, 30*sizeof(char), 0) == -1){
			perror("send: ");
			exit(1);
		}
		printf("alreadyGuessed: %s\n", alreadyGuessed);


		// send number of guess left
		toSend = guess_left;
		network_byte_order_short = htons(toSend);
		if (send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0) == -1){
			perror("send: ");
			exit(1);
		}
		printf("guess left = %d\n", guess_left);


		// Send word to client - start off with just number of space
		if (send(new_fd, wordfromthe_Server, 30*sizeof(char), 0) == -1){
			perror("send: ");
			exit(1);
		}
		printf("%s\n", wordfromthe_Server);
		

		//decrement guesses left - may need to change postion
		guess_left--;

		//guess = recieve guess from client
		if ((numbytes=recv(new_fd, guess, sizeof(char), 0)) == -1) {
			perror("recv: ");
			exit(1);

		}
		//printf("guess recieved from client = %s\n", guess);
		alreadyGuessed[i] = guess[0];//*concat(alreadyGuessed, guess);

		wordtocheck = concat(words.object[randomIndex], words.objectType[randomIndex]);
		for(int ii = 0; ii < strlen(wordtocheck); ii++){
			if(wordtocheck[ii] == guess[0]){
				if(ii < wordLength[0]){
					wordfromthe_Server[ii] = guess[0];
				}else{
					wordfromthe_Server[ii+1] = guess[0];
				}
			}
		}
		//free(wordtocheck);		


		// send guesses_left back to client
		toSend = guess_left;
		network_byte_order_short = htons(toSend);
		if (send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0) == -1){
			perror("send: ");
			exit(1);
		}
		
		// See if client has won or not 

		letters_not__ = 0;

		for(int kk = 0; kk < strlen(wordfromthe_Server); kk++){
			if(wordfromthe_Server[kk] != ' ' && wordfromthe_Server[kk] != '_'){
				letters_not__++;
			}
		} printf("letters_not__ = %d, wordLength = %d\n", letters_not__, wordLength[2]);



		if(letters_not__ >= wordLength[2]){
			won = true;
		// send have won to the client
			toSend = 1;
			network_byte_order_short = htons(toSend);
			if (send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0) == -1){
				perror("send: ");
				exit(1);
			}
			break;
		}else{
			toSend = 0;
			network_byte_order_short = htons(toSend);
			if (send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0) == -1){
				perror("send: ");
				exit(1);
			}
		}	

		i++;
	} // end while
	if(keeprunning){

	//TODOd - free already guessed
		//send client guess
		if (send(new_fd, alreadyGuessed, 30*sizeof(char), 0) == -1){
			perror("send: ");
			exit(1);
		}
		// send number of guess left
		toSend = guess_left;
		network_byte_order_short = htons(toSend);
		if (send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0) == -1){
			perror("send: ");
			exit(1);
		}
		// Send word to client - start off with just number of space
		if (send(new_fd, wordfromthe_Server, 30*sizeof(char), 0) == -1){
			perror("send: ");
			exit(1);
		}
	}

	// Deallocate memory
	for(int jj = 0; jj < strlen(wordtocheck)+1; jj++){ // +1 to account for '\0'
		free(wordtocheck);
	wordtocheck++;
}

return won;
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ EventLoop ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
*/
void EventLoop(int new_fd){
	int choice = 0; 
	bool gameswon = false;
	int games_played = 0;bool validInput = false;
	threadState_t* thread_state_ptr;

	if((thread_state_ptr = AuthenticateClients(&new_fd)) == NULL){
		//  no need to deallocate memory, return;
		return;
	}

	printf("Event Loop: Client Name : %s\n", authentication.usernames[(*thread_state_ptr).current_player]);

	while(keeprunning){
		while(!validInput || choice == 0 && keeprunning){
			GetClientChoice(&choice, new_fd); /*Recieves Client choice */
			validInput = CheckCorrectInput(choice, new_fd); /* Check response valid and send confirmation to Client */		
		}
		validInput = false;

		if(choice == QUIT && keeprunning){ 
			thr_id[(*thread_state_ptr).current_player][1] = 0; // thread is no longer serving client
			free(thread_state_ptr);
			close(new_fd);//other threads won't be able to do this - will need to be handled by main - but might not need to  id have a playing struct variable
			return; // 

		}else if(choice == LEADERBOARD && keeprunning){
			ShowLeaderBoard(new_fd, games_played, thread_state_ptr);

		}else if(keeprunning){ /*PLAY_GAME */	
			gameswon = PlayGame(new_fd);
				games_played++; // this is a local variable

				/* Critical section */
				sem_wait(&rw);

				Leaderboard.gamesPlayed[(*thread_state_ptr).current_player]++;
				if(gameswon){
					Leaderboard.gamesWon[(*thread_state_ptr).current_player]++;
				}				
				
				printf("PlayGame (end) gameswon = %d, games_played = %d\n", Leaderboard.gamesWon[(*thread_state_ptr).current_player], games_played);

				sem_post(&rw);
			}		

		} choice = 0; /* Reset choice */				

			if(!keeprunning){
				printf("Im a working thread about to exit! \n");
				free(thread_state_ptr);
		close(new_fd);//other threads won't be able to do this - will need to be handled by main - but might not need to  id have a playing struct variable
		pthread_exit(NULL); //exit thread
	}
}



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
/* ~~~~~~~ Main ~~~~~~~~~~*/
int main(int argc, char* argv[]){
/* signal handling for ctl+c user input */
	signal(SIGINT, signalhandler);
	LoadFiles();

	/* TODO: dynamically allocate memory to structs */

	int games_won = 0; int games_played = 0;
	bool validInput = false;
	int  new_fd, portNumber, choice = 0;  /* listen on sock_fd, new connection on new_fd */
	struct sockaddr_in my_addr;    /* my address information */
	struct sockaddr_in their_addr; /* connector's address information */
	socklen_t sin_size = sizeof(struct sockaddr_in);


	int i = 0;                                /* loop counter          */

    //pthread_t  p_threads[NUM_HANDLER_THREADS];   /* thread's structures   */
    struct timespec delay;                       /* used for wasting time */
	parent_thread_id = pthread_self();
	int *client_list = malloc(sizeof(int));

    /* semaphore intialisation: set vaule to 1 */
	sem_init(&rw, 0, 1);



	/* Preliminary funtions - Load files and Setup and accept new Clients */
	
	SetupSocket(argc,argv, &portNumber, &sockfd, &my_addr);		 
	//CheckForNewClient(&sockfd, &new_fd, &my_addr, &their_addr, &sin_size);

	// Listen_Accept(*sockfd, *new_fd, *my_addr, *their_addr, *sin_size);
			/* start listnening */
	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}
	printf("server starts listnening ...\n");


    /* create the request-handling threads */
	for (i=0; i<NUM_HANDLER_THREADS; i++) {      
        thr_id[i][1] = 0; //thread is not serving a client
        thr_id[i][0] = i;
        pthread_create(&(pthr_id[i]), NULL, handle_requests_loop, (void*)&thr_id[i][0]);

        printf("Thread ID: %d\n", thr_id[i][0]);
    }

    while(1){ // this is the parent thread (thread #1) loop - keep adding 'jobs' to the list to do


    	if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size)) == -1) {
    		perror("accept");
    	}
    	i++;

    	client_list = (int*)realloc(client_list, sizeof(int) * i);
    	served_clients += 1;
		client_list[served_clients] = new_fd; // store fd's and i, # fd's so can close the prgram with ctl+c

		

		add_request(served_clients, new_fd, &request_mutex, &got_request);
		printf("Request number <%d> is added.\n", num_requests);

		

		printf("server: got connection from %s\n", \
			inet_ntoa(their_addr.sin_addr));
	 	/*char disp[30]; sprintf(disp, "\n%d", portNumber);puts(disp);*/ //checking local funcitons are actually assignining :/

	}

	return 1;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
/* Interrupts */


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ signalhandler ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Catches ct+c by user and exits program gracefully
*/
void signalhandler(int signum){


	switch(signum){

		case SIGINT :


			keeprunning = 0; // get thread to return to pthred_create

			// if main thread - wait until other threads exit
			if(pthread_self() == parent_thread_id){
				printf("\nEXIT signal: %d. Exiting as soon as possible\n", signum);
				printf("EXIT signal: Closing Sockets\n");

				//close(new_fd);
				close(sockfd);
				printf("EXIT signal: Waiting for other threads to exit...\n");	

				// // wait for other threads to exit...
				// for(int ii = 0; ii < NUM_HANDLER_THREADS; ii++){ 
				// 	pthread_kill(pthr_id[ii], 2);
				// 	printf("%d Got a signal delivered to it\n", (int)pthr_id[ii]);
				// }

			// }else{ // non-parent threads quit straight away - or could just get them to quit when finished allocating
				for(int jj = 0; jj < NUM_HANDLER_THREADS; jj++){
					//if(pthread_equal(pthread_self(),pthr_id[jj]) != 0){

					if(thr_id[jj][1] == 0){ // thread is not serving a client
						printf("Not serving client =%d pno = %d\n", jj, (int)pthr_id[jj]);
						pthread_kill(pthr_id[jj], 2); // deallocate memory wherever the thread is
					}else{
						printf("Im serving a client, wait for me: %d,   pno=%d\n", jj, (int)pthr_id[jj]);
						//return;
						pthread_exit(NULL);
					}
				}				
				//}
				
			// for(int ii = 0; ii < NUM_HANDLER_THREADS; ii++){ 
			// 	printf("Main exiting thread %d\n", ii);
			// 	pthread_join(pthr_id[ii], NULL);

			// }


				printf("EXIT signal: parent: Goodbye\n");
				exit(1);
			}

			case SIGPIPE :
			//Ignore on write and return
			break;

			default :
			// Another signal recieved
			break;
		}

}


