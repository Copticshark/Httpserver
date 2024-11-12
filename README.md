Multithreaded HTTP Server
This project is a multithreaded HTTP server capable of handling concurrent client connections using a queue-based worker thread model. It supports basic HTTP methods (GET and PUT) and includes error logging, directory protection, and atomic response handling.

Features
Multithreading with Connection Queuing: Uses a connection queue and worker threads to handle multiple client requests concurrently, improving performance and responsiveness.
Supported HTTP Methods: Handles GET and PUT requests with proper response codes.
File and Directory Access Control: Protects directories by denying access and uses file locking to prevent read/write conflicts.
Audit Logging: Logs requests, response codes, and request identifiers for easier tracking and debugging.
Configurable Thread Pool: Number of worker threads can be set via command-line arguments.
Setup
Prerequisites
C compiler (e.g., gcc)
POSIX Threads library (usually available on Unix-based systems)
Standard C Libraries (for networking and file handling)
Compilation
Clone the repository to your local machine.
bash
git clone https://github.com/your-username/your-repo.git
Navigate into the project directory.
cd your-repo
Compile the server with your preferred C compiler:
gcc -o httpserver httpserver.c -lpthread
Usage
To run the server, execute the following command:

./httpserver <port> -t <number_of_threads>
<port>: The port number on which the server listens.
-t <number_of_threads>: (Optional) Specifies the number of worker threads. Default is 4.
For example, to start the server on port 8080 with 4 threads:

./httpserver 8080 -t 4
Example Commands
Start server with default threads:


./httpserver 8080
Start server with 8 threads:


./httpserver 8080 -t 8
HTTP Method Details
GET: Retrieves the requested file from the server if it exists and is accessible. Directory access requests return a 403 Forbidden response.
PUT: Uploads a file to the server. If the file exists, it will be overwritten; if not, it will be created. Attempts to upload to a directory return a 403 Forbidden response.
Logging
The server logs request details to stderr with the following format:

php

<operation>, <URI>, <status_code>, <request_id>
Project Structure
httpserver.c: The main server code, handling connections and requests.
asgn4_helper_funcs.h, connection.h, request.h, response.h, queue.h: Header files providing helper functions and data structures for managing connections and requests.
Contributing
Feel free to fork this repository and submit pull requests for enhancements or bug fixes.
