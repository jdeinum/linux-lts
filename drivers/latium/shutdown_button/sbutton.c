/*
 *  This module is responsible for shutting down the system when shutdown button
 *  is pressed.
 *
 *  Author: Jacob Deinum
 *  Date: 2023-03-08
 */

#define _POSIX_C_SOURCE

#include <linux/gpio.h> /* Needed for GPIO functions */
#include <linux/irq.h> /* Needed for Interrupt functions */
#include <linux/module.h> /* Needed for module functions */
#include <linux/init.h> /* Needed for the macros */
#include <linux/printk.h> /* Needed for pr_info() */
#include <linux/signal.h>
#include <linux/interrupt.h> /* We want an interrupt */
#include <linux/sched.h>

#define INTERRUPT_PIN 49

// interrupt handler for the shutdown pin
static irqreturn_t irq_handler(int irq, void *dev_id)
{
	// create the pid of the init process
	struct pid *pid_struct = find_get_pid(1);

	pr_info("Interrupt received from shutdown button, shutting down now!");
	kill_pid(pid_struct, SIGRTMIN + 4, 1);
	return IRQ_HANDLED;
}

static int __init ssd_init(void)
{
	int irq;
	pr_info("Initializing button shutdown module\n");

	// request gpio pin
	if (gpio_request(INTERRUPT_PIN, "button shutdown interrupt")) {
		pr_err("Failed to request interrupt pin");
		return -1;
	}

	// register interrupt
	irq = gpio_to_irq(INTERRUPT_PIN);
	if (request_irq(irq, (irq_handler_t)irq_handler, IRQF_TRIGGER_RISING,
			"shutdown button", NULL)) {
		pr_err("Failed to request interrupt");
		return -1;
	}
	pr_info("Interrupt registered");

	// modules must return 0 on successful load
	return 0;
}

static void __exit ssd_exit(void)
{
	pr_info("Initiating button shutdown handler\n");

	// unregister interrupt
	free_irq(gpio_to_irq(INTERRUPT_PIN), NULL);

	// free gpio pin
	gpio_free(INTERRUPT_PIN);
}

module_init(ssd_init);
module_exit(ssd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jacob Deinum");
MODULE_DESCRIPTION("Shutdown Using Button Press");
