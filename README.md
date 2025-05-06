# FTP Client-Server Project

This project implements a simple FTP (File Transfer Protocol) client and server in C, following the RFC 959 specification. The system supports user authentication, directory navigation, and file transfers between a client and server over TCP.

## Features

- **User Authentication**: Users must log in with a username and password (stored in `users.csv`).
- **Directory Isolation**: Each user has a separate directory on the server.
- **FTP Commands Supported**:
  - `USER <username>`: Provide username
  - `PASS <password>`: Provide password
  - `LIST`: List files in the current server directory
  - `CWD <directory>`: Change server directory
  - `PWD`: Show current server directory
  - `RETR <filename>`: Download file from server
  - `STOR <filename>`: Upload file to server
  - `QUIT`: Disconnect from server
  - Local commands: `!LIST`, `!CWD`, `!PWD` for local filesystem operations

## Directory Structure

```
.
├── code/
│   ├── client.c
│   └── server.c
├── client/
│   └── client (compiled binary)
├── server/
│   └── server (compiled binary)
├── users.csv
├── Makefile
└── README.md
```

## Getting Started

### Prerequisites

- GCC (or any C compiler)
- Unix-like OS (Linux, macOS)

### Build

Compile both client and server using the provided Makefile:

```sh
make
```

### Usage

1. **Start the server** (in one terminal):

    ```sh
    make run-server
    ```

2. **Start the client** (in another terminal):

    ```sh
    make run-client
    ```

3. **Authenticate**:

    - Enter `USER <username>`
    - Enter `PASS <password>`

    User credentials are stored in [`users.csv`](users.csv).

4. **Use FTP commands** as described above.

### Example Session

```
ftp> USER bob
331 Username OK, need password.
ftp> PASS donuts
230 User logged in, proceed.
ftp> LIST
file1.txt
file2.jpg
ftp> RETR file1.txt
226 Transfer complete.
ftp> QUIT
221 Service closing control connection.
```

## Notes

- The server runs on port 21 by default and uses active mode FTP (PORT command).
- Each user has a dedicated directory on the server for isolation.
- Only basic error handling is implemented; more robust checks are recommended for production use.


## Authors
**Reginald Kotey Appiah Sekyere** – [GitHub Profile](https://github.com/itsRekas)

**Khaleeqa Aasiyah** – [GitHub Profile](https://github.com/Khaleeks)


## License

This project is for educational purposes.
