# RDMA-CS

1. Server
  1. Listener
    1. Listen for incoming connection requests
    1. Accept incoming connections
    1. Pass connection info into the creation of a new pthread (Agent)
    1. Continue for any other connection requests
  1. Agent
    1. Allocate structures for client
    1. Exchange rkeys and MR addresses with the client
    1. Allow client to RW while waiting to receive an operation
      * Disconnect
      * View open MRs
      * Request an open MR’s address and key
      * Open or close a client’s MR
  1. Interface
    1. Set up and create listener thread
    1. Perform various server side operations
      * View connected clients
      * Disconnect a specific client
      * Shutdown server after all clients have disconnected
      * Disconnect all clients and immediately shutdown the server
1. Client
  1. Interface
    1. Connect to a server and create the listener thread
    1. Perform various operations
      * RDMA read
      * Display data
      * Write data into a file
      * RDMA write inline
      * RDMA write
      * Request more memory on the server
      * Open MR on server for other clients to use
      * Close MR on server from other clients’ use
      * View open MRs on the server
      * Request an open MR’s address and key
  1. Listener
    1. Listens for commands sent from the server
      * Server initiated disconnect
      
---
## RDMA-CS v1

A basic single client/single server model that lacks the full feature set.

---
## RDMA-CS v2

A multithreaded client/server that allows for multiple client conenction to one server. Basic administration
features are added alongside the increased functionality.
