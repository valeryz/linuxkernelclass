/*
 * A virtual network device
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/compiler.h>

#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#ifndef CONFIG_PROC_FS
#error Enable procfs support in kernel
#endif

#define MY_MOD_NAME "netdev"


struct net_device *nd;

#ifdef DEBUG
static unsigned int debug_level = 0;
module_param(debug_level, uint, S_IRUGO|S_IWUSR);

#define DBG(level, kern_level, fmt, ...)				 \
	do {								 \
		if (level <= debug_level) {				 \
			printk(kern_level (MY_MOD_NAME "[%s:%u]: ") fmt, \
			       __func__, __LINE__,			 \
			       ## __VA_ARGS__);				 \
		}							 \
	} while (0)
#else
#define DBG(...)
#endif

int netdev_open(struct net_device *dev)
{
	/* set mac address */
	static const char mymacaddr[ETH_ALEN] = "AABBCC";

	/* memcpy(dev->dev_addr, mymacaddr, ETH_ALEN); */

	netif_start_queue(dev);

	return 0;
}

int netdev_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

netdev_tx_t netdev_start_xmit(struct sk_buff *skb,
			      struct net_device *dev)
{
	/* TODO implement */
	if (skb->protocol == htons(ETH_P_ARP)) {
		DBG(0, KERN_INFO, "Got an ARP packet");
		/* get the ARP IP Address */

	}
	/* rest packets are siltently dropped */
	return NETDEV_TX_OK;
}

static const struct net_device_ops netdev_ops = {
	.ndo_open = &netdev_open,
	.ndo_stop = &netdev_stop,
	.ndo_start_xmit = &netdev_start_xmit,
};

static int __init create_net_device(void)
{
	int res;

	nd = alloc_netdev(0, "netdev%d", ether_setup);
	if (nd == NULL)
		return -ENOMEM;

	nd->netdev_ops = &netdev_ops;

	res = register_netdev(nd);
	if (res < 0)
		return res;

	return 0;
}

static int __init netdev_init(void)
{
	if (unlikely(create_net_device()))
		return -1;

	DBG(2, KERN_INFO, "netdev loaded\n");
	return (0);
}

static void __exit netdev_exit(void)
{
	unregister_netdev(nd);
	free_netdev(nd);

	DBG(2, KERN_INFO, "netdev unloaded\n");
}

module_init(netdev_init);
module_exit(netdev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Valeriy Zamarayev <valeriy.zamarayev@gmail.com>");
MODULE_DESCRIPTION("Virtual Network Device Module");
