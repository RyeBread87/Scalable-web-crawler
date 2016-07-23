#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <pthread.h>

/*
TODO:
1) queue of links (parsers to downloaders). Fixed size
2) queue of pages (downloaders to parsers). Unbounded
3) hash set
4) waiting until done

QUEUE OF LINKS
parser waits when queue is full (need cond variable)
downloader waits when queue is empty (need another cond variable)
need mutex (to make sure we're concurrently accessing data structures)

QUEUE OF PAGE (unbounded)
parser waits when queue is empty (need cond variable)
downloader waits when never (don't need cond variable)
need mutex

WAITING UNTIL DONE
- [NEED TO DO THIS] have to track how much total work in system
  - increment work variable when put on link queue, decrement work when put on
    page queue
  - (count the work for b before I consider a to be done) - add work for all of the links in a before decrementing and saying a is done
- 1 cond var, 1 mutex

[DONE]QUEUE OF LINKS
2 cond vars, 1 mutex

[DONE]QUEUE OF PAGE (unbounded)
1 cond vars, 1 mutex

HASH SET
1 mutex

WAITING UNTIL DONE
1 cond vars, 1 mutex
*/

// SET UP QUEUE DATA STRUCTURE
typedef struct node_t {
  char *pagedata;
  struct node_t *next;
  struct node_t *prev;
  char* fromlink;
  char* pagelink;
} node_t;

typedef struct queue_t {
  node_t *front;
  node_t *end;
  int count;
  int max;
  pthread_mutex_t mutex;
  pthread_cond_t condp;
  pthread_cond_t condd;
} queue_t;

queue_t links;
queue_t pages;

// WAITING UNTIL DONE
typedef struct work_t {
  int count;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} work_t;

work_t work;


//hash function is from:
//http://www.ks.uiuc.edu/Research/vmd/doxygen/hash_8c-source.html

#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get16bits(d) (*((const uint16_t *) (d)))
#endif

#if !defined (get16bits)
#define get16bits(d) ((((uint32_t)(((const uint8_t *)(d))[1])) << 8)+(uint32_t)(((const uint8_t *)(d))[0]) )
#endif

int totalLinks = 0;
uint32_t* visitedLinks = NULL;

uint32_t SuperFastHash (const char * data, int len) {
  uint32_t hash = len, tmp;
  int rem;

  if (len <= 0 || data == NULL) return 0;

  rem = len & 3;
  len >>= 2;

  /* Main loop */
  for (;len > 0; len--) {
    hash  += get16bits (data);
    tmp    = (get16bits (data+2) << 11) ^ hash;
    hash   = (hash << 16) ^ tmp;
    data  += 2*sizeof (uint16_t);
    hash  += hash >> 11;
  }

  /* Handle end cases */
  switch (rem) {
  case 3: hash += get16bits (data);
    hash ^= hash << 16;
    hash ^= ((signed char)data[sizeof (uint16_t)]) << 18;
    hash += hash >> 11;
    break;
  case 2: hash += get16bits (data);
    hash ^= hash << 11;
    hash += hash >> 17;
    break;
  case 1: hash += (signed char)*data;
    hash ^= hash << 10;
    hash += hash >> 1;
  }

  /* Force "avalanching" of final 127 bits */
  hash ^= hash << 3;
  hash += hash >> 5;
  hash ^= hash << 4;
  hash += hash >> 17;
  hash ^= hash << 25;
  hash += hash >> 6;

  return hash;
}

void initialize(uint32_t **linkArray){

  *linkArray=malloc(sizeof(uint32_t));

  if(*linkArray==NULL)
    {
      printf("Malloc error\n");
      return;
    }

  (*linkArray)[0]=0;
}

void add(uint32_t **linkArray, uint32_t newLink){
  
  static int sizeCount = 0;
  totalLinks = sizeCount;
  sizeCount++;

  uint32_t *temp;

  temp=realloc(*linkArray, (sizeCount+1) * sizeof(uint32_t));

  if(temp==NULL)
    {
     printf("Realloc error!");
      return;
    }

  *linkArray = temp;

  (*linkArray)[sizeCount] = newLink;
}



// Pointers to the callback functions
char *(*fetch_ptr)(char *url);
void (*edge_ptr)(char *from, char *to);


// Adds node to the end of the queue
void enqueue(node_t *new_node, queue_t *queue) {

  if (queue->count == 0){
    new_node->next = NULL;
    queue->front = new_node;
    queue->end = new_node;
  } else {
    queue->end->next = new_node;
    queue->end = new_node;
  }
  (queue->count)++;
}

// Removes the node from the front of the queue
node_t *dequeue(queue_t *queue) {

  struct node_t *rem_node = malloc(sizeof(node_t));

  rem_node = queue->front;
  rem_node->pagedata = queue->front->pagedata;
  rem_node->pagelink = queue->front->pagelink;

  if (queue->count == 0){
    return NULL;
  } else  if (queue->count == 1){
    queue->front = NULL;
    queue->end = NULL;
    queue->count = 0;
  } else {
    queue->front = queue->front->next;
    queue->count--;
  }
  return rem_node;
}

// increment work
void increment_work(){

  pthread_mutex_lock(&work.mutex);

  work.count++;

  pthread_cond_signal(&work.cond);

  pthread_mutex_unlock(&work.mutex);
}

// decrement work
void decrement_work(){

  pthread_mutex_lock(&work.mutex);
 
  while(work.count < 0){
    pthread_cond_wait(&work.cond, &work.mutex);
  }
  
  work.count--;
  
  pthread_mutex_unlock(&work.mutex);
}




// Adds the page to the queue of pages
void download_pages ( node_t * ptr){

  pthread_mutex_lock(&pages.mutex);

  // add page to the queue of pages
  enqueue(ptr,&pages);

  // signal parse_pages
  pthread_cond_signal(&pages.condp);

  pthread_mutex_unlock(&pages.mutex);
}


//STARTER FOR DOWNLOADERS
// Remove links from the queue of links, adds to the queue of pages
void *download_links (void *ptr){

  do{

    pthread_mutex_lock(&links.mutex);
    
    // downloader needs to wait while the queue is empty
    while (links.count == 0){
      pthread_cond_wait(&links.condd, &links.mutex);
    }
    // remove the link from the front of the queue
    node_t *tmp = dequeue(&links);

    pthread_cond_signal(&links.condp);

    pthread_mutex_unlock(&links.mutex);

    // !!! fetch outside of the lock!
    char* page= fetch_ptr(tmp->pagelink);
    tmp->pagedata = page;

    // add tmp to the pages queue
    download_pages(tmp);
   
  }while(work.count > 0);
 
  exit(0);
  //pthread_exit(0);
}

// Adds links to the queue of links
void parse_links(node_t *ptr){

  pthread_mutex_lock(&links.mutex);

  // parser needs to wait while the queue is full
  while (links.count == links.max){
    pthread_cond_wait(&links.condp, &links.mutex);
  }

  // add to the link to the end of the queue
  enqueue(ptr,&links);

  // signal download_links
  pthread_cond_signal(&links.condd);

  pthread_mutex_unlock(&links.mutex);

}

// called by parse
char ** parsePage(char* page, char** returnArray, int* numberOfLinks) {

  char * saveptr;
  char * saveptr2;

  char s[2] = " ";
  char n[2] = "\n";
  int i = 0;
  int linkLength = 0;

  char *token = strtok_r(page,n,&saveptr);
  char *token2 = strdup(token);

  token2 = strtok_r(token, s, &saveptr2);

  // gets the line up to new line
  while (token != NULL) {

    // parses out the words in the lines
    while (token2 != NULL) {

      // if the word == 'link:' - ***it's probably possible for link to 
      // show up elsewhere in the line
      if ((token2[0] == 'l') && (token2[1] == 'i') && 
	  (token2[2] == 'n') && (token2[3] == 'k') && (token2[4] == ':')) {

	// if there is something in the line after 'link:'
	if (strlen(token2)>5){
	  linkLength = strlen(token2) - 5 + 1;
	  returnArray[i] = malloc(linkLength * sizeof(char));

	  // add what's after "link:" to the return array
	  int j = 0;
	  for (j = 0; j <= (linkLength - 1); j++) {
	    returnArray[i][j] = token2[5+j];
	  }
	  // add terminating char
	  returnArray[i][j++] = '\0';
	  i++;
	}
      }
      token2 = strtok_r(NULL, s, &saveptr2);
    }

    if (token != NULL) {
      token = strtok_r(NULL, n, &saveptr);
    }

    if ((token == NULL) && (token2 == NULL)) {
      break;
    }
    else {
      token2 = strdup(token);
    }
  }
  *numberOfLinks = i;

  return returnArray;
}

// Parses the page and removes the page from the 
// queue of pages
void parse(node_t *node ){
  
  char *page = node->pagedata;
  char *destLink;
  char** linkArray = malloc(sizeof(char) * 100);
  int* numberOfLinks = malloc(sizeof(int));
  int alreadyDone = 0;
  int testLength = strlen(node->pagelink);
  uint32_t testHash = SuperFastHash(node->pagelink, testLength);
   
  int j=0;
  for (j = 0; j <= totalLinks; j++) {
     if (testHash == visitedLinks[j]) {
      alreadyDone = 1;
    }
  }

  if (alreadyDone != 1){
    add(&visitedLinks, testHash);
  }

  linkArray = (char**) parsePage(page, linkArray, numberOfLinks);
  visitedLinks[totalLinks] = testHash;

  int i=0;
  for (i = 0; i <= *numberOfLinks; i++) {
    if (linkArray[i] != NULL) {

      // new node to store the new link
      node_t *newnode = malloc(sizeof(node_t));
      newnode->fromlink = node->pagelink;
      destLink = strdup(linkArray[i]);
      newnode->pagelink = destLink;

      edge_ptr(node->pagelink, destLink);

      // if we haven't already visited this link
      // yet then add work and add the node to the
      // queue of links
      if (alreadyDone != 1){
	increment_work();
	parse_links(newnode);
      }
    }
  }
  decrement_work();
}


//STARTER FOR PARSERS
// removes page from queue of pages, parses the page, and gives
// the link to the queue of links 
void *parse_pages(void* ptr){

   do{

    pthread_mutex_lock(&pages.mutex);

    // parser waits when the queue of pages is empty
    while (pages.count == 0){
      pthread_cond_wait(&pages.condp, &pages.mutex);
    }

    // remove the page from the front of the queue
    node_t *tmp = dequeue(&pages);
    
    pthread_mutex_unlock(&pages.mutex);

    // parse the info on the page
    // parse calls parse_links
    parse(tmp);

  }while (work.count > 0);

   exit(0);
   //pthread_exit(0);
}

int crawl(char *start_url,
	  int download_workers,
	  int parse_workers,
	  int queue_size,
	  char * (*_fetch_fn)(char *url),
	  void (*_edge_fn)(char *from, char *to)) {
  
  fetch_ptr = _fetch_fn;
  edge_ptr = _edge_fn;

  char *page = fetch_ptr(start_url);
  node_t *newnode = malloc(sizeof(node_t));
  newnode->pagedata = page;
  newnode->pagelink = start_url;
  newnode->fromlink = malloc(sizeof(char));
  
  assert(page != NULL);

  // queue of links
  links.front = NULL;
  links.end = NULL;
  links.count = 0;
  links.max = queue_size;
  pthread_mutex_init(&links.mutex, NULL);
  pthread_cond_init(&links.condp, NULL);
  pthread_cond_init(&links.condd, NULL);

  // queue of pages
  pages.front = NULL;
  pages.end = NULL;
  pages.count = 0;
  pages.max = -1;
  pthread_mutex_init(&pages.mutex, NULL);
  pthread_cond_init(&pages.condp, NULL);

  // work - for 'waiting until done'
  work.count = 0;
  pthread_mutex_init(&work.mutex, NULL);
  pthread_cond_init(&work.cond, NULL);

  // used with hashing
  initialize(&visitedLinks);
  
  // maybe we should handle the first page, first? before creating threads?
  enqueue(newnode,&pages);

  // create threads to do work
  // The downloaders will be responsible for fetching new pages 
  // from the Internet. The parsers will scan the downloaded pages for new links.
  pthread_t downloader[download_workers], parser[parse_workers];
  int i,j;

  for (j=0; j < parse_workers; j++){
    if (pthread_create(&parser[j], NULL, parse_pages, NULL)!=0){
      printf("issue creating parser thread\n");
    } 
  }
  
   for (i=0; i < download_workers; i++){
    if ( pthread_create(&downloader[i], NULL, download_links, NULL)!=0){
      printf("issue creating downloader thread\n");
    } 
  }
  
  // join the threads,.. we quit out without doing much without these calls
  for (j=0; j < parse_workers; j++){
    pthread_join(parser[j], NULL );
  }
  
  for (i=0; i < download_workers; i++){
    pthread_join(downloader[i], NULL);
  }
 
  free(page);

  return 0; // return 0 if succeeds
}

