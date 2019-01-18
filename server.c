#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <time.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

#define BUFF_SIZE 1024 /*max text line length*/
#define LISTENQ 8      /*maximum number of client connections */

struct client_info
{
    int conn_sock;
    char ip[INET_ADDRSTRLEN];
};

int clients[100];
int n = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
    int quesID;
    char answer[BUFF_SIZE];
} client_answers_t;

typedef struct
{
    int roomID;
    int num_of_question_of_test;
    int time_of_test;
    int status; //1 = waiting, 2 = testing, 3 = finish
    int num_of_user;
    int list_of_client[100];
} rooms_t;

// user id
int user_current_ID;

//total user
// static int total_user = 1;
//total room
static int total_room = 0;

//list of room
static rooms_t list_of_room[100];

//active room status = 1;
static int active_room_ID;

char recv_answers[BUFF_SIZE];
char send_message[BUFF_SIZE], recv_message[BUFF_SIZE];
int bytes_sent;
int bytes_received;

//user score
int scores;

sqlite3 *db;
client_answers_t answers[100];

int number_of_question;

char *file_name_roomlist = "server_room_list.txt";
char *file_name_questionList = "exam_server.txt";

//print DONE
void printDONE()
{
    printf("--------------DONE--------------\n");
}

//send file to client
void sendFile(int conn_sock, char *file_name)
{

    char buff[BUFSIZ];
    char trash[BUFSIZ];
    int fd;
    int bytes_sent = 0;
    struct stat file_stat;
    ssize_t bytes_received;
    int offset;
    char file_size[256];
    fd = open(file_name, O_RDONLY);

    if (fd == -1)
    {
        fprintf(stderr, "Error opening file --> %s", strerror(errno));
    }
    else
    {
        if (fstat(fd, &file_stat) < 0)
        {
            fprintf(stderr, "Error fstat --> %s", strerror(errno));
        }
        {

            sprintf(file_size, "%ld", file_stat.st_size);
            // Sending file size
            bytes_sent = send(conn_sock, file_size, sizeof(file_size), 0);
            if (bytes_sent <= 0)
            {
                printf("\nConnection closed!\n");
            }
            recv(conn_sock, trash, BUFSIZ - 1, 0);
            int fs_block_sz;
            offset = 0;

            while ((fs_block_sz = sendfile(conn_sock, fd, &offset, BUFSIZ)) > 0)
            {
                printf("\n");
            }

            bytes_received = recv(conn_sock, buff, BUFSIZ - 1, 0);
            if (bytes_received <= 0)
            {
                printf("\nError!Cannot receive data from sever!\n");
                //break;
            }
            buff[bytes_received] = '\0';
        }
    }
}

//create new test room
void create_new_room(int num, int duration, int conn_sock)
{
    total_room = total_room + 1;
    active_room_ID = active_room_ID + 1;
    list_of_room[total_room].roomID = total_room;
    list_of_room[total_room].num_of_question_of_test = num;
    list_of_room[total_room].time_of_test = duration;
    list_of_room[total_room].status = 1;
    list_of_room[total_room].num_of_user = 1;
    list_of_room[total_room].list_of_client[0] = conn_sock; //waiting;
}

void create_roomlist_file()
{
    int i;
    FILE *ptr;
    ptr = fopen(file_name_roomlist, "w");
    char payload[BUFF_SIZE];
    for (i = 1; i < total_room + 1; i++)
    {
        sprintf(payload, "%d\t%d\t%d\t%d\n", list_of_room[i].roomID, list_of_room[i].num_of_question_of_test, list_of_room[i].time_of_test, list_of_room[i].status);
        fputs(payload, ptr);
    }
    fputs("\n", ptr);
    fclose(ptr);
}

void send_roomlist_file(int conn_sock)
{
    memset(send_message, 0, strlen(send_message));
    char trash[BUFF_SIZE];
    FILE *ptr;
    ptr = fopen(file_name_roomlist, "r");
    sprintf(send_message, "SENDROOMLIST-%d", total_room);
    bytes_sent = send(conn_sock, send_message, strlen(send_message), 0);
    recv(conn_sock, trash, BUFSIZ - 1, 0);
    puts(trash);
    sendFile(conn_sock, file_name_roomlist);
    fclose(ptr);
    remove(file_name_roomlist);
}

//create room list file and send to client
void create_roomlist_file_and_send(int conn_sock)
{
    char trash[BUFF_SIZE];
    if (total_room == 0 || active_room_ID == 0)
    {
        printf("There is no active room\n");
        bytes_sent = send(conn_sock, "NOROOM- ", strlen("NOROOM- "), 0);
        recv(conn_sock, trash, BUFSIZ - 1, 0);
    }
    else
    {
        create_roomlist_file();
        send_roomlist_file(conn_sock);
    }
}

//write question from database to file
static int callback_write(void *data, int argc, char **argv, char **azColName)
{
    int i;
    //fprintf(stderr, "%s: ", (const char *)data);
    FILE *ptr;
    ptr = fopen(file_name_questionList, "a");
    char payload[BUFF_SIZE];

    for (i = 0; i < argc - 1; i++)
    {
        // char trash[BUFF_SIZE];
        sprintf(payload, "%s\t", argv[i]);
        fputs(payload, ptr);
    }
    fputs("\n", ptr);
    fclose(ptr);
    return 0;
}

//check correct answer
static int callback_judge(void *data, int argc, char **argv, char **azColName)
{
    // int i;
    printf("user_answer: %s, correct_answer: %s\n", (char *)data, argv[argc - 1]);
    if (strcmp(data, argv[argc - 1]) == 0)
    {
        scores++;
    }
    return 0;
}

//create file question list
void prepare_test()
{
    char *zErrMsg = 0;
    int rc;
    rc = sqlite3_open("serverfiles/test.db", &db);

    if (rc)
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return;
    }
    else
    {
        fprintf(stderr, "Opened database successfully\n");
    }
    const char *data = "Callback function called";
    char *sql;
    //change
    sql = "SELECT * from question";
    rc = sqlite3_exec(db, sql, callback_write, (void *)data, &zErrMsg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
    }
    else
    {
        //fprintf(stdout, "Operation done successfully\n");
    }
    sqlite3_close(db);
}

//send test to client
void send_test(int conn_sock)
{
    // char trash[BUFF_SIZE];
    bytes_sent = send(conn_sock, "SEND- ", strlen("SEND- "), 0);
    //puts(trash);
    sleep(1);
    sendFile(conn_sock, file_name_questionList);
}

void progress_user_answers(int num_of_question)
{
    memset(send_message, 0, strlen(send_message));
    int i = 0;
    scores = 0;
    char *idAndAnswer[2];
    char *listOfAnswer[num_of_question];
    //receive answers and save to array
    char *p = strtok(recv_answers, ":");
    while (p != NULL)
    {
        printf("%s\n", p);
        listOfAnswer[i++] = p;
        p = strtok(NULL, ":");
    }
    for (i = 0; i < num_of_question; i++)
    {
        int temp = 0;
        char *dump = strtok(listOfAnswer[i], "-");
        while (dump != NULL)
        {
            idAndAnswer[temp++] = dump;
            dump = strtok(NULL, "\0");
        }
        answers[i].quesID = atoi(idAndAnswer[0]);
        strcpy(answers[i].answer, idAndAnswer[1]);
        printf("%d/%s\n", answers[i].quesID, answers[i].answer);
    }
}

void compare_answers_with_db(int num_of_question)
{
    int i;
    char *zErrMsg = 0;
    int rc;
    rc = sqlite3_open("serverfiles/test.db", &db);

    if (rc)
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return;
    }
    else
    {
        //fprintf(stderr, "Opened database successfully\n");
    }

    for (i = 0; i < num_of_question; i++)
    {
        char *data = answers[i].answer;
        int id = answers[i].quesID;
        char sql[BUFF_SIZE];
        sprintf(sql, "SELECT * from question WHERE id =  %d;", id);

        rc = sqlite3_exec(db, sql, callback_judge, (void *)data, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            fprintf(stderr, "SQL error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
        }
        else
        {
            fprintf(stdout, "Operation done successfully\n");
        }
    }
    sqlite3_close(db);
}

//check anwers from client and send score to client
void judge(int conn_sock, int num_of_question)
{
    progress_user_answers(num_of_question);
    //compare correct answers with database
    compare_answers_with_db(num_of_question);
    //send score to client
    printf("score: %d\n", scores);
    sprintf(send_message, "SENDRESULT-%d", scores);
    send(conn_sock, send_message, strlen(send_message), 0);
}

void sendtoall(char *msg, int curr)
{
    int i;
    pthread_mutex_lock(&mutex);
    for (i = 0; i < n; i++)
    {
        if (clients[i] != curr)
        {
            if (send(clients[i], msg, strlen(msg), 0) < 0)
            {
                perror("sending failure");
                continue;
            }
        }
    }
    pthread_mutex_unlock(&mutex);
}

void send_START_signal(int choosen_room)
{
    int i, j;
    pthread_mutex_lock(&mutex);
    for (i = 0; i < n; i++)
    {
        for (j = 0; j < list_of_room[choosen_room].num_of_user; j++)
        {
            if (clients[i] == list_of_room[choosen_room].list_of_client[j])
            {
                bytes_sent = send(clients[i], "BEGINTESTROOM- ", strlen("BEGINTESTROOM- "), 0);
            }
            else
            {
                printf("error\n");
            }
        }
    }
    pthread_mutex_unlock(&mutex);
}

void *echo(void *sock)
{
    struct client_info client = *((struct client_info *)sock);
    // char msg[500];
    // int len;
    int j;
    while (1)
    {
        int bytes_received;
        char *roomInformation[2];

        memset(recv_message, '\0', strlen(recv_message));
        char *request[2];
        bytes_received = recv(client.conn_sock, recv_message, BUFF_SIZE - 1, 0);
        recv_message[bytes_received] = '\0';
        int i = 0;
        char *p = strtok(recv_message, "-");
        while (p != NULL)
        {
            request[i++] = p;
            p = strtok(NULL, "\0");
        }
        printf("Receiving data ...\n");

        if (strcmp(request[0], "PRACTICE") == 0)
        {
            printf("Practice start\n");
            send_test(client.conn_sock);
            number_of_question = 3;
            printDONE();
        }
        else if (strcmp(request[0], "NEW") == 0)
        {
            char trash[BUFSIZ];
            i = 0;
            char *dump = strtok(request[1], "-");
            while (dump != NULL)
            {
                roomInformation[i++] = dump;
                dump = strtok(NULL, "\0");
            }
            printf("Number of question: \"%d\". \n", atoi(roomInformation[0]));
            number_of_question = atoi(roomInformation[0]);
            printf("Time: \"%d\". \n", atoi(roomInformation[1]));
            create_new_room(atoi(roomInformation[0]), atoi(roomInformation[1]), client.conn_sock);
            sprintf(send_message, "CREATEROOM-%d\n", total_room);
            bytes_sent = send(client.conn_sock, send_message, strlen(send_message), 0);
            recv(client.conn_sock, trash, BUFF_SIZE - 1, 0);
            printDONE();
        }
        else if (strcmp(request[0], "START") == 0)
        {
            int id = atoi(request[1]);
            active_room_ID = active_room_ID - 1;
            send_START_signal(id);
            list_of_room[total_room].status = 2;
            send_test(client.conn_sock);
            printDONE();
        }
        else if (strcmp(request[0], "REQUESTLIST") == 0)
        {
            create_roomlist_file_and_send(client.conn_sock);
            printDONE();
        }
        else if (strcmp(request[0], "CHOOSE") == 0)
        {
            int roomID_choose = atoi(request[1]);
            list_of_room[roomID_choose].list_of_client[list_of_room[roomID_choose].num_of_user] = client.conn_sock;
            list_of_room[roomID_choose].num_of_user++;
            printDONE();
        }
        else if (strcmp(request[0], "READY_TEST_ROOM") == 0)
        {
            send_test(client.conn_sock);
            printDONE();
        }
        else if (strcmp(request[0], "FINISH") == 0)
        {
            strcpy(recv_answers, request[1]);
            recv_answers[strlen(recv_answers) - 1] = '\0';
            judge(client.conn_sock, number_of_question);
            printDONE();
        }
        else if (strcmp(request[0], "QUIT") == 0)
        {
            printDONE();
            break;
        }
        else
        {
            printf("Client disconnect unexpectedly\n");
            printDONE();
        }
    }
    int i;
    printf("User exited. \n");
    pthread_mutex_lock(&mutex);
    printf("%s disconnected\n", client.ip);
    for (i = 0; i < n; i++)
    {
        if (clients[i] == client.conn_sock)
        {
            j = i;
            while (j < n - 1)
            {
                clients[j] = clients[j + 1];
                j++;
            }
        }
    }
    n--;
    pthread_mutex_unlock(&mutex);

    close(client.conn_sock);
    pthread_exit(NULL);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("error, too many or too few arguments\n");
        printf("Correct format is /.server YourPort\n");
        return 1;
    }

    // int sockfd;
    // struct sockaddr_in servaddr, cliaddr;
    int listen_sock, conn_sock; /* file descriptors */

    struct sockaddr_in server_addr; /* server's address information */
    struct sockaddr_in client_addr; /* client's address information */
    int sin_size;

    //Step 1: Construct a TCP socket to listen connection request
    if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    { /* calls socket() */
        perror("\nError: ");
        return 0;
    }
    //Step 2: Bind address to socket
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[1]));     /* Remember htons() from "Conversions" section? =) */
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); /* INADDR_ANY puts your IP address automatically */
    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    { /* calls bind() */
        perror("\nError: ");
        return 0;
    }

    //Step 3: Listen request from client
    if (listen(listen_sock, LISTENQ) == -1)
    { /* calls listen() */
        perror("\nError: ");
        return 0;
    }
    // int pid;
    //Step 4: Communicate with client"
    printf(" -- BEGIN PROGRESS -- \n");

    pthread_t recvt;
    char ip[INET_ADDRSTRLEN];
    struct client_info client;
    prepare_test();

    while (1)
    {
        //accept request
        sin_size = sizeof(struct sockaddr_in);
        if ((conn_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &sin_size)) == -1)
            perror("\nError: ");

        printf("You got a connection from %s - %d\n", inet_ntoa(client_addr.sin_addr), conn_sock); /* prints client's IP */
        pthread_mutex_lock(&mutex);
        inet_ntop(AF_INET, (struct sockaddr *)&client_addr, ip, INET_ADDRSTRLEN);
        client.conn_sock = conn_sock;
        strcpy(client.ip, ip);
        clients[n] = conn_sock;
        n++;
        printf("%s connected\n", ip);

        pthread_create(&recvt, NULL, echo, &client);
        pthread_mutex_unlock(&mutex);
    }
    close(listen_sock);
    remove(file_name_questionList);
    return 0;
}
