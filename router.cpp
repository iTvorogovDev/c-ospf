#include <stdio.h>
#include <fstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <iostream>
#include <string>
#include <string.h>
#include <netdb.h>
#include <limits.h>
#include <iterator>
#include <unordered_map>
#include <map>
#include "router.h"

/* REFERENCES

1. https://linux.die.net/man/3/getaddrinfo - how to parse a provided Internet address into a computer-usable format
2. http://man7.org/linux/man-pages/man2/recv.2.html - how to receive messages from a socket
3. https://www.cs.rutgers.edu/~pxk/417/notes/sockets/udp.html - general guide on UDP socket programming

*/

#define BUFSIZE 128

//Variables that are used for business logic of the router
unsigned char buffer[BUFSIZE];
unsigned int router_id;

//Map representing the Link State Database of the router
//Keying by link_id makes it easier to maintain the database and use it for business logic
//Key: link_id
//Value: struct lsdb_entry (cost and IDs of the routers the link connects)
std::unordered_map<int, struct lsdb_entry> lsdb;

//Map representing the Link State Database keyed by the router_id
//Convenient for printing out the topology database
//Key: <router_id, link_id>
//Value: link cost
std::map<std::pair<int, int>, int> rsdb;

//Array of discovered neighbors. An entry is added for each router from which a HELLO is received
//first: router_id
//second: link_id
std::pair<unsigned int, unsigned int> neighbors[NBR_ROUTER];
unsigned int neighbors_length = 0;

//Array of all the routers in the graph, represented as a shortest-path tree
//Useful for printing the RIB
struct spt_node spt[NBR_ROUTER];

//Array keeping track of how many links are attached to a specific router
//Used in printing the topology database
unsigned int nbr_link[NBR_ROUTER];

//Represent the graph of router network as an adjacency matrix
//If there is a link between routers i and j, graph[i][j] = graph[j][i] = link cost
//Otherwise, graph[i][j] = graph[i][j] = -1
int graph[NBR_ROUTER][NBR_ROUTER];

//Variables that are used for syscalls
int sockfd, router_port, flags;
char* nse_host;
char* nse_port;
struct sockaddr_in host_sockaddr_in;
struct addrinfo* nse_addrinfo;
struct addrinfo hints;
struct sockaddr_storage nse_sockaddr_storage;
std::string file_name;
std::ofstream log;

//Helper functions
int process_HELLO(struct pkt_HELLO pkt);
int process_LSPDU(struct pkt_LSPDU pkt);
void dijkstra();

int main(int argc, char* argv[]) {

  //Parse the arguments
  if (argc != 5) {

    std::cerr << "Invalid number of arguments provided!" << std::endl;
    return -1;

  }

  router_id = std::stoi(argv[1]);
  nse_host = argv[2];
  nse_port = argv[3];
  router_port = std::stoi(argv[4]);

  //Initialize the adjacency matrix and the router state
  for (int i = 0; i < NBR_ROUTER; ++i) {

    spt[i].router_id = i + 1;

    for (int j = 0; j < NBR_ROUTER; ++j) {

      if (i == j) {

        graph[i][j] = 0;

      } else {

        graph[i][j] = -1;

      }

    }

  }
  
  file_name = "router" + std::to_string(router_id) + ".log";

  //Set up the router address
  host_sockaddr_in.sin_family = AF_INET;
  host_sockaddr_in.sin_addr.s_addr = INADDR_ANY;
  host_sockaddr_in.sin_port = htons(router_port); 

  //Set up the nse address
  hints.ai_family = AF_INET;    //Allow IPv4 or IPv6
  hints.ai_socktype = SOCK_DGRAM; //Use datagram sockets
  hints.ai_flags = 0;
  hints.ai_protocol = 0;          //Use any protocol

  if (getaddrinfo(nse_host, nse_port, &hints, &nse_addrinfo) < 0) {

    std::cerr << "ERROR: could not resolve the provided hostname" << std::endl;
    return -1;

  }

  memcpy(&nse_sockaddr_storage, nse_addrinfo->ai_addr, nse_addrinfo->ai_addrlen);
  freeaddrinfo(nse_addrinfo);

  //Set up the socket for reception
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {

    std::cerr << "ERROR: cannot create socket" << std::endl; 
    return -1;

  }

  if (bind(sockfd, (struct sockaddr *) &host_sockaddr_in, sizeof(host_sockaddr_in)) < 0) {

    std::cerr << "ERROR: cannot bind the socket" << std::endl;
    return -1;

  }

  flags |= MSG_WAITALL;
  struct pkt_INIT init_packet;
  init_packet.router_id = router_id;
  log.open(file_name, std::ios::out | std::ios::trunc);


  if (sendto(sockfd, (char *) &init_packet, sizeof(init_packet), 0,
          (struct sockaddr *) &nse_sockaddr_storage, sizeof(nse_sockaddr_storage)) < 0) {

    std::cerr << "ERROR: could not send INIT packet" << std::endl;
    return -1;

  } else {

    log << "R" << router_id << " sends an INIT: router_id " << router_id << "\n";

  }

  //Listen for incoming circuit_DB
  struct circuit_DB incoming_circuit_DB;

  if (recv(sockfd, (char*) &incoming_circuit_DB, sizeof(incoming_circuit_DB), flags) <= 0) {

    std::cerr << "ERROR: could not receive a CIRCUIT_DB from the emulator" << std::endl;

  }

  log << "R" << router_id << " receives a CIRCUIT_DB: nbr_link " << incoming_circuit_DB.nbr_link << "\n";

  //Update the topology database based on the circuit DB received
  for (int i = 0; i < incoming_circuit_DB.nbr_link; ++i) {

    lsdb.insert(std::pair<int, struct lsdb_entry>(incoming_circuit_DB.linkcost[i].link,
                                                 {incoming_circuit_DB.linkcost[i].cost, router_id, 0}));
    rsdb.insert(std::pair<std::pair<int, int>, int>(std::pair<int, int>(router_id, incoming_circuit_DB.linkcost[i].link),
                                                    incoming_circuit_DB.linkcost[i].cost));
    ++nbr_link[router_id - 1];

  }

  //Send HELLO packets to the neighbors
  for (int i = 0; i < incoming_circuit_DB.nbr_link; ++i) {

    struct pkt_HELLO hello_packet;
    hello_packet.router_id = router_id;
    hello_packet.link_id = incoming_circuit_DB.linkcost[i].link;
    log << "R" << router_id << " sends a HELLO: router_id " << hello_packet.router_id << " link_id " << hello_packet.link_id << "\n";

    if (sendto(sockfd, (char *) &hello_packet, sizeof(hello_packet), 0,
          (struct sockaddr *) &nse_sockaddr_storage, sizeof(nse_sockaddr_storage)) < 0) {

      std::cerr << "ERROR: could not send HELLO packet through link_id " << hello_packet.link_id << std::endl;
      return -1;

    }

  }

  log.close();

  //From now on, listen to incoming packets indefinitely
  while (true) {

    log.open(file_name, std::ios::out | std::ios::app);
    int bytes_received = recv(sockfd, buffer, BUFSIZE, flags);

    if (bytes_received <= 0) {

      std::cerr << "ERROR: could not receive a HELLO or LSPDU packet" << std::endl;
      return -1;

    }

    //Determine what kind of packet we received
    if (bytes_received == sizeof(struct pkt_HELLO)) {

      struct pkt_HELLO incoming_hello;
      memcpy(&incoming_hello, (struct pkt_HELLO*) buffer, sizeof(struct pkt_HELLO));
      log << "R" << router_id << " receives a HELLO: router_id "
          << incoming_hello.router_id << " link_id " << incoming_hello.link_id << "\n";
      
      if (process_HELLO(incoming_hello) < 0) {

        std::cerr << "ERROR: cannot process an incoming HELLO packet" << std::endl;
        return -1;

      }

    } else if (bytes_received == sizeof(struct pkt_LSPDU)) {

      struct pkt_LSPDU incoming_lspdu;
      memcpy(&incoming_lspdu, (struct pkt_LSPDU*) buffer, sizeof(struct pkt_LSPDU));
      log << "R" << router_id << " receives an LSPDU: sender " << incoming_lspdu.sender
          << " router_id " << incoming_lspdu.router_id << " link_id " << incoming_lspdu.link_id
          << " cost " << incoming_lspdu.cost << " via " << incoming_lspdu.via << "\n";

      if (process_LSPDU(incoming_lspdu) < 0) {

        std::cerr << "ERROR: could not process an incoming LSPDU" << std::endl;
        return -1;

      }

    }

    //Save changes to the file
    log.close();

  }

  //Never terminates
  return 0;

}

int process_HELLO(struct pkt_HELLO pkt) {

  //Include the neighbor in all future communications
  neighbors[neighbors_length] = std::pair<int, int>(pkt.router_id, pkt.link_id); 
  ++neighbors_length;

  //Send a set of LSPDUs to the neighbor from which we received the HELLO
  for (std::unordered_map<int, struct lsdb_entry>::iterator itr = lsdb.begin(); itr != lsdb.end(); ++itr) {

    //Prepare the LSPDU
    struct pkt_LSPDU pkt_out;
    pkt_out.sender = router_id;
    pkt_out.router_id = itr->second.r1;
    pkt_out.link_id = itr->first;
    pkt_out.cost = itr->second.cost;
    pkt_out.via = pkt.link_id;

    log << "R" << router_id << " sends an LSPDU: sender " << pkt_out.sender << " router_id " << pkt_out.router_id
        << " link_id " << pkt_out.link_id << " cost " << pkt_out.cost << " via " << pkt_out.via << "\n";

    //Send the packet
    if (sendto(sockfd, (char *) &pkt_out, sizeof(pkt_out), 0,
        (struct sockaddr *) &nse_sockaddr_storage, sizeof(nse_sockaddr_storage)) < 0) {

      std::cerr << "ERROR: could not send an LSPDU packet through link " << pkt_out.via <<  std::endl;
      return -1;

    }

    //Send another LSPDU for the current link, if we know the second router connected to it
    if (itr->second.r2 > 0) {

      pkt_out.router_id = itr->second.r2;
      log << "R" << router_id << " sends an LSPDU: sender " << pkt_out.sender << " router_id " << pkt_out.router_id
              << " link_id " << pkt_out.link_id << " cost " << pkt_out.cost << " via " << pkt_out.via << "\n";

      if (sendto(sockfd, (char *) &pkt_out, sizeof(pkt_out), 0,
        (struct sockaddr *) &nse_sockaddr_storage, sizeof(nse_sockaddr_storage)) < 0) {

        std::cerr << "ERROR: could not send an LSPDU packet through link " << pkt_out.via <<  std::endl;
        return -1;

      }

    }

  }

  return 0;

}

int process_LSPDU(struct pkt_LSPDU pkt) {

  bool update = false;
  std::unordered_map<int, struct lsdb_entry>::iterator entry = lsdb.find(pkt.link_id);
  
  if (entry == lsdb.end()) {

    //If an entry with the given link_id does not exist yet, create a new entry
    update = true;
    lsdb.insert(std::pair<int, struct lsdb_entry>(pkt.link_id, {pkt.cost, pkt.router_id, 0}));

  } else if (entry->second.r1 != pkt.router_id && entry->second.r2 == 0) {

    //If an entry with the given link_id already exists, but does not have a second router connected to it, update the entry
    update = true;
    entry->second.r2 = pkt.router_id;

    //Update the adjacency matrix representation of the graph
    graph[entry->second.r1 - 1][entry->second.r2 - 1] = entry->second.cost;
    graph[entry->second.r2 - 1][entry->second.r1 - 1] = entry->second.cost;

  }

  if (update) {

    //If the received info is new, perform the appropriate steps.

    //Update the printable topology database
    rsdb.insert(std::pair<std::pair<int, int>, int>(std::pair<int, int>(pkt.router_id, pkt.link_id), pkt.cost));
    ++nbr_link[pkt.router_id - 1];
    log << "#R" << router_id << " Topology Database\n";

    //Output the updated topology database info to the log
    unsigned int last_id = 0;
    for (std::multimap<std::pair<int, int>, int>::iterator itr = rsdb.begin(); itr != rsdb.end(); ++itr) {

      if (last_id != itr->first.first) {

        log << "R" << router_id << " -> R" << itr->first.first << " nbr link " << nbr_link[itr->first.first - 1] << "\n";
        last_id = itr->first.first;

      }

      log << "R" << router_id << " -> R" << itr->first.first << " link " << itr->first.second << " cost " << itr->second << "\n";

    }

    //Propagate the newly received LSPDU across all discovered neighbors
    unsigned int sender = pkt.sender;
    pkt.sender = router_id;

    for (int i = 0; i < neighbors_length; ++i) {

      //Don't send the LSPDU back to the sender
      if (neighbors[i].first == sender) continue;

      pkt.via = neighbors[i].second;
      log << "R" << router_id << " sends an LSPDU: sender " << pkt.sender << " router_id " << pkt.router_id
              << " link_id " << pkt.link_id << " cost " << pkt.cost << " via " << pkt.via << "\n";

      if (sendto(sockfd, (char *) &pkt, sizeof(pkt), 0,
          (struct sockaddr *) &nse_sockaddr_storage, sizeof(nse_sockaddr_storage)) < 0) {

          std::cerr << "ERROR: could not send an LSPDU packet through link " << pkt.via <<  std::endl;
          return -1;

      }

    }

    //Perform Dijkstra's algorithm on the graph
    dijkstra();

  }

  return 0;

}

void dijkstra() {

  //Initialize all nodes as having infinite distances and not being included in the Shortest Path Tree set
  for (int i = 0; i < NBR_ROUTER; ++i) {

    spt[i].dist = UINT_MAX;
    spt[i].in_spt_set = false;
    spt[i].prev = nullptr;

  }

  //The distance from the source to itself is zero, obviously!
  spt[router_id - 1].dist = 0;

  //Iteratively find the shortest path for all vertices
  for (int i = 0; i < NBR_ROUTER; ++i) {

    //Out of vertices not yet processed, pick the one with smallest distance
    unsigned int min_dist = UINT_MAX;
    unsigned int min_index;

    for (int j = 0; j < NBR_ROUTER; ++j) {

      if (spt[j].in_spt_set == false && spt[j].dist <= min_dist) {

        min_dist = spt[j].dist;
        min_index = j;

      }

    }

    //Mark the picked vertex as processed
    spt[min_index].in_spt_set = true;

    //Update the distance values of vertices adjacent to the picked vertex, if
    //they have not yet been processed, and their new distance value would be smaller
    for (int j = 0; j < NBR_ROUTER; ++j) {

      if (spt[j].in_spt_set == false && graph[min_index][j] > -1 && spt[min_index].dist != UINT_MAX
          && spt[min_index].dist + graph[min_index][j] < spt[j].dist) {

            spt[j].dist = spt[min_index].dist + graph[min_index][j];
            spt[j].prev = &spt[min_index];

        }

    }

  }

  //Log the new RIB
  log << "#R" << router_id << " RIB\n";
  log << "R" << router_id << " -> R" << router_id << " -> Local, 0\n";

  struct spt_node* cur;
  struct spt_node* next;
  for (int i = 0; i < NBR_ROUTER; ++i) {

    if (i == router_id - 1) continue;
    else if (spt[i].dist == UINT_MAX) {

      log << "R" << router_id << " -> R" << i + 1 << " -> INF, INF\n";

    } else {

      //Determine the node that the router should take to its destination
      cur = &spt[i];

      while(cur->prev != nullptr) {

        next = cur;
        cur = cur->prev;

      }

      log << "R" << router_id << " -> R" << i + 1 << " -> R" << next->router_id << ", " << spt[i].dist << "\n";

    }

  }


}
