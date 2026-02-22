/*
Author Name: Cooper Huntington-Bugg
Email: cohunti@okstate.edu
Date: 02/22/2026
Program Description: This system implements a concurrent registration pipeline utilizing POSIX message queues.
It creates three separate processes to manage frontend submission, database processing and activity logging.
All three processes share a single queue. Messages are tagged with a type field so each process
knows which messages belong to it.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <time.h>

const char* queueName = "/student_reg_queue";
const int studentCount = 3;

/*
Single message structure used by all three processes.
msg_type = 1 : student submission  (Frontend -> Database)
msg_type = 2 : confirmed registration (Database -> Logger)
*/
struct studentMessage {
    long msg_type;
    char studentName[50];
    int rollNo;
};

time_t startTime;

int getElapsedSeconds() {
    return (int)(time(NULL) - startTime);
}

// Process logic is separated into single function to prioritize clarity

void runFrontendProcess(mqd_t queue) {
    struct studentMessage msg;
    char* students[] = {"Alice", "Bob", "Charlie"};
    int i;

    for (i = 0; i < studentCount; i++) {
        msg.msg_type = 1;
        strcpy(msg.studentName, students[i]);
        msg.rollNo = 0;

        printf("[Frontend] @ %ds: Sending %s...\n", getElapsedSeconds(), msg.studentName);
        sleep(1);

        if (mq_send(queue, (char*)&msg, sizeof(struct studentMessage), 0) == -1) {
            perror("Frontend process encountered an error sending a message");
            exit(1);
        }
    }

    printf("[Frontend] @ %ds: All students submitted! My job is done.\n", getElapsedSeconds());
    exit(0);
}

void runDatabaseProcess(mqd_t queue) {
    struct studentMessage msg;
    int processedCount = 0;

    while (processedCount < studentCount) {
        if (mq_receive(queue, (char*)&msg, sizeof(struct studentMessage), NULL) == -1) {
            perror("Database process encountered an error receiving a message");
            exit(1);
        }

        // Only process type 1 messages — put anything else back for the right process
        if (msg.msg_type != 1) {
            mq_send(queue, (char*)&msg, sizeof(struct studentMessage), 0);
            continue;
        }

        printf("[Database] @ %ds: Start processing %s...\n", getElapsedSeconds(), msg.studentName);
        sleep(3);

        msg.msg_type = 2;
        msg.rollNo = 1001 + processedCount;

        printf("[Database] @ %ds: Finished processing %s. Assigned ID: %d\n", getElapsedSeconds(), msg.studentName, msg.rollNo);

        if (mq_send(queue, (char*)&msg, sizeof(struct studentMessage), 0) == -1) {
            perror("Database process encountered an error sending a confirmation");
            exit(1);
        }

        processedCount++;
    }
    exit(0);
}

void runLoggerProcess(mqd_t queue) {
    struct studentMessage msg;
    int loggedCount = 0;

    while (loggedCount < studentCount) {
        if (mq_receive(queue, (char*)&msg, sizeof(struct studentMessage), NULL) == -1) {
            perror("Logger process encountered an error receiving a message");
            exit(1);
        }

        // Only log type 2 messages — put anything else back for the right process
        if (msg.msg_type != 2) {
            mq_send(queue, (char*)&msg, sizeof(struct studentMessage), 0);
            continue;
        }

        printf("[Logger] @ %ds: CONFIRMED - ID: %d, Name: %s\n", getElapsedSeconds(), msg.rollNo, msg.studentName);
        loggedCount++;
    }
    exit(0);
}

int main() {
    mqd_t queue;
    struct mq_attr queueAttributes;
    pid_t frontendPid;
    pid_t databasePid;
    pid_t loggerPid;

    startTime = time(NULL);

    queueAttributes.mq_flags = 0;
    queueAttributes.mq_maxmsg = 10;
    queueAttributes.mq_msgsize = sizeof(struct studentMessage);
    queueAttributes.mq_curmsgs = 0;

    // Unlink any existing queue to ensure a clean state
    mq_unlink(queueName);

    queue = mq_open(queueName, O_CREAT | O_RDWR, 0666, &queueAttributes);
    if (queue == (mqd_t)-1) {
        perror("Main process encountered an error creating the message queue");
        exit(1);
    }

    frontendPid = fork();
    if (frontendPid < 0) {
        perror("Main process encountered an error forking the frontend process");
        exit(1);
    }
    if (frontendPid == 0) {
        runFrontendProcess(queue);
    }

    databasePid = fork();
    if (databasePid < 0) {
        perror("Main process encountered an error forking the database process");
        exit(1);
    }
    if (databasePid == 0) {
        runDatabaseProcess(queue);
    }

    loggerPid = fork();
    if (loggerPid < 0) {
        perror("Main process encountered an error forking the logger process");
        exit(1);
    }
    if (loggerPid == 0) {
        runLoggerProcess(queue);
    }

    wait(NULL);
    wait(NULL);
    wait(NULL);

    if (mq_close(queue) == -1) {
        perror("Main process encountered an error closing the message queue");
        exit(1);
    }

    if (mq_unlink(queueName) == -1) {
        perror("Main process encountered an error unlinking the message queue");
        exit(1);
    }

    return 0;
}