#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/netlink.h>
#include <net/sock.h>

#define NETLINK_MSG_TYPE 31
#define MAX_MSG_SIZE 1024
#define GPIO_1 49 // power failure GPIO
#define GPIO_2 191 // power button GPIO

struct sock *nl_sk; // netlink socket
struct sk_buff *skb_out; // netlink message buffer
struct nlmsghdr *nlh; // netlink message header
int pid; // pid of the process to send messages to
int ret; // return value for GPIO functions

// workqueue for sending messages to userspace
DECLARE_WORK(workqueue, send_shutdown_message);

/*
 * Function to send a message to userspace using netlink sockets, we never use 
 * the SKB after getting it, so we just need to set the body of the message and
 * send it.
 */
static void send_shutdown_message(struct work_struct *work)
{
  struct sk_buff *skb;
	skb = alloc_skb(NLMSG_SPACE(MAX_MSG_SIZE), GFP_KERNEL);
	nlh = (struct nlmsghdr *)skb->data;
	nlh->nlmsg_len = NLMSG_SPACE(MAX_PAYLOAD);
	nlh->nlmsg_pid = 0; /* from kernel */
	nlh->nlmsg_flags = 0;
	strcpy(NLMSG_DATA(nlh), "Greeting from kernel!");
	/* sender is in group 1<<0 */
	NETLINK_CB(skb).groups = 1;
	NETLINK_CB(skb).pid = 0; /* from kernel */
	NETLINK_CB(skb).dst_pid = 0; /* multicast */
	/* to mcast group 1<<0 */
	NETLINK_CB(skb).dst_groups = 1;

	/*multicast the message to all listening processes*/
	netlink_broadcast(nl_sk, skb, 0, 1, GFP_KERNEL);
}

/*
 * ISR for both GPIOs. We just need to schedule the workqueue to send a message
 * to userspace. The linux kernel will take care of the rest.
 */
static irqreturn_t gpio_isr(int irq, void *dev_id)
{
	schedule_work(&workqueue);
}

void nl_data_ready(struct sock *sk, int len)
{
	wake_up_interruptible(sk->sleep);
}

/*
 * Module initialization function
 */
static int __init mod_init(void)
{
	// create our netlink socket
	nl_sk = netlink_kernel_create(&init_net, NETLINK_MSG_TYPE, 0, NULL,
				      NULL, NETLINK_MSG_TYPE);
	if (!nl_sk) {
		printk(KERN_ALERT "Error creating socket.\n");
		return -10;
	}

	// receive message from userspace
	// needed to get pid of userspace process
	skb = skb_recv_datagram(nl_sk, 0, 0, &err);
	nlh = (struct nlmsghdr *)skb->data;
	printk("%s: received netlink message payload:%s\n", __FUNCTION__,
	       NLMSG_DATA(nlh));
	pid = nlh->nlmsg_pid; /*pid of sending process */

	// initialize GPIOs
	ret = gpio_request(GPIO_1, "power failure");
	if (ret != 0) {
		printk(KERN_ALERT "Error requesting GPIO 1\n");
		return -10;
	}

	ret = gpio_request(GPIO_2, "power button");
	if (ret != 0) {
		printk(KERN_ALERT "Error requesting GPIO 2\n");
		return -10;
	}

	// set GPIO directions
	ret = gpio_direction_input(GPIO_1);
	if (ret != 0) {
		printk(KERN_ALERT "Error setting GPIO 1 direction\n");
		return -10;
	}

	ret = gpio_direction_input(GPIO_2);
	if (ret != 0) {
		printk(KERN_ALERT "Error setting GPIO 2 direction\n");
		return -10;
	}

	// export GPIOs
	ret = gpio_export(GPIO_1, false);
	if (ret != 0) {
		printk(KERN_ALERT "Error exporting GPIO 1\n");
		return -10;
	}

	ret = gpio_export(GPIO_2, false);
	if (ret != 0) {
		printk(KERN_ALERT "Error exporting GPIO 2\n");
		return -10;
	}

	// gpio to IRQ mapping
	int irqNum1 = gpio_to_irq(GPIO_1);
	int irqNum2 = gpio_to_irq(GPIO_2);

	// request IRQs
	ret = request_irq(irqNum1, (irq_handler_t)gpio_isr, IRQF_TRIGGER_RISING,
			  "power failure", NULL);
	if (ret != 0) {
		printk(KERN_ALERT "Error requesting IRQ 1\n");
		return -10;
	}

	ret = request_irq(irqNum2, (irq_handler_t)gpio_isr, IRQF_TRIGGER_RISING,
			  "power button", NULL);
	if (ret != 0) {
		printk(KERN_ALERT "Error requesting IRQ 2\n");
		return -10;
	}

	return 0;
}

/*
 * Module exit function
 */
static void __exit mod_exit(void)
{
	// free IRQs
	free_irq(irqNum1, NULL);
	free_irq(irqNum2, NULL);

	// unexport GPIOs
	gpio_unexport(GPIO_1);
	gpio_unexport(GPIO_2);

	// free GPIOs
	gpio_free(GPIO_1);
	gpio_free(GPIO_2);

	// destroy netlink socket
	netlink_kernel_release(nl_sk);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jacob Deinum");
MODULE_DESCRIPTION("Kernel module for requesting a shutdown");
