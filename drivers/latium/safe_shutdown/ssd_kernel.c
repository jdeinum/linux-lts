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
#define GPIO_1 100 // GPIO number for the first GPIO
#define GPIO_2 101 // GPIO number for the second GPIO

struct sock *nl_sk; // netlink socket
struct nlmsghdr *nlh; // netlink message header
int pid; // pid of the process to send messages to
int ret; // return value for GPIO functions

// workqueue for sending messages to userspace
static struct workqueue_struct *workqueue;
static struct delayed_work work;

/*
 * Function to send a message to userspace using netlink sockets. We set the
 * payload of the message and send it.
 */
static void send_shutdown_message(struct work_struct *work)
{
	struct sk_buff *skb;
	skb = alloc_skb(NLMSG_SPACE(MAX_MSG_SIZE), GFP_KERNEL);
	nlh = nlmsg_put(skb, 0, 0, NETLINK_MSG_TYPE, MAX_MSG_SIZE, 0);
	nlh->nlmsg_pid = pid; /* PID of the client process */
	nlh->nlmsg_flags = 0;
	strcpy(nlmsg_data(nlh), "Shutdown");

	/* Unicast the message to the client process */
	netlink_unicast(nl_sk, skb, pid, MSG_DONTWAIT);
}

/*
 * ISR for both GPIOs. We just need to schedule the workqueue to send a message
 * to userspace. The Linux kernel will take care of the rest.
 */
static irqreturn_t gpio_isr(int irq, void *dev_id)
{
	schedule_delayed_work(&work, 0);
	return IRQ_HANDLED;
}

/*
 * Module initialization function
 */
static int __init mod_init(void)
{
	// Create our netlink socket
	struct netlink_kernel_cfg cfg = {};
	nl_sk = netlink_kernel_create(&init_net, NETLINK_MSG_TYPE, &cfg);
	if (!nl_sk) {
		pr_err("Error creating socket.\n");
		return -ENOMEM;
	}

	// Receive message from userspace to get the PID of the client process
	struct sk_buff *skb;
	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb) {
		pr_err("Failed to allocate skb\n");
		netlink_kernel_release(nl_sk);
		return -ENOMEM;
	}

	nlh = nlmsg_put(skb, 0, 0, NETLINK_MSG_TYPE, NLMSG_DEFAULT_SIZE, 0);
	if (!nlh) {
		pr_err("Failed to put nlh\n");
		nlmsg_free(skb);
		netlink_kernel_release(nl_sk);
		return -ENOMEM;
	}

	nlh->nlmsg_flags = 0;
	nlh->nlmsg_len = NLMSG_LENGTH(0);
	NETLINK_CB(skb).portid = 0; // Port ID from kernel
	NETLINK_CB(skb).dst_group = 0; // Multicast group
	netlink_broadcast(nl_sk, skb, 0, 1, GFP_KERNEL);

	// Extract the PID of the client process
	pid = NETLINK_CREDS(skb)->pid;
	pr_info("Received netlink message from PID: %d\n", pid);
	kfree_skb(skb);

	// Initialize GPIOs
	ret = gpio_request(GPIO_1, "power failure");
	if (ret != 0) {
		pr_err("Error requesting GPIO 1\n");
		netlink_kernel_release(nl_sk);
		return -EINVAL;
	}

	ret = gpio_request(GPIO_2, "power button");
	if (ret != 0) {
		pr_err("Error requesting GPIO 2\n");
		gpio_free(GPIO_1);
		netlink_kernel_release(nl_sk);
		return -EINVAL;
	}

	// Set GPIO directions
	ret = gpio_direction_input(GPIO_1);
	if (ret != 0) {
		pr_err("Error setting GPIO 1 direction\n");
		gpio_free(GPIO_1);
		gpio_free(GPIO_2);
		netlink_kernel_release(nl_sk);
		return -EINVAL;
	}

	ret = gpio_direction_input(GPIO_2);
	if (ret != 0) {
		pr_err("Error setting GPIO 2 direction\n");
		gpio_free(GPIO_1);
		gpio_free(GPIO_2);
		netlink_kernel_release(nl_sk);
		return -EINVAL;
	}

	// Export GPIOs
	ret = gpio_export(GPIO_1, false);
	if (ret != 0) {
		pr_err("Error exporting GPIO 1\n");
		gpio_free(GPIO_1);
		gpio_free(GPIO_2);
		netlink_kernel_release(nl_sk);
		return -EINVAL;
	}

	ret = gpio_export(GPIO_2, false);
	if (ret != 0) {
		pr_err("Error exporting GPIO 2\n");
		gpio_free(GPIO_1);
		gpio_free(GPIO_2);
		netlink_kernel_release(nl_sk);
		return -EINVAL;
	}

	// GPIO to IRQ mapping
	int irqNum1 = gpio_to_irq(GPIO_1);
	int irqNum2 = gpio_to_irq(GPIO_2);

	// Request IRQs
	ret = request_irq(irqNum1, gpio_isr, IRQF_TRIGGER_RISING,
			  "power failure", NULL);
	if (ret != 0) {
		pr_err("Error requesting IRQ 1\n");
		gpio_free(GPIO_1);
		gpio_free(GPIO_2);
		netlink_kernel_release(nl_sk);
		return -EINVAL;
	}

	ret = request_irq(irqNum2, gpio_isr, IRQF_TRIGGER_RISING,
			  "power button", NULL);
	if (ret != 0) {
		pr_err("Error requesting IRQ 2\n");
		gpio_free(GPIO_1);
		gpio_free(GPIO_2);
		free_irq(irqNum1, NULL);
		netlink_kernel_release(nl_sk);
		return -EINVAL;
	}

	// Create workqueue for sending messages
	workqueue = create_singlethread_workqueue("gpio_workqueue");
	if (!workqueue) {
		pr_err("Failed to create workqueue\n");
		gpio_free(GPIO_1);
		gpio_free(GPIO_2);
		free_irq(irqNum1, NULL);
		free_irq(irqNum2, NULL);
		netlink_kernel_release(nl_sk);
		return -ENOMEM;
	}

	// Initialize work structure
	INIT_DELAYED_WORK(&work, send_shutdown_message);

	return 0;
}

/*
 * Module exit function
 */
static void __exit mod_exit(void)
{
	// Destroy workqueue
	if (workqueue) {
		flush_workqueue(workqueue);
		destroy_workqueue(workqueue);
	}

	// Free IRQs
	free_irq(gpio_to_irq(GPIO_1), NULL);
	free_irq(gpio_to_irq(GPIO_2), NULL);

	// Unexport GPIOs
	gpio_unexport(GPIO_1);
	gpio_unexport(GPIO_2);

	// Free GPIOs
	gpio_free(GPIO_1);
	gpio_free(GPIO_2);

	// Destroy netlink socket
	netlink_kernel_release(nl_sk);
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Kernel module for sending GPIO events via Netlink");
