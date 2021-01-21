#define NBR_ROUTER 5 /* for simplicity we consider only 5 routers */

struct pkt_HELLO {

  unsigned int router_id; /* id of the router who sends the HELLO PDU */
  unsigned int link_id; /* id of the link through which it is sent */

};

struct pkt_LSPDU {

  unsigned int sender; /* sender of the LS PDU */
  unsigned int router_id; /* router id */
  unsigned int link_id; /* link id */
  unsigned int cost; /* cost of the link */
  unsigned int via; /* id of the link through which the LS PDU is sent */

};

struct pkt_INIT {

  unsigned int router_id; /* id of the router that send the INIT PDU */
  
};

struct link_cost {

  unsigned int link; /* link id */
  unsigned int cost; /* associated cost */

};

struct circuit_DB {

  unsigned int nbr_link; /* number of links attached to a router */
  struct link_cost linkcost[NBR_ROUTER];
  /* we assume that at most NBR_ROUTER links are attached to each router */

};

struct lsdb_entry {

  unsigned int cost;
  unsigned int r1;
  unsigned int r2;

};

//Struct representing a node in a shortest-path tree.
//Used to compute the next router that should be used to reach a given destination
struct spt_node {

  unsigned int router_id;
  unsigned int dist;
  struct spt_node* prev;
  bool in_spt_set;

};
