Part 4a.
1. Queues
   - Two queues (stuct queue_t) were created to hold links and pages
   - The queues are constructed of nodes (struct node_t) which hold all of
   the information about the links and the pages
   a. Queue of links
      - The queue of links (queue_t links) is a fixes sized queue (size defined
      	by the parameters passed into crawl)
	   - Contains two conditional variables and one mutex
	   b. Queue of pages
	      - The queue of pages is an unbounded queue
	      	- Contains one conditional variable and one mutex
2. Hash Set
   - The hash function used is from:
   http://www.ks.uiuc.edu/Research/vmd/doxygen/hash_8c-source.html
   - The hash set is used to ensure that we do not get stuck looping through
   pages that we have already visited
3. Waiting until done
   - struct work_t was created to account for the work in the queues
   - Contains one conditional variable and one mutex
   
Program flow:
- Crawl:
  - initializes all of the conditional variables and the locks (for the two queues 
  and the work struct)
  - fetches the page passed is as the start_url and adds the page to the 
  queue of pages
  - creates the number of downloaders and parsers as specified in the parameters of
  crawl (calls pthread_create and then pthread_join on all of the threads)

  - When the parsers are created:
    - Call parse_pages (this is the starter function for each parser)
      	   - parse_pages then:
				- grabs the lock for the queue of pages and removes the next
				  	    page from the front of the queue of pages (if the queue of 
					    	      	  pages is empty, then the parser waits until it receives a signal
							  	   	  that an additional page was added to the queue of pages - will be
									       	  	     signaled by download_pages)
														- releases the lock on the queue of pages
														  	       - calls parse on the page that was removed from the queue
															       	       	     so that we can parse through the info on the page and then add the
																	     	     	links from the page to the queue of links
																			      	       	    - parse:
																					          - calls parsePage to find all of the links on the pages
																						    	  	       	    - then checks to make sure that we have not already visited 
																									      	   	     	  any of the links on the page
																												      	     	      	  - if we have visited them, then we will print out the edge,
																															       	       	       	     but we will not add the link to the queue and increment the work
																																		     	    	     	     (this prevents loops from occurring from our page visits)
																																					     	   	    	       		      - if we have not visited the pages for the links yet, then we 
																																										      	      	       	       increment the amount of work we have (1 unit of work per
																																													       		     	       	       page link that has not yet been visited) and we call parse_links
																																																	       	    	      	      	  - parse_links:
																																																					          - grabs the lock on the queue of links and adds the link 
																																																						    	      	      	  	to the queue
																																																										             - if the queue of links is already full then we will 
																																																											       	      	       	     		wait until space frees up (we will be signaled by 
																																																																     	   	       	      	      download_links)
																																																																				            - once we are done adding all of the links from the page then we 
																																																																					      	      	       	      also decrement the work associated with the page
																																																																								      	   	     - parse_pages will continue to try to remove pages so long as there is
																																																																										       		   still work left in the queues
																																																																												   	      	   
																																																																														   - When the downloaders are created:
																																																																														     - Call download_links (this is the starter function for each downloader)
																																																																														       	    - download_links then:
																																																																															      		      - grabs the lock for the queue of links and removes the node
																																																																																	      	      	  from the front of the queue
																																																																																			       	   	 - sends a signal to parse_links to let the parser that is waiting to 
																																																																																					   	   add a link to the queue of links know that there is now room on the queue
																																																																																						       	      of links
																																																																																							      	   - release lock on queue of links
																																																																																								     	     	  - fetches the information from the page of the link that we just removed
																																																																																										    	    	- calls download_pages on the node that was removed from the queue of links 
																																																																																												  		       so that we can now add the page information to the queue of pages
																																																																																														       	       	      - download_pages:
																																																																																																      	    - grabs the lock on the queue of pages, adds the node to the queue of 
																																																																																																	      	    	     	pages, sends a signal to parse_pages to let the parser know there that 
																																																																																																				       	       	      	 we now have more pages on the queue to process, and then releases 
																																																																																																							    	     	  	the lock on the queue of pages
																																																																																																										    	 - download_links will continue to try to remove links so long as there is
																																																																																																											   		  still work left in the queues
