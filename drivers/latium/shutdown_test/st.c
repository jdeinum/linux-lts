#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <systemd/sd-daemon.h>

static int __init my_module_init(void)
{
    int ret = systemd_shutdown(SD_HALT, true);
    if (ret < 0) {
        printk(KERN_ERR "Failed to initiate shutdown: %d\n", ret);
        return ret;
    }
    return 0;
}

static void __exit my_module_exit(void)
{
    printk(KERN_INFO "Module exited\n");
}

module_init(my_module_init);
module_exit(my_module_exit);
MODULE_LICENSE("GPL");
