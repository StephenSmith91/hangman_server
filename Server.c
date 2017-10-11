/* Header files */
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

/* Defines */
#define PORT 12345    /* the default port client will be connecting to */
#define MAXDATASIZE 100 /* max number of bytes we can get at once */
#define BACKLOG 10     /* how many pending connections queue will hold */
#define CHAR_SIZE 20	/* Max Char size for password and username */
#define NUMBER_OF_STRINGS 11 /* Number of usernames and passwords */
#define NUMBER_OF_WORDS 288 /* Number of words in hangman_txt */
#define NUMBER_OF_LETTERS 22
#define PLAY_GAME 1
#define LEADERBOARD 2
#define QUIT 3


/* Global Varaibles */
bool keeprunning = true; // this flag gets set to false when ctl+c is pressed

/* Structs */
struct authentication{
	char usernames[NUMBER_OF_STRINGS][CHAR_SIZE+1];
	char passwords[NUMBER_OF_STRINGS][CHAR_SIZE+1];
}authentication;

struct Words{
	char object[NUMBER_OF_WORDS][NUMBER_OF_LETTERS];
	char objectType[NUMBER_OF_WORDS][NUMBER_OF_LETTERS];
}words;

struct ClientStates{
	int online [NUMBER_OF_STRINGS];
	int gamesPlayed [NUMBER_OF_STRINGS];
	int gamesWon [NUMBER_OF_STRINGS];
	int current_player; // for sinlge thread keeps track of current player
}clientStates;


/* Function Prototypes */
void Instantiate_ClientStates();

void LoadFiles();
void LoadCredentils();
void LoadWords();

void SetupSocket(int argc, char* argv[], int* portNumber, int* sockfd, struct sockaddr_in *my_addr);
void CheckForNewClient(int* sockfd, int* new_fd,\
	struct sockaddr_in *my_addr, struct sockaddr_in *their_addr, socklen_t* sin_size);
void Listen_Accept(int *sockfd, int *new_fd,\
	struct sockaddr_in *my_addr, struct sockaddr_in *their_addr, socklen_t* sin_size);
void AuthenticateClients(int* sockfd, int* new_fd);

void GetClientChoice(int* choice, int new_fd);
bool CheckCorrectInput(int choice, int new_fd);

void RecvNumberFrom_Client(int sockfd, int* number);

int SelectRandomNumber();
char* concat(const char *s1, const char *s2);
void signalhandler(int);

void ShowLeaderBoard(int new_fd, int games_played, int games_won);
bool PlayGame(int new_fd);

/* Funciton Implementations */


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Instantiate_ClientStates ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Instantiate struct members of cleintStates to 0
*/
void Instantiate_ClientStates(){
	/* Instantiate struct members to zero initially */
	for(int ii = 0; ii < NUMBER_OF_STRINGS; ii++){
		clientStates.online[ii] = 0;
		clientStates.gamesPlayed[ii] = 0;
		clientStates.gamesWon[ii] = 0;
	}	
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ LoadFiles~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Interface for loading textfiles functions
*/
void LoadFiles(){
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
		sscanf(buffer, "%s %s", authentication.usernames[i], authentication.passwords[i]);
		i++;
	}
	fclose(fp);
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



/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ CheckForNewClient~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Provides interface for Listening, accetping, and authenticating new clients.
May be used in a while loop in future use, to do this continually

Inputs:
		all socket information needed to do this
*/
void CheckForNewClient(int* sockfd, int* new_fd,\
	struct sockaddr_in *my_addr, struct sockaddr_in *their_addr, socklen_t* sin_size){

	Listen_Accept(sockfd, new_fd, my_addr, their_addr, sin_size);

	AuthenticateClients(sockfd, new_fd);
}


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ Listen_Accept~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Server Listens and Accepts any connect() requests from other Clients

Inputs:
		int* sockfd : The server's socket file descriptor
		int*new_fd  : The clients file descriptor

Outputs:
		Nothing
*/
void Listen_Accept(int* sockfd, int* new_fd,\
	struct sockaddr_in *my_addr, struct sockaddr_in *their_addr, socklen_t* sin_size){

		/* start listnening */
	if (listen(*sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	printf("server starts listnening ...\n");

	if ((*new_fd = accept(*sockfd, (struct sockaddr *)their_addr, sin_size)) == -1) {
		perror("accept");
		
	}
//printf("New fd  = %d\n", *new_fd);
//printf("server: got connection from %s\n", inet_ntoa((*my_addr).sin_addr));

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
void AuthenticateClients(int* sockfd, int* new_fd){
	bool authentic = false;
	char username[9];
	char password[9];
	int numbytes;

	/* Recieve username and password from client */
	if ((numbytes=recv(*new_fd, &username, 9*sizeof(char), 0)) == -1) {
		perror("recv");
		exit(1);
	}

	if ((numbytes=recv(*new_fd, &password, 9*sizeof(char), 0)) == -1) {
		perror("recv");
		exit(1);
	}

	/* Check if Client is authentic */
	for(int ii = 1; ii < NUMBER_OF_STRINGS; ii++){
		if((strcmp(username, authentication.usernames[ii]) == 0)  && \
			(strcmp(password, authentication.passwords[ii]) == 0)){
			authentic = true;

			clientStates.online[ii] = 1;//would like to bind client pid to this address aswell
			clientStates.current_player = ii;

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
		close(*sockfd);
		exit(1);
	} 
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
*/
char* concat(const char *s1, const char *s2){
	char *result = malloc(strlen(s1) + strlen(s2) +1);
	strcpy(result, s1);
	strcat(result, s2);
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
void ShowLeaderBoard(int new_fd, int games_won, int games_played){
	int toSend, answer = 0, result = 0;
	uint16_t network_byte_order_short;

	if(games_played <= 0){

		/* Send 0 to say no info at this time */
		network_byte_order_short = htons(answer);
		send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);

		printf("Im at LeaderBoard - no games_played\n");
		return;
	}
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


	/* Send winning confirmation */
	answer = 1;
	network_byte_order_short = htons(answer);
	send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);

	/* Synching */
	RecvNumberFrom_Client(new_fd, &result);
	/* Synching finished */

	/* Send how many players */
	toSend = 1;
	network_byte_order_short = htons(toSend);
	send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);


	/*Send to Client thier name */
	char * Name = authentication.usernames[clientStates.current_player];
	send(new_fd, Name, 11*sizeof(char), 0);

	/* Send to client thier number of games won */
	toSend = games_won;
	network_byte_order_short = htons(toSend);
	send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);

	/*Send to client their number of games played */
	toSend = games_played;
	network_byte_order_short = htons(toSend);
	send(new_fd, &network_byte_order_short, sizeof(uint16_t), 0);
	return;

	printf("LeaderBoard: Sent games_played = %d\n", toSend);
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

	// Deallocate memory
	for(int jj = 0; jj < strlen(wordtocheck)+1; jj++){ // +1 to account for '\0'
		free(wordtocheck);
		wordtocheck++;
	}

	return won;
}



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
/* ~~~~~~~ Main ~~~~~~~~~~*/
int main(int argc, char* argv[]){
/* signal handling for ctl+c user input */
	signal(SIGINT, signalhandler);

	/* TODO: dynamically allocate memory to structs */

	int games_won = 0; int games_played = 0;
	bool validInput = false;
	int sockfd, new_fd, portNumber, choice = 0;  /* listen on sock_fd, new connection on new_fd */
	struct sockaddr_in my_addr;    /* my address information */
	struct sockaddr_in their_addr; /* connector's address information */
	socklen_t sin_size = sizeof(struct sockaddr_in);

	/* Preliminary funtions - Load files and Setup and accept new Clients */
	LoadFiles();
	SetupSocket(argc,argv, &portNumber, &sockfd, &my_addr);		 
	CheckForNewClient(&sockfd, &new_fd, &my_addr, &their_addr, &sin_size);

	printf("server: got connection from %s\n", \
		inet_ntoa(their_addr.sin_addr));
	 /*char disp[30]; sprintf(disp, "\n%d", portNumber);puts(disp);*/ //checking local funcitons are actually assignining :/

	/* EVENT LOOP */
	while(choice != 3  && keeprunning){
		while(!validInput || choice == 0){
			GetClientChoice(&choice, new_fd); /*Recieves Client choice */
			validInput = CheckCorrectInput(choice, new_fd); /* Check response valid and send confirmation to Client */		
		}
		validInput = false;

		if(choice == QUIT && keeprunning){
			close(new_fd);
			close(sockfd);
			exit(1);

		}else if(choice == LEADERBOARD && keeprunning){
			ShowLeaderBoard(new_fd, games_won, games_played);

		}else if(keeprunning){ /*PLAY_GAME */
			games_played++;	
			if(PlayGame(new_fd)){
				games_won++;
				printf("PlayGame (end) gameswon = %d, games_played = %d\n", games_won, games_played);
			}		

		} choice = 0; /* Reset choice */				
	}
	if(!keeprunning){
		printf("EXIT signal: Closing Sockets\n");
		close(new_fd);
		close(sockfd);
		printf("EXIT signal: Goodbye\n");
		exit(1);
	}

	return 1;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
/* Interrupts */


/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ signalhandler ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Catches ct+c by user and exits program gracefully
*/
void signalhandler(int signum){
	printf("\nEXIT signal: %d. Exiting as soon as possible\n", signum);
	keeprunning = false;
	//exit(1);
}