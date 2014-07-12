/*
 * Rotation sensor module.
 *
 * (c) 2014 Christophe BLAESS <christophe.blaess@logilin.fr>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/version.h>

#include <asm/uaccess.h>

#define DEFAULT_GPIO_A        18  // Pin 12 on Raspberry Pi P1 connector
#define DEFAULT_GPIO_B        17  // Pin 11 on Raspberry Pi P1 connector
#define DEFAULT_COUNT_MAX   5000

//------------------- Module parameters -------------------------------------

static int gpio_a = DEFAULT_GPIO_A;
module_param(gpio_a, int, 0444);
MODULE_PARM_DESC(gpio_a, "Channel A GPIO.");

static int gpio_b = DEFAULT_GPIO_B;
module_param(gpio_b, int, 0444);
MODULE_PARM_DESC(gpio_b, "Channel B GPIO.");

static int count_max = DEFAULT_COUNT_MAX;
module_param(count_max, int, 0444);
MODULE_PARM_DESC(count_max, "Maximal counter value.");


// ------------------ Driver private data type ------------------------------

struct rotation_sensor_struct {
    long int       value;
    spinlock_t     spinlock;
} g_rotation_sensor;


// ------------------ Driver private methods -------------------------------

static ssize_t rotation_sensor_read(struct file * filp, char * __user buffer, size_t length, loff_t * offset)
{
	int lg;
	char kbuffer[64];
	unsigned long irqmsk;
	long int angle;

	angle = (g_rotation_sensor.value * 3600)/ count_max;
	spin_lock_irqsave(& (g_rotation_sensor.spinlock), irqmsk);
	snprintf(kbuffer, 64, "%ld.%ld\n", angle/10, angle%10);
	spin_unlock_irqrestore(& (g_rotation_sensor.spinlock), irqmsk);

	lg = strlen(kbuffer);

	lg -= (*offset);
	if (lg <= 0)
		return 0;

	if (lg > length)
		lg = length;

	if (copy_to_user(buffer, kbuffer + (*offset), lg) != 0)
		return -EFAULT;
	(*offset) += lg;
	return lg;
}


static ssize_t rotation_sensor_write(struct file * filp, const char * __user buffer, size_t length, loff_t * offset)
{
	long int value;
	char * kbuffer;
	unsigned long irqmsk;

	kbuffer = kmalloc(length, GFP_KERNEL);
	if (kbuffer == NULL)
		return -ENOMEM;
	if (copy_from_user(kbuffer, buffer, length) != 0) {
		kfree(kbuffer);
		return -EFAULT;
	}
	if (sscanf(kbuffer, "%ld", & value) != 1) {
		kfree(kbuffer);
		return -EINVAL;
	}
	kfree(kbuffer);
	
	spin_lock_irqsave(& (g_rotation_sensor.spinlock), irqmsk);
	g_rotation_sensor.value = value;
	spin_unlock_irqrestore(& (g_rotation_sensor.spinlock), irqmsk);
	return length;
}


static irqreturn_t gpio_a_handler(int irq, void * arg)
{
	struct rotation_sensor_struct * sensor = arg;

	spin_lock(& sensor->spinlock);
	if (gpio_get_value(gpio_b))
		sensor->value ++;
	else
		sensor->value --;
	while (sensor->value > count_max)
		sensor->value -= count_max;
	while (sensor->value < 0)
		sensor->value += count_max;

	spin_unlock(& sensor->spinlock);

    return IRQ_HANDLED;
}



// ------------------ Driver private global data ----------------------------

static struct file_operations rotation_sensor_fops = {
    .owner   =  THIS_MODULE,
    .read    =  rotation_sensor_read,
    .write   =  rotation_sensor_write,
};


static struct miscdevice rotation_sensor_driver = {
        .minor          = MISC_DYNAMIC_MINOR,
        .name           = THIS_MODULE->name,
        .fops           = & rotation_sensor_fops,
};


// ------------------ Driver init and exit methods --------------------------

static int __init rotation_sensor_init (void)
{
	int err;

	spin_lock_init(& (g_rotation_sensor.spinlock));
	g_rotation_sensor.value = 0;
	
	// Reserve GPIO A & B.
	err = gpio_request(gpio_a, THIS_MODULE->name);
	if (err != 0)
		return err;

	err = gpio_request(gpio_b, THIS_MODULE->name);
	if (err != 0) {
		gpio_free(gpio_a);
		return err;
	}

	// Set GPIO A & B as inputs.
	if ((gpio_direction_input(gpio_a) != 0)
	 || (gpio_direction_input(gpio_b) != 0)) {
		gpio_free(gpio_b);
		gpio_free(gpio_a);
		return err;
	}

	// Install IRQ handlers.
	err = request_irq(gpio_to_irq(gpio_a), gpio_a_handler,
	                  IRQF_SHARED | IRQF_TRIGGER_RISING,
	                  THIS_MODULE->name, & g_rotation_sensor);
	if (err != 0) {
		gpio_free(gpio_b);
		gpio_free(gpio_a);
		return err;
	}

	// Install user space char interface.
	err = misc_register(& rotation_sensor_driver);
	return err;
}


void __exit rotation_sensor_exit (void)
{
	misc_deregister(& rotation_sensor_driver);
	
	free_irq(gpio_to_irq(gpio_a), & g_rotation_sensor);
	gpio_free(gpio_b);
	gpio_free(gpio_a);
}


module_init(rotation_sensor_init);
module_exit(rotation_sensor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christophe Blaess <christophe.blaess@logilin.fr>");

