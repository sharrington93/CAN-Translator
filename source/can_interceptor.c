/*
 * can_interceptor.c
 *
 *  Created on: Jan 7, 2015
 *      Author: Sean Harrington
 */

#include "all.h"

tree *msg_tree;
const int canfd_on = 1;
int keepRunning = 1;

struct can_queue can_read_queue;

extern sem_t semaphore, mutex;
int queue_len;

int * can_interceptor_thread(int s)
{
	fd_set rdfs;
	char ctrlmsg[CMSG_SPACE(sizeof(struct timeval)) + CMSG_SPACE(sizeof(__u32))];
	struct iovec iov;
	struct msghdr msg;

	struct canfd_frame frame;
	int nbytes;
	struct sockaddr_can addr;
	struct ifreq ifr;

	strcpy(ifr.ifr_name, "can0" );
	ioctl(s, SIOCGIFINDEX, &ifr);
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_on, sizeof(canfd_on));
	if(bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		printf("Problem binding socket");
		return 1;
	}

	iov.iov_base = &frame;
	msg.msg_name = &addr;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &ctrlmsg;


	iov.iov_base = &frame;
	msg.msg_name = &addr;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &ctrlmsg;

	struct message_node msg_node_key;
	struct message_node *result_node;
	
	queue_len = 0;

	while(keepRunning){
		FD_ZERO(&rdfs);
		FD_SET(s, &rdfs);
		if (FD_ISSET(s, &rdfs))
		{
			iov.iov_len = sizeof(frame);
			msg.msg_namelen = sizeof(addr);
			msg.msg_controllen = sizeof(ctrlmsg);
			msg.msg_flags = 0;
			nbytes = recvmsg(s, &msg, 0);

			//Don't allow the translation of bad reads
			if (nbytes < sizeof(struct can_frame))
			{
				fprintf(stderr, "read: incomplete CAN frame\n");
			}
			if (nbytes < 0)
			{
				perror("Close socket?");
				printf("Closing socket\n");
				close(s);

				// DETERMINE WAY TO HANDLE CLOSING OTHER THREADS...
				/*
				pthread_join(txthread, NULL);
				pthread_join(logging, NULL);
				pthread_join(syncthread, NULL);
				delete_tree(signal_tree);
				delete_tree(msg_tree);
				*/
				return 1;
			}
			if (nbytes > 0)
			{
				/* Successfully read CAN message. Obtain CAN ID, find message node that has that ID in the tree and assign
				 * CAN frame to it for processing by another thread.
				 */
				msg_node_key.key = (int)frame.can_id;
				// Need to check if result_node is NULL or if this is valid if the msg doesn't exist in tree...
				result_node = get_message(msg_tree, &msg_node_key, sizeof(struct message_node));
				if(result_node != NULL)
				{
					result_node->data = frame;
					// Add message node pointer to queue that allows another thread to find it quickly to translate.
					// QUEUE CODE HERE...
					//struct can_message *msg = malloc(sizeof(struct can_message));
					//msg->frame = &frame;
					//msg->frame = malloc(sizeof(struct canfd_frame));
					//memcpy(msg->frame, &frame, sizeof(frame));
					//msg->next = NULL;
					//msg->can_signals = result_node->list;
					sem_wait(&mutex);
					if(can_read_queue.head == NULL)
					{
						can_read_queue.head = result_node;
						can_read_queue.tail = result_node;
						result_node->is_queued = 1;
					}
					else if(result_node->is_queued == 0)
					{
						can_read_queue.tail->next = result_node;
						can_read_queue.tail = can_read_queue.tail->next;
						if(can_read_queue.tail == NULL)
						{
							printf("Tail is null\n");
						}
						result_node->is_queued = 1;
					}

					sem_post(&mutex);
					sem_post(&semaphore);
				}
			}
		}
	}
	close(s);

	delete_tree(msg_tree);
	free(msg_tree);
	msg_tree = NULL;
	return 0;
}
