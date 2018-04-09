#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80  /* maximum permitted name size, not including \0 */
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* initial number of pebbles per pit */

int port = 3000;
int listenfd;

struct player {
    int fd;
    char name[MAXNAME+1]; 
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits 
                        // pits[NPITS] is the end pit
    //other stuff undoubtedly needed here
    struct player *next;
    char transit[MAXNAME];
};
struct player *player_list = NULL;//the ones with name
struct player *wait_list = NULL;//the ones without name
struct player *current_player = NULL;//the one who is playing

extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char *s);  /* you need to write this one */

/* Accept a connection. 
 * A new file descriptor is created for communication with the client. 
 */
int accept_connection(int fd) {
    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) {
        perror("server: accept");
        close(fd);
        exit(1);
    }

    //set up a new struct
    struct player *a = malloc(sizeof(struct player));
    for(int i = 0;i< 6;i++){
                a->pits[i] = compute_average_pebbles();
            }
    a->pits[6] = 0;
    a->fd = client_fd;
    a->transit[0] = '\0';
    //link it to the wait_list
    a->next = wait_list;
    wait_list = a;

    //write to client
    char *mes = "Welcome to Mancala. What is your name?\r\n";
    write(client_fd,mes,41);
    
    return client_fd;
}

int check_new_line(char *s){
    char *res = strchr(s, '\n');
    if(res == NULL){
        return 1;
    }
    return 0;
}

//Function to check whether the name is valid
int check_name(char *s){
    int len = strlen(s);
    //check length
    if(len >= 80){
        return 1;
    }else if(s[0] == '\n'){
        return 1;
    }
    //check repeat
    for (struct player *p = player_list; p; p = p->next) {
        if(strcmp(s,p->name) == 0){
            return 1;
        }
    }
    return 0;
}

//disconnect the player away from the player_list
int disconnect(struct player *drop){
    int fd = drop->fd;
    //at the front
    if(player_list == drop){
        struct player *new_list = drop->next;
        player_list = new_list;
        free(drop);
    //in the middle
    }else{
        struct player *curr = player_list;
        while(curr->next != NULL && curr->next != drop){
            curr = curr->next;
        }
        curr->next = curr->next->next;
        free(drop);
    }
    return fd;
}

//write to clients  to tell them who to move
void tell(struct player *player){
    for (struct player *p = player_list; p; p = p->next) {
        if(p == player){//tell the player to move
            int client_fd = p->fd;
            write(client_fd, "Your move?\r\n", 13);
        }else{//tell others who to move
            int client_fd = p->fd;
            char statement[MAXMESSAGE];
            snprintf(statement, MAXMESSAGE, "It's %s's move\r\n", player->name);
            write(client_fd, statement, strlen(statement));
        }
        
    }
}

//rearrange pits
int move(int start, int num, struct player *p, int self){
    if(self == 1){//self's pits
        if(num <= 7 - start){//won't go to next player's pit
            for(int i = start;i < start + num;i++){
                p->pits[i] += 1;
            }
            if(num == 7 - start){//end at the end pit
                return 1;
            }
        }else{//go to next player's pit
            for(int i = start;i < 7;i++){//fill up self's pits
                p->pits[i] += 1;
            }
            if(p->next != NULL){//if next player exists
                move(0, num+start-7, p->next, 0);
            }else{//next player is NULL, go to the front
                if(player_list == p){//go back to self
                move(0, num+start-7, player_list, 1);
                }else{//go to the front
                move(0, num+start-7, player_list, 0);
                }
            }
        }
    }else{//other's pits
        if(num <= 6 - start){//won't go to next player's pit
            for(int i = start;i < start + num;i++){
                p->pits[i] += 1;
            }
        }else{//go to next player's pit
            for(int i = start;i < 6;i++){
                p->pits[i] += 1;
            }
            if(p->next != NULL){//if next player exists
                if(p->next == current_player){//if next player is the current player
                    move(0, num-6, p->next, 1);
                }else{//go to the next player
                    move(0, num-6, p->next, 0);
                }
            }else{//next player is NULL, go to the front
                if(player_list == current_player){//if the front is the current player
                    move(0, num-6, player_list, 1);
                }else{//go to the front
                    move(0, num-6, player_list, 0);
                }
            }
        }
    }
    return 0;
}

//print the game state
void show(){
    for (struct player *p = player_list; p; p = p->next) {
        char pits[MAXMESSAGE];
        snprintf(pits, MAXMESSAGE, "%s:  [0]%d [1]%d [2]%d [3]%d [4]%d [5]%d  [end pit]%d\r\n", p->name,p->pits[0],p->pits[1],p->pits[2],p->pits[3],p->pits[4],p->pits[5],p->pits[6]);
        broadcast(pits);
    }
}

int main(int argc, char **argv) {
    char msg[MAXMESSAGE];

    parseargs(argc, argv);
    makelistener();

    int max_fd = listenfd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);
    
    while (!game_is_over()) {
        // select updates the fd_set it receives, so we always use a copy and retain the original.
        fd_set listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }

        // Is it the original socket? Create a new connection ...
        if (FD_ISSET(listenfd, &listen_fds)) {
            int client_fd = accept_connection(listenfd);
            FD_SET(client_fd, &all_fds);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            printf("A new connection\n");
        }

        struct player *cur_2 = player_list;
        while (cur_2 != NULL) {
            //current player makes move
            if(FD_ISSET((*cur_2).fd, &listen_fds) && cur_2 == current_player){
                char buf[MAXMESSAGE];
                int num_read = read(cur_2->fd,buf,MAXMESSAGE);
                //current player disconnect
                if(num_read == 0){
                    //set the new current_player
                    if(cur_2->next != NULL){
                        current_player = cur_2->next;
                    }else{
                        current_player = player_list;
                    }
                    char log_out_mes[MAXMESSAGE];
                    snprintf(log_out_mes, MAXMESSAGE, "%s logged out\r\n", cur_2->name);
                    int client_closed = disconnect(cur_2);
                    //remove the fd away from the fd_set
                    FD_CLR(client_closed, &all_fds);
                    //tell others the log out information, show the state
                    //and tell the current player to move
                    broadcast(log_out_mes);
                    show();
                    tell(current_player);
                }else{
                    //get the number of peddles in the pit
                    buf[num_read] = '\0';
                    int pit_num = strtol(buf,NULL,10);
                    int peddle_num = cur_2->pits[pit_num];
                    //wrong pit number
                    if(pit_num < 0 || pit_num > 5){
                        write(cur_2->fd,"Wrong pit number. Choose Again\r\n",33);
                    //emty pit
                    }else if(peddle_num == 0){
                        write(cur_2->fd,"Empty pit. Choose Again\r\n",26);
                    }else{
                        cur_2->pits[pit_num] = 0;
                        int res = move(pit_num+1, peddle_num, cur_2, 1);
                        char move_mes[MAXMESSAGE];
                        snprintf(move_mes, MAXMESSAGE, "%s moved pit %d\r\n", cur_2->name,pit_num);
                        //tell others the move information, show the state
                        //and tell the current player to move
                        broadcast(move_mes);
                        show();
                        if(res != 1){
                            if(cur_2->next != NULL){
                                current_player = cur_2->next;
                                tell(current_player);
                            }else{
                                current_player = player_list;
                                tell(current_player);
                            }
                        }else{
                            tell(current_player);
                        }
                    }
                }
            //player disconnect
	        }else if(FD_ISSET((*cur_2).fd, &listen_fds)){
                char buf[MAXMESSAGE];
                int num_read = read(cur_2->fd, buf, MAXMESSAGE);
                if(num_read == 0){
                    char log_out_mes[MAXMESSAGE];
                    snprintf(log_out_mes, MAXMESSAGE, "%s logged out\r\n", cur_2->name);
                    int client_closed = disconnect(cur_2);
                    //remove the fd away from the fd_set
                    //and tell others the log out information
                    FD_CLR(client_closed, &all_fds);
                    broadcast(log_out_mes);
                }
            }
	        cur_2 = cur_2->next;
        }

        struct player *cur_1 = wait_list;
        while(cur_1 != NULL){
            //one in wait_list type the name
            if (FD_ISSET((*cur_1).fd, &listen_fds)) {
		        char get[MAXNAME];
                int num_read = read(cur_1->fd, get, MAXNAME);
                get[num_read-1] = '\0';
                //make sure not disconnect
                if(num_read > 0){
                    int rs = check_new_line(get);
                    if(rs == 1){
                        strcat(cur_1->transit, get);
                        memset(&get[0],0,num_read);
                        printf("no new line detected");
                    }else{
                    //check the name
                    strcat(cur_1->transit, get);
                    int a = check_name(cur_1->transit);
                    //invalid name
                    if(a == 1){
                        memset(&cur_1->transit[0],0,MAXNAME);
                        write(cur_1->fd,"Invalid Name.Again\r\n",21);
                    //valid name    
                    }else{
                        strcpy(cur_1->name, cur_1->transit);
                        memset(&cur_1->transit[0],0,MAXNAME);
                        //if no player in the player_list, be the current player
                        if(player_list == NULL){
                            current_player = cur_1;
                        }
                        //remove away from the wait_list
                        if(wait_list == cur_1){
                            struct player *new = cur_1->next;
                            wait_list = new;
                        }else{
                            struct player *current = wait_list;
                            while(current->next != NULL && current->next != cur_1){
                                current = current->next;
                            }
                            current->next = current->next->next;
                        }
                        //link to the player_list
                        cur_1->next = player_list;
                        player_list = cur_1;
                        char log_in_mes[MAXMESSAGE];
                        snprintf(log_in_mes, MAXMESSAGE, "%s logged in\r\n", cur_1->name);
                        //tell others the log in information, show the state
                        //and tell the current player to move
                        broadcast(log_in_mes);
                        show();
                        tell(current_player);
                    }
                }
                }
            }
            cur_1 = cur_1->next;
        }
    }

    broadcast("Game over!\r\n");
    printf("Game over!\n");
    for (struct player *p = player_list; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }
        printf("%s has %d points\r\n", p->name, points);
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        broadcast(msg);
    }

    return 0;
}


void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);  
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}


void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}



/* call this BEFORE linking the new player in to the list */
int compute_average_pebbles() { 
    struct player *p;
    int i;

    if (player_list == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (p = player_list; p; p = p->next) {
        nplayers++;
        for (i = 0; i < NPITS; i++) {
            npebbles += p->pits[i];
        }
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}


int game_is_over() { /* boolean */
    int i;

    if (!player_list) {
       return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = player_list; p; p = p->next) {
        int is_all_empty = 1;
        for (i = 0; i < NPITS; i++) {
            if (p->pits[i]) {
                is_all_empty = 0;
            }
        }
        if (is_all_empty) {
            return 1;
        }
    }
    return 0;
}

void broadcast(char *s){
    for (struct player *p = player_list; p; p = p->next) {
        int client_fd = p->fd;
        int length = strlen(s);
        write(client_fd, s, length);
    }
}