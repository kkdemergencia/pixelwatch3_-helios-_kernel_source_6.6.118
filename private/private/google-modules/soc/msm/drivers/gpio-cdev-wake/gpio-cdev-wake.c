// SPDX-License-Identifier: GPL-2.0

/* Copyright 2026 Google LLC */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/anon_inodes.h>
#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <uapi/linux/gpio.h>
#define GPIO_V2_LINE_FLAG_EDGE_WAKE (_BITULL(13))

#define DEVICE_NAME "gpiochipwake0"
#define MAX_EVENTS 16

struct line_event_data {
	struct gpio_desc *desc;
	unsigned int offset;
	int irq;
	u64 requested_flags;
	spinlock_t lock;
	wait_queue_head_t wq;
	struct gpio_v2_line_event events[MAX_EVENTS];
	unsigned int head, tail;
	u32 seqno;
};

static dev_t gpiochipwake0_devt;
static struct cdev gpiochipwake0_cdev;
static struct class *gpiochipwake0_class;
static struct platform_device *pdev;

static irqreturn_t line_irq_handler(int irq, void *dev_id)
{
	struct line_event_data *led = dev_id;
	struct gpio_v2_line_event event;
	unsigned long flags;
	unsigned int slot;

	event.timestamp_ns = ktime_get_real_ns();

	if (led->requested_flags & GPIO_V2_LINE_FLAG_EDGE_RISING)
		event.id = GPIO_V2_LINE_EVENT_RISING_EDGE;
	else if (led->requested_flags & GPIO_V2_LINE_FLAG_EDGE_FALLING)
		event.id = GPIO_V2_LINE_EVENT_FALLING_EDGE;
	else
		return IRQ_NONE;

	event.offset = led->offset;

	spin_lock_irqsave(&led->lock, flags);
	event.seqno = led->seqno++;
	slot = led->head;
	if (((slot + 1) % MAX_EVENTS) != led->tail) {
		led->events[slot] = event;
		led->head = (slot + 1) % MAX_EVENTS;
	} else {
		pr_warn_ratelimited("event queue overflow for line %u\n",
				led->offset);
	}
	spin_unlock_irqrestore(&led->lock, flags);

	wake_up_interruptible(&led->wq);

	return IRQ_HANDLED;
}

static ssize_t line_read(struct file *filp, char __user *buf, size_t count,
			loff_t *f_pos)
{
	struct line_event_data *led = filp->private_data;
	struct gpio_v2_line_event event;
	unsigned long flags;
	size_t event_size = sizeof(event);
	int ret;

	if (count < event_size)
		return -EINVAL;

	ret = wait_event_interruptible(led->wq, (led->head != led->tail));
	if (ret < 0)
		return ret;

	spin_lock_irqsave(&led->lock, flags);
	if (led->head == led->tail) {
		spin_unlock_irqrestore(&led->lock, flags);
		return -EAGAIN;
	}
	event = led->events[led->tail];
	led->tail = (led->tail + 1) % MAX_EVENTS;
	spin_unlock_irqrestore(&led->lock, flags);

	if (copy_to_user(buf, &event, event_size))
		return -EFAULT;

	return event_size;
}

static __poll_t line_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct line_event_data *led = filp->private_data;
	__poll_t mask = 0;
	unsigned long flags;

	poll_wait(filp, &led->wq, wait);

	spin_lock_irqsave(&led->lock, flags);
	if (led->head != led->tail)
		mask |= EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&led->lock, flags);

	return mask;
}

static int line_release(struct inode *inode, struct file *filp)
{
	struct line_event_data *led = filp->private_data;

	if (led->irq > 0) {
		if (led->requested_flags & GPIO_V2_LINE_FLAG_EDGE_WAKE)
			disable_irq_wake(led->irq);
		free_irq(led->irq, led);
	}
	if (led->desc)
		gpiod_put(led->desc);
	kfree(led);

	return 0;
}

static const struct file_operations line_fops = {
	.owner = THIS_MODULE,
	.read = line_read,
	.poll = line_poll,
	.release = line_release,
};

static int chip_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int chip_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long chip_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	if (cmd != GPIO_V2_GET_LINE_IOCTL)
		return -ENOTTY;

	struct gpio_v2_line_request req;
	struct line_event_data *led = NULL;
	struct gpio_desc *desc = NULL;
	unsigned long irq_trig_flags = 0;
	int line_fd = -1;
	int gpio = -1;
	int irq;
	int ret;

	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;

	if (req.num_lines != 1) {
		dev_err(&pdev->dev, "multi-line requests unsupported\n");
		return -EINVAL;
	}

	if (!(req.config.flags & GPIO_V2_LINE_FLAG_INPUT)) {
		dev_err(&pdev->dev, "only input lines supported\n");
		return -EINVAL;
	}

	if (!(req.config.flags & GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN)) {
		dev_err(&pdev->dev, "only pull down supported\n");
		return -EINVAL;
	}

	if (req.config.flags & ~(GPIO_V2_LINE_FLAG_INPUT |
					GPIO_V2_LINE_FLAG_EDGE_RISING |
					GPIO_V2_LINE_FLAG_EDGE_FALLING |
					GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN |
					GPIO_V2_LINE_FLAG_EDGE_WAKE
			)) {
		dev_err(&pdev->dev, "unsupported GPIO V2 line flags: 0x%llx\n",
			req.config.flags);
		return -EINVAL;
	}

	if ((req.config.flags & GPIO_V2_LINE_FLAG_EDGE_RISING) &&
		(req.config.flags & GPIO_V2_LINE_FLAG_EDGE_FALLING)) {
		dev_err(&pdev->dev,
			"can't request both rising and falling edges\n");
		return -EINVAL;
	}

	desc = gpiod_get(&pdev->dev, req.consumer, GPIOD_IN);
	if (IS_ERR(desc)) {
		dev_err(&pdev->dev, "failed to get GPIO %s; error %ld\n",
			req.consumer, PTR_ERR(desc));
		return PTR_ERR(desc);
	}

	/* Legacy GPIO numbers include a per-gpiochip offset. We don't
	 * have access to that, but we know the lowest 8 bits contain
	 * the per-chip number.
	 */
	gpio = desc_to_gpio(desc) & 0xFFU;
	if (gpio != req.offsets[0]) {
		dev_err(&pdev->dev,
			"GPIO %s: number %d doesn't match offset %d\n",
			req.consumer, gpio, req.offsets[0]);
		ret = -EINVAL;
		goto err_put_desc;
	}

	/* We only accept !GPIO_V2_LINE_FLAG_ACTIVE_LOW (unset, active high) */
	if (gpiod_is_active_low(desc)) {
		/* gpiod_toggle_active_low(desc) isn't exported in 6.6 KMI */
		dev_err(&pdev->dev,
			"GPIO %s must be configured active high\n",
			req.consumer);
		ret = -EINVAL;
		goto err_put_desc;
	}

	led = kzalloc(sizeof(*led), GFP_KERNEL);
	if (!led) {
		ret = -ENOMEM;
		goto err_put_desc;
	}

	led->desc = desc;
	led->offset = req.offsets[0];
	led->requested_flags = req.config.flags;
	spin_lock_init(&led->lock);
	init_waitqueue_head(&led->wq);

	if (req.config.flags & GPIO_V2_LINE_FLAG_EDGE_RISING)
		irq_trig_flags |= IRQF_TRIGGER_RISING;
	if (req.config.flags & GPIO_V2_LINE_FLAG_EDGE_FALLING)
		irq_trig_flags |= IRQF_TRIGGER_FALLING;

	if (irq_trig_flags) {
		irq = gpiod_to_irq(desc);
		if (irq < 0) {
			dev_err(&pdev->dev,
				"failed to get IRQ for GPIO %s: %d\n",
				req.consumer, irq);
			ret = irq;
			goto err_free_led;
		}
		led->irq = irq;

		ret = request_threaded_irq(irq, NULL, line_irq_handler,
					irq_trig_flags | IRQF_ONESHOT,
					req.consumer, led);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"failed to request IRQ %d for GPIO %s: %d\n",
				irq, req.consumer, ret);
			led->irq = 0;
			goto err_free_led;
		}

		if (req.config.flags & GPIO_V2_LINE_FLAG_EDGE_WAKE) {
			ret = enable_irq_wake(irq);
			if (ret)
				goto err_free_irq;
		}
	}

	line_fd = anon_inode_getfd(DEVICE_NAME, &line_fops, led,
				O_RDONLY | O_CLOEXEC);
	if (line_fd < 0) {
		ret = line_fd;
		goto err_disable_wake;
	}

	req.fd = line_fd;
	if (copy_to_user((void __user *)arg, &req, sizeof(req))) {
		put_unused_fd(line_fd);
		return -EFAULT;
	}

	return 0;

err_disable_wake:
	if (led->irq > 0 && (req.config.flags & GPIO_V2_LINE_FLAG_EDGE_WAKE))
		disable_irq_wake(led->irq);
err_free_irq:
	if (led->irq > 0)
		free_irq(led->irq, led);
err_free_led:
	kfree(led);
err_put_desc:
	gpiod_put(desc);

	return ret;
}

#ifdef CONFIG_COMPAT
static long chip_ioctl_compat(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	return chip_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations chip_fops = {
	.owner = THIS_MODULE,
	.open = chip_open,
	.release = chip_release,
	.unlocked_ioctl = chip_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = chip_ioctl_compat,
#endif
};

static int gpiochipwake0_probe(struct platform_device *pdev_arg)
{
	int ret;
	struct device *dev;

	pdev = pdev_arg;

	ret = alloc_chrdev_region(&gpiochipwake0_devt, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to allocate char dev region: %d\n",
			ret);
		return ret;
	}

	gpiochipwake0_class = class_create(DEVICE_NAME);
	if (IS_ERR(gpiochipwake0_class)) {
		ret = PTR_ERR(gpiochipwake0_class);
		dev_err(&pdev->dev, "failed to create class: %d\n", ret);
		goto unreg_chrdev;
	}

	cdev_init(&gpiochipwake0_cdev, &chip_fops);
	gpiochipwake0_cdev.owner = THIS_MODULE;

	ret = cdev_add(&gpiochipwake0_cdev, gpiochipwake0_devt, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add cdev: %d\n", ret);
		goto dest_class;
	}

	dev = device_create(gpiochipwake0_class, &pdev->dev,
			gpiochipwake0_devt, NULL, DEVICE_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		dev_err(&pdev->dev, "failed to create device: %d\n", ret);
		goto del_cdev;
	}

	dev_info(&pdev->dev, "device created\n");

	return 0;

del_cdev:
	cdev_del(&gpiochipwake0_cdev);
dest_class:
	class_destroy(gpiochipwake0_class);
unreg_chrdev:
	unregister_chrdev_region(gpiochipwake0_devt, 1);

	return ret;
}

static int gpiochipwake0_remove(struct platform_device *pdev)
{
	device_destroy(gpiochipwake0_class, gpiochipwake0_devt);
	cdev_del(&gpiochipwake0_cdev);
	class_destroy(gpiochipwake0_class);
	unregister_chrdev_region(gpiochipwake0_devt, 1);
	dev_info(&pdev->dev, "device removed\n");

	return 0;
}

static const struct of_device_id gpiochipwake0_of_match[] = {
	{ .compatible = "google,gpiochipwake0" },
	{},
};
MODULE_DEVICE_TABLE(of, gpiochipwake0_of_match);

static struct platform_driver gpiochipwake0_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = gpiochipwake0_of_match,
	},
	.probe = gpiochipwake0_probe,
	.remove = gpiochipwake0_remove,
};

module_platform_driver(gpiochipwake0_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Limited GPIO V2 cdev driver supporting wake-from-suspend");
