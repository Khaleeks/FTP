#include <stdio.h>              // Standard I/O functions (printf, scanf, etc.)
#include <stdlib.h>             // Standard library functions (malloc, free, exit, etc.)
#include <string.h>             // String manipulation functions
#include <unistd.h>             // POSIX API (read, write, close, etc.)
#include <sys/socket.h>         // Socket functions and structures
#include <sys/types.h>          // Data types used in system calls
#include <netinet/in.h>         // Internet address family
#include <arpa/inet.h>          // Functions for IP address conversion
#include <netdb.h>              // Network database operations
#include <sys/stat.h>           // File status functions
#include <dirent.h>             // Directory entry functions
#include <errno.h>              // Error number definitions
#include <fcntl.h>              // File control options
#include <signal.h>             // Signal handling

#define BUFFER_SIZE 1024        // Buffer size for data transfer and messages
#define SERVER_PORT 21          // Default FTP server port
#define SERVER_IP "127.0.0.1"   // Default server IP (localhost)
#define MAXPORT 65530           // Maximum port number

int control_fd;                 // File descriptor for the control connection to the server
int data_port = 0;              // Port number used for data connections
int authenticated = 0;          // Flag indicating if the user is authenticated

// Function declarations for all handlers and helpers
void send_command(char *command);
void receive_response(char *response, int size);
void handle_user_command(char *username);
void handle_pass_command(char *password);
void handle_list_command();
void handle_local_list_command();
void handle_cwd_command(char *dir);
void handle_local_cwd_command(char *dir);
void handle_pwd_command();
void handle_local_pwd_command();
void handle_retr_command(char *filename);
void handle_stor_command(char *filename);
void handle_quit_command();
int setup_data_connection();
void display_welcome_message();
int count_arguments(char *command);

int main() {
    struct sockaddr_in server_addr; // Structure to hold server address info

    // Create a socket for the control connection
    if ((control_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed"); // Print error if socket creation fails
        exit(EXIT_FAILURE);               // Exit the program
    }

    // Zero out the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;                     // Set address family to IPv4
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);   // Set server IP address
    server_addr.sin_port = htons(SERVER_PORT);            // Set server port (network byte order)

    // Connect to the FTP server
    if (connect(control_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");      // Print error if connection fails
        close(control_fd);                // Close the socket
        exit(EXIT_FAILURE);               // Exit the program
    }

    display_welcome_message();            // Show welcome/instructions to the user

    char response[BUFFER_SIZE];           // Buffer for server responses
    receive_response(response, BUFFER_SIZE); // Read server greeting
    printf("%s\n", response);             // Print server greeting

    char command[BUFFER_SIZE];            // Buffer for user commands
    while (1) {
        printf("ftp> ");                  // Print the FTP prompt
        fflush(stdout);                   // Flush output to ensure prompt is shown

        if (fgets(command, BUFFER_SIZE, stdin) == NULL) {
            break;                        // Exit loop if input fails (e.g., EOF)
        }

        command[strcspn(command, "\n")] = '\0'; // Remove newline from input

        if (strlen(command) == 0) {
            continue;                     // Skip empty commands
        }

        char cmd[BUFFER_SIZE] = "";       // Buffer for command keyword
        char arg[BUFFER_SIZE] = "";       // Buffer for command argument

        if (sscanf(command, "%s %s", cmd, arg) < 1) {
            continue;                     // Skip if no command found
        }

        int arg_count = count_arguments(command); // Count number of arguments

        /*
         * COMMAND PROCESSING
         * -----------------
         * This section checks which command the user entered and calls 
         * the appropriate handler function after validating the arguments.
         * Each command has specific requirements for the number of arguments.
         */
        if (strcmp(cmd, "USER") == 0) {
            if (arg_count != 2) {
                printf("Usage: USER <username>\n");
            } else {
                handle_user_command(arg);
            }
        } else if (strcmp(cmd, "PASS") == 0) {
            if (arg_count != 2) {
                printf("Usage: PASS <password>\n");
            } else {
                handle_pass_command(arg);
            }
        } else if (strcmp(cmd, "LIST") == 0) {
            if (arg_count != 1) {
                printf("Usage: LIST\n");
            } else {
                handle_list_command();
            }
        } else if (strcmp(cmd, "!LIST") == 0) {
            if (arg_count != 1) {
                printf("Usage: !LIST\n");
            } else {
                handle_local_list_command();
            }
        } else if (strcmp(cmd, "CWD") == 0) {
            if (arg_count != 2) {
                printf("Usage: CWD <directory>\n");
            } else {
                handle_cwd_command(arg);
            }
        } else if (strcmp(cmd, "!CWD") == 0) {
            if (arg_count != 2) {
                printf("Usage: !CWD <directory>\n");
            } else {
                handle_local_cwd_command(arg);
            }
        } else if (strcmp(cmd, "PWD") == 0) {
            if (arg_count != 1) {
                printf("Usage: PWD\n");
            } else {
                handle_pwd_command();
            }
        } else if (strcmp(cmd, "!PWD") == 0) {
            if (arg_count != 1) {
                printf("Usage: !PWD\n");
            } else {
                handle_local_pwd_command();
            }
        } else if (strcmp(cmd, "RETR") == 0) {
            if (arg_count != 2) {
                printf("Usage: RETR <filename>\n");
            } else {
                handle_retr_command(arg);
            }
        } else if (strcmp(cmd, "STOR") == 0) {
            if (arg_count != 2) {
                printf("Usage: STOR <filename>\n");
            } else {
                handle_stor_command(arg);
            }
        } else if (strcmp(cmd, "QUIT") == 0) {
            if (arg_count != 1) {
                printf("Usage: QUIT\n");
            } else {
                handle_quit_command();
                break;
            }
        } else {
            printf("Unknown command: %s\n", cmd);
            printf("Available commands: USER, PASS, LIST, !LIST, CWD, !CWD, PWD, !PWD, RETR, STOR, QUIT\n");
        }
    }
    
    return 0;
}

/*
 * ARGUMENT COUNTING FUNCTION
 * ------------------------
 * This function counts how many space-separated words are in a command,
 * which helps validate that commands have the correct number of arguments.
 */
int count_arguments(char *command) {
    int count = 0; // Counter for arguments
    char *token; // Pointer for tokenizing
    char temp[BUFFER_SIZE]; // Temporary buffer to avoid modifying original
    strncpy(temp, command, BUFFER_SIZE); // Copy command to temp
    token = strtok(temp, " \t"); // Tokenize by space or tab
    while (token != NULL) {
        count++; // Increment count for each token
        token = strtok(NULL, " \t"); // Get next token
    }
    return count; // Return total number of arguments
}

/*
 * WELCOME MESSAGE FUNCTION
 * ---------------------
 * This function displays instructions for the user when the program starts,
 * explaining how to log in and what commands are available.
 */
void display_welcome_message() {
    // Print welcome and usage instructions
    printf("Hello!! Please Authenticate\n");
    printf("1. type \"USER\" followed by a space and your username\n");
    printf("2. type \"PASS\" followed by a space and your password\n");
    printf("or type \"QUIT\" to close connection at any moment\n\n");
    printf("Once Authenticated this is the list of commands:\n");
    printf("\"STOR\" + space + filename | to send a file to the server\n");
    printf("\"RETR\" + space + filename | to download a file from the server\n");
    printf("\"LIST\" | to list all the files under the current server directory\n");
    printf("\"CWD\" + space + directory | to change the current server directory\n");
    printf("\"PWD\" | to display the current server directory\n");
    printf("Add \"!\" before the last three commands to apply them locally\n");
}

/*
 * SERVER COMMUNICATION FUNCTIONS
 * ---------------------------
 * These two functions handle the basic communication with the FTP server:
 * - send_command: Sends a formatted command to the server
 * - receive_response: Reads and processes the server's response
 */
void send_command(char *command) {
    char full_command[BUFFER_SIZE]; // Buffer for full command with CRLF
    sprintf(full_command, "%s\r\n", command); // Append CRLF
    write(control_fd, full_command, strlen(full_command)); // Send to server
}

void receive_response(char *response, int size) {
    memset(response, 0, size); // Clear response buffer
    int bytes_read = read(control_fd, response, size - 1); // Read from server
    if(bytes_read > 0) {
        response[bytes_read] = '\0'; // Null-terminate
        response[strcspn(response, "\r\n")] = '\0'; // Remove trailing newline
    }
}

/*
 * DATA CONNECTION SETUP
 * -------------------
 * This function creates a data connection for transferring files or directory listings.
 * In FTP, there are two connections:
 * 1. Control connection (established in main) for sending commands
 * 2. Data connection (created here) for transferring actual files/data
 *
 * This function:
 * - Creates a new socket
 * - Binds it to a random available port
 * - Tells the FTP server which port to connect to using the PORT command
 */
int setup_data_connection() {
    struct sockaddr_in data_addr; // Data socket address
    socklen_t addr_len = sizeof(data_addr); // Address length
    int data_listen_fd; // Data socket file descriptor
    if ((data_listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Data socket creation failed");
        return -1;
    }
    memset(&data_addr, 0, sizeof(data_addr)); // Zero out address
    data_addr.sin_family = AF_INET; // IPv4
    data_addr.sin_addr.s_addr = INADDR_ANY; // Any local address
    data_addr.sin_port = 0; // Let OS assign port (N > 1024)
    int opt = 1;
    if (setsockopt(data_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(data_listen_fd);
        return -1;
    }
    if (bind(data_listen_fd, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
        perror("Data bind failed");
        close(data_listen_fd);
        return -1;
    }
    if (getsockname(data_listen_fd, (struct sockaddr *)&data_addr, &addr_len) < 0) {
        perror("Getsockname failed");
        close(data_listen_fd);
        return -1;
    }
    data_port = ntohs(data_addr.sin_port); // Store assigned port
    if (listen(data_listen_fd, 1) < 0) {
        perror("Data listen failed");
        close(data_listen_fd);
        return -1;
    }
    char port_command[BUFFER_SIZE]; // Buffer for PORT command
    sprintf(port_command, "PORT 127,0,0,1,%d,%d", data_port / 256, data_port % 256); // Format PORT
    send_command(port_command); // Send PORT command
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE); // Get server response
    printf("%s\n", response); // Print response
    return data_listen_fd; // Return data socket fd
}

/*
 * AUTHENTICATION HANDLERS
 * ---------------------
 * These functions manage the login process:
 * - handle_user_command: Sends the username to the server
 * - handle_pass_command: Sends the password and updates authentication status
 */
void handle_user_command(char *username) {
    char command[BUFFER_SIZE];
    sprintf(command, "USER %s", username); // Format USER command
    send_command(command); // Send to server
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE); // Get server response
    printf("%s\n", response); // Print response
}

void handle_pass_command(char *password) {
    char command[BUFFER_SIZE];
    sprintf(command, "PASS %s", password); // Format PASS command
    send_command(command); // Send to server
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE); // Get server response
    printf("%s\n", response); // Print response
    if (strncmp(response, "230", 3) == 0) {
        authenticated = 1; // Set authenticated flag if login successful
    }
}

/*
 * LIST COMMAND HANDLERS
 * -------------------
 * These functions show directory contents:
 * - handle_list_command: Shows files on the server
 * - handle_local_list_command: Shows files on the local computer
 */
void handle_list_command() {
    if (!authenticated) {
        printf("Not authenticated. Please login first.\n");
        return;
    }
    int data_fd = setup_data_connection(); // Setup data connection
    if (data_fd < 0) {
        return;
    }
    send_command("LIST"); // Send LIST command
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE); // Get server response
    printf("%s\n", response);
    if (strncmp(response, "530", 3) == 0 || response[0] == '5') {
        close(data_fd); // Close data socket if error
        return;
    }
    struct sockaddr_in server_data_addr;
    socklen_t server_addr_len = sizeof(server_data_addr);
    int connection_fd = accept(data_fd, (struct sockaddr *)&server_data_addr, &server_addr_len); // Accept data connection
    if (connection_fd < 0) {
        perror("Data accept failed");
        close(data_fd);
        return;
    }
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(connection_fd, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0'; // Null-terminate
        printf("%s", buffer); // Print directory listing
    }
    if (bytes_read < 0) {
        fprintf(stderr, "Error receiving directory listing: %s\n", strerror(errno));
    }
    close(connection_fd); // Close data connection
    close(data_fd);
    char final_response[BUFFER_SIZE];
    receive_response(final_response, BUFFER_SIZE); // Get final server response
    printf("%s\n", final_response);
}

void handle_local_list_command() {
    system("ls"); // List local directory using shell
}

/*
 * DIRECTORY NAVIGATION HANDLERS
 * --------------------------
 * These functions help change or show the current directory:
 * - handle_cwd_command: Changes directory on the server
 * - handle_local_cwd_command: Changes directory on the local computer
 * - handle_pwd_command: Shows current directory on the server
 * - handle_local_pwd_command: Shows current directory on the local computer
 */
void handle_cwd_command(char *dir) {
    if (!authenticated) {
        printf("Not authenticated. Please login first.\n");
        return;
    }
    char command[BUFFER_SIZE];
    sprintf(command, "CWD %s", dir); // Format CWD command
    send_command(command); // Send to server
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE); // Get server response
    printf("%s\n", response);
}

void handle_local_cwd_command(char *dir) {
    if (chdir(dir) != 0) {
        perror("Local CWD failed"); // Print error if chdir fails
    } else {
        printf("Local directory changed to %s\n", dir); // Print new local directory
    }
}

void handle_pwd_command() {
    if (!authenticated) {
        printf("Not authenticated. Please login first.\n");
        return;
    }
    send_command("PWD"); // Send PWD command
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE); // Get server response
    printf("%s\n", response);
}

void handle_local_pwd_command() {
    char cwd[BUFFER_SIZE];
    if (getcwd(cwd, BUFFER_SIZE) != NULL) {
        printf("%s\n", cwd); // Print local working directory
    } else {
        perror("Local PWD failed"); // Print error if getcwd fails
    }
}

/*
 * FILE TRANSFER HANDLERS
 * --------------------
 * These functions manage downloading and uploading files:
 * - handle_retr_command: Downloads a file from the server
 * - handle_stor_command: Uploads a file to the server
 *
 * Both functions work by:
 * 1. Setting up a data connection
 * 2. Sending the appropriate command (RETR or STOR)
 * 3. Opening the local file (for reading or writing)
 * 4. Transferring data between the file and the data connection
 * 5. Closing all connections and files
 */
void handle_retr_command(char *filename) {
    if (!authenticated) {
        printf("Not authenticated. Please login first.\n");
        return;
    }
    int data_fd = setup_data_connection(); // Setup data connection
    if (data_fd < 0) {
        return;
    }
    char command[BUFFER_SIZE];
    sprintf(command, "RETR %s", filename); // Format RETR command
    send_command(command); // Send to server
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE); // Get server response
    printf("%s\n", response);
    if (strncmp(response, "150", 3) == 0) {
        FILE *fp = fopen(filename, "wb"); // Open file for writing
        if (fp == NULL) {
            perror("File open failed");
            close(data_fd);
            return;
        }
        struct sockaddr_in server_data_addr;
        socklen_t server_addr_len = sizeof(server_data_addr);
        int connection_fd = accept(data_fd, (struct sockaddr *)&server_data_addr, &server_addr_len); // Accept data connection
        if (connection_fd < 0) {
            perror("Data accept failed");
            close(data_fd);
            fclose(fp);
            return;
        }
        char buffer[BUFFER_SIZE];
        ssize_t bytes_read;
        while ((bytes_read = read(connection_fd, buffer, BUFFER_SIZE)) > 0) {
            fwrite(buffer, 1, bytes_read, fp); // Write to file
        }
        if (bytes_read < 0) {
            fprintf(stderr, "Error receiving file: %s\n", strerror(errno));
        }
        fclose(fp); // Close file
        close(connection_fd); // Close data connection
        close(data_fd);
        char final_response[BUFFER_SIZE];
        receive_response(final_response, BUFFER_SIZE); // Get final server response
        printf("%s\n", final_response);
    } else {
        close(data_fd);
    }
}

void handle_stor_command(char *filename) {
    if (!authenticated) {
        printf("Not authenticated. Please login first.\n");
        return;
    }
    FILE *fp = fopen(filename, "rb"); // Open file for reading
    if (fp == NULL) {
        perror("File not found");
        return;
    }
    fclose(fp);
    int data_fd = setup_data_connection(); // Setup data connection
    if (data_fd < 0) {
        return;
    }
    char command[BUFFER_SIZE];
    sprintf(command, "STOR %s", filename); // Format STOR command
    send_command(command); // Send to server
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE); // Get server response
    printf("%s\n", response);
    if (strncmp(response, "150", 3) == 0) {
        fp = fopen(filename, "rb"); // Reopen file for reading
        if (fp == NULL) {
            perror("File open failed");
            close(data_fd);
            return;
        }
        struct sockaddr_in server_data_addr;
        socklen_t server_addr_len = sizeof(server_data_addr);
        int connection_fd = accept(data_fd, (struct sockaddr *)&server_data_addr, &server_addr_len); // Accept data connection
        if (connection_fd < 0) {
            perror("Data accept failed");
            close(data_fd);
            fclose(fp);
            return;
        }
        char buffer[BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
            write(connection_fd, buffer, bytes_read); // Send file data
        }
        fclose(fp); // Close file
        close(connection_fd); // Close data connection
        close(data_fd);
        char final_response[BUFFER_SIZE];
        receive_response(final_response, BUFFER_SIZE); // Get final server response
        printf("%s\n", final_response);
    } else {
        close(data_fd);
    }
}

/*
 * QUIT COMMAND HANDLER
 * -----------------
 * This function cleanly terminates the connection to the FTP server
 * by sending the QUIT command and closing the control connection.
 */
void handle_quit_command() {
    send_command("QUIT"); // Send QUIT command
    char response[BUFFER_SIZE];
    receive_response(response, BUFFER_SIZE); // Get server response
    printf("%s\n", response);
    close(control_fd); // Close control connection
}