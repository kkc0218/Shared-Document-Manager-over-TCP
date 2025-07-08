# Shared-Document-Manager-over-TCP

# TCP Shared Document System

> A TCP-based shared document system implementing **Multiple Readers - Single Writer** concurrency control. This project demonstrates socket programming, thread synchronization, and basic document management over the network.

## Project Overview

This project simulates a networked collaborative document editing environment with the following core features:

- **Multiple Clients** can connect to the server simultaneously.
- Each client can perform commands like `create`, `write`, `read`, and `bye`.
- **Multiple reads are allowed concurrently**, but **only one client can write** to a document section at a time.
- Writes are **synchronized** using `mutex` and `condition variables` to avoid race conditions.

## Components

### `server.c`

- Accepts and manages multiple client connections using `pthreads`.
- Maintains a list of documents and their divided sections.
- Each section supports **Multiple Read / Single Write** concurrency via:
  - A per-section mutex (`pthread_mutex_t`)
  - A per-section condition variable (`pthread_cond_t`)
  - A per-section write queue

### `client.c`

- Connects to the server using a TCP socket.
- Sends user-input commands to the server line-by-line.
- Handles server responses until receiving `<END>` as a terminator for multi-line messages.

## Features & Commands

| Command            | Description                                 |
|--------------------|---------------------------------------------|
| `create <doc>`     | Create a new document.                      |
| `read <doc> <sec>` | Read a section of a document.               |
| `write <doc> <sec>`| Request exclusive write access to a section. Input ends with `<END>`. |
| `bye`              | Disconnect from server.                     |

### Example Write Flow

```
Client> create diary 2 "morning" "evening"
[OK] Document created.

Client> write diary morning
[OK] You can start writing. Send <END> to finish.
>> I woke up early and had coffee.
>> Then I went for a walk in the park.
>> <END>
[Write_Completed]

Client> write diary evening
[OK] You can start writing. Send <END> to finish.
>> Cooked dinner and watched a movie.
>> Went to bed early.
>> <END>
[Write_Completed]

Client> read diary morning
diary
    1. morning
       I woke up early and had coffee.
       Then I went for a walk in the park.
__END__

Client> read diary evening
diary
    2. evening
       Cooked dinner and watched a movie.
       Went to bed early.
__END__

Client> bye
[Disconnected]
```

## Directory Structure

```
.
├── client.c         // Client-side TCP socket + command interface
├── server.c         // Server-side TCP multithreading & synchronization
├── README.md        // Project overview and documentation
```

## Thread Synchronization

- Each section maintains:
  - A **write queue** to manage write requests fairly.
  - A **mutex lock** to ensure only one writer at a time.
  - **Readers** can proceed concurrently if no writers are present.
- Threads wait on `pthread_cond_wait()` until it's their turn to write.

## How to Build & Run

### Build

```bash
gcc -pthread -o server server.c
gcc -o client client.c
```

### Run Server

```bash
./server <IP> <port>
```

### Run Client

```bash
./client <server_ip> <port>
```

> Example:
```bash
./server 127.0.0.1 12345
./client 127.0.0.1 12345
```

## Networking Details

- Communication is line-based and TCP-reliable.
- The client sends newline-terminated strings.
- The server uses a delimiter `<END>` to mark the end of multi-line responses (especially for `read` or write prompts).

## To Do / Enhancements

- [ ] Add user authentication
- [ ] Add file persistence (saving documents to disk)
- [ ] Add version control per section
- [ ] Support document listing command (`list`)

## License

MIT License

---

Developed for educational purposes. Demonstrates concurrency, socket communication, and basic synchronization patterns in C.
