#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include "uart16550.h"
#include "uart16550_hw.h"

MODULE_DESCRIPTION("Uart16550 driver");
MODULE_LICENSE("GPL");

#ifdef __DEBUG
        #define dprintk(fmt, ...)     printk(KERN_DEBUG "%s:%d " fmt, \
                                        __FILE__, __LINE__, ##__VA_ARGS__)
#else
        #define dprintk(fmt, ...)     do { } while (0)
#endif

static struct class *uart16550_class = NULL;
/*
 * Module parameters
 */
static int major = 42;
static int behavior = 0x3;
module_param(major, int, 0);
module_param(behavior, int, 0);

/*
 * Flags used in module init/clean_up
 */
static int have_com1, have_com2, dev_count, first_minor;

/*
 * File operation structure
 */
static int uart16550_open(struct inode *inode, struct file *filp);
static int uart16550_release(struct inode *inode, struct file *filp);
static ssize_t uart16550_read(struct file *filp, char __user *user_buffer,
        size_t size, loff_t *offset);
static ssize_t uart16550_write(struct file *filp, const char __user *user_buffer,
        size_t size, loff_t *offset);

static const struct file_operations uart16550_fops = {
        .owner = THIS_MODULE,
        .open = uart16550_open,
        .read = uart16550_read,
        .write = uart16550_write,
        .release = uart16550_release
};

/*
 * Device specific structure
 */
struct uart16550_dev {
        struct cdev cdev;
        DECLARE_KFIFO(write_buf, char, FIFO_SIZE);
        DECLARE_KFIFO(read_buf, char, FIFO_SIZE);
        wait_queue_head_t read_q;
        wait_queue_head_t write_q;
};

static struct uart16550_dev uart16550_dev_COM1, uart16550_dev_COM2;

/*
 * Setup cdev
 */
static int uart16550_setup_cdev(struct uart16550_dev *dev, int minor)
{
        int err;
        dev_t dev_num = MKDEV(major, minor);

        cdev_init(&dev->cdev, &uart16550_fops);
        dev->cdev.owner = THIS_MODULE;

        err = cdev_add(&dev->cdev, dev_num, 1);
        if (err) {
                dprintk("Fail add cdev, device minor number: %d\n", minor);
                return err;
        }

        return 0;
}

/*
 * File operations
 */
static int uart16550_open(struct inode *inode, struct file *filp)
{
        /* identify device, store device information with private_data */
        struct uart16550_dev *dev;
        dev = container_of(inode->i_cdev, struct uart16550_dev, cdev);
        filp->private_data = dev;

        dprintk("open() device: COM%d\n", iminor(inode) + 1);
        return 0;
}

static int uart16550_release(struct inode *inode, struct file *filp)
{
        dprintk("release()\n");
        return 0;
}

static ssize_t uart16550_read(struct file *filp, char __user *user_buffer,
        size_t size, loff_t *offset)
{
        int bytes_copied = 0;
        struct uart16550_dev *dev = filp->private_data;

        /* Reject bad user address at first place */
        if (!access_ok(VERIFY_WRITE, user_buffer, size))
                return -EFAULT;

        wait_event_interruptible(dev->read_q, !kfifo_is_empty(&dev->read_buf));
        if (kfifo_to_user(&dev->read_buf, user_buffer, size, &bytes_copied))
                return -EFAULT;

        dprintk("read() %d bytes\n", bytes_copied);
        return bytes_copied;
}

#ifdef jkjl______
static ssize_t uart16550_write(struct file *filp, const char __user *user_buffer,
        size_t size, loff_t *offset)
{
        int bytes_copied;
        struct uart16550_dev *dev = filp->private_data;

        /* Reject bad user address at first place */
        if (!access_ok(VERIFY_READ, user_buffer, size))
                return -EFAULT;

        if (!kfifo_is_full(&dev->write_buf)) {
                if (kfifo_from_user(&dev->write_buf, user_buffer, size, &bytes_copied))
                        return -EFAULT;
        } else {
                return -ENOBUFS;
        }
        dprintk("write() %d bytes\n", bytes_copied);
        return bytes_copied;
}
#endif

/* TODO: unlocked_ioctl */

static ssize_t uart16550_write(struct file *filp, const char __user *user_buffer,
        size_t size, loff_t *offset)
{
        int bytes_copied;
        u32 device_port;
        struct uart16550_dev *dev = filp->private_data;

        if (dev == &uart16550_dev_COM1) {
                device_port = COM1_BASEPORT;
        } else {
                device_port = COM2_BASEPORT;
        }
        /*
         * TODO: Write the code that takes the data provided by the
         *      user from userspace and stores it in the kernel
         *      device outgoing buffer.
         * TODO: Populate bytes_copied with the number of bytes
         *      that fit in the outgoing buffer.
         */
        /* Reject bad user address at first place */
        if (!access_ok(VERIFY_READ, user_buffer, size))
                return -EFAULT;

        wait_event_interruptible(dev->write_q, !kfifo_is_full(&dev->write_buf));
        if (kfifo_from_user(&dev->write_buf, user_buffer, size, &bytes_copied))
                return -EFAULT;

        dprintk("write() %d bytes\n", bytes_copied);

        uart16550_hw_force_interrupt_reemit(device_port);

        return bytes_copied;
}

irqreturn_t interrupt_handler(int irq_no, void *data)
{
        int device_status, device_status_tmp;
        u32 device_port;
        /*
         * TODO: Write the code that handles a hardware interrupt.
         * TODO: Populate device_port with the port of the correct device.
         */
        struct uart16550_dev *dev = data;

        if (irq_no == COM1_IRQ) {
                device_port = COM1_BASEPORT;
        } else {
                device_port = COM2_BASEPORT;
        }

        device_status = uart16550_hw_get_device_status(device_port);
        device_status_tmp = device_status;

        while (uart16550_hw_device_can_send(device_status)) {
                u8 byte_value;
                /*
                 * TODO: Populate byte_value with the next value
                 *      from the kernel device outgoing buffer.
                 * NOTE: If the outgoing buffer is empty, the interrupt
                 *       will not occur again. When data becomes available,
                 *       the driver must either:
                 *   a) force the hardware to reissue the interrupt.
                 *      OR
                 *   b) send the data separately.
                 */
                if (!kfifo_get(&dev->write_buf, &byte_value))
                        goto ret;
                uart16550_hw_write_to_device(device_port, byte_value);
                device_status = uart16550_hw_get_device_status(device_port);
        }
        if (uart16550_hw_device_can_send(device_status_tmp))
                wake_up_interruptible(&dev->write_q);

        while (uart16550_hw_device_has_data(device_status)) {
                u8 byte_value;
                byte_value = uart16550_hw_read_from_device(device_port);
                /*
                 * TODO: Store the read byte_value in the kernel device
                 *      incoming buffer.
                 */
                kfifo_put(&dev->read_buf, byte_value);
                device_status = uart16550_hw_get_device_status(device_port);
        }
        if (uart16550_hw_device_has_data(device_status_tmp))
                wake_up_interruptible(&dev->read_q);

ret:
        return IRQ_HANDLED;
}

static int uart16550_init(void)
{
        int err;
        struct device *dev_ret;
        /*
         * Parsing module parameters
         */
        switch (behavior) {
                case 0x1 :
                        have_com1 = 1;
                        have_com2 = 0;
                        dev_count = 1;
                        first_minor = 0; 
                        break;
                case 0x2 :
                        have_com1 = 0;
                        have_com2 = 1;
                        dev_count = 1;
                        first_minor = 1; 
                        break;
                case 0x3 :
                        have_com1 = 1;
                        have_com2 = 1;
                        dev_count = 2;
                        first_minor = 0; 
                        break;
                default :
                        err = -EINVAL;
                        dprintk("Bad module parameters\n");
                        goto nothing_to_undo;
        }

        if (major >= 512) {
                err = -EINVAL;
                dprintk("Bad module parameters\n");
                goto nothing_to_undo;
        }

        /*
         * Register character device numbers
         */
        err = register_chrdev_region(MKDEV(major, first_minor), dev_count,
                "uart16550");
        if (err) {
                dprintk("Fail registering chrdev region\n");
                goto nothing_to_undo; 
        }

        /*
         * TODO: Write driver initialization code here.
         * TODO: have_com1 & have_com2 need to be set according to the
         *      module parameters.
         * TODO: Check return values of functions used. Fail gracefully.
         */

        /*
         * Setup a sysfs class & device to make /dev/com1 & /dev/com2 appear.
         */
        uart16550_class = class_create(THIS_MODULE, "uart16550");
        if (IS_ERR(uart16550_class)) {
                dprintk("Fail creating sysfs class\n");
                err = PTR_ERR(uart16550_class);
                goto undo_reg_chrdev_region;
        }

        if (have_com1) {
                /* Create the sysfs info for /dev/com1 */
                dev_ret = device_create(uart16550_class, NULL, MKDEV(major, 0),
                        NULL, "com1");
                if (IS_ERR(dev_ret)) {
                        dprintk("Fail device_create COM1\n");
                        err = PTR_ERR(dev_ret);
                        goto undo_sysfs_class_create;
                }

                /* Init buffers of COM1 */
                INIT_KFIFO(uart16550_dev_COM1.write_buf);
                INIT_KFIFO(uart16550_dev_COM1.read_buf);

                /* Wait queue */
                init_waitqueue_head(&uart16550_dev_COM1.write_q);
                init_waitqueue_head(&uart16550_dev_COM1.read_q);

                /* Register COM1 to the kernel */ 
                err = uart16550_setup_cdev(&uart16550_dev_COM1, 0);
                if (err) {
                        dprintk("Fail registering COM1 to kernel");
                        goto undo_dev_create_and_buf_init_COM1;
                }

                /* Register handler for IRQ4 */
                err = request_irq(COM1_IRQ, interrupt_handler, IRQF_SHARED,
                        THIS_MODULE->name, &uart16550_dev_COM1);
                if (err) {
                        dprintk("Fail registering IRQ4 handler\n");
                        goto undo_dev_reg_COM1;
                }

                /* Setup the hardware device for COM1 */
                err = uart16550_hw_setup_device(COM1_BASEPORT, THIS_MODULE->name);
                if (err) {
                        dprintk("Fail setting up hw device COM1\n");
                        goto undo_handler_reg_COM1; 
                }
        }
        if (have_com2) {
                /* Create the sysfs info for /dev/com2 */
                dev_ret = device_create(uart16550_class, NULL, MKDEV(major, 1),
                        NULL, "com2");
                if (IS_ERR(dev_ret)) {
                        dprintk("Fail device_create COM2\n");
                        err = PTR_ERR(dev_ret);
                        goto undo_hw_setup_COM1;
                }

                /* Init buffers of COM2 */
                INIT_KFIFO(uart16550_dev_COM2.write_buf);
                INIT_KFIFO(uart16550_dev_COM2.read_buf);

                /* Wait queue */
                init_waitqueue_head(&uart16550_dev_COM2.write_q);
                init_waitqueue_head(&uart16550_dev_COM2.read_q);

                /* Register COM2 to the kernel */ 
                err = uart16550_setup_cdev(&uart16550_dev_COM2, 1);
                if (err) {
                        dprintk("Fail registering COM2 to kernel");
                        goto undo_dev_create_and_buf_init_COM2;
                }

                /* Register handler for IRQ3 */
                err = request_irq(COM2_IRQ, interrupt_handler, IRQF_SHARED,
                        THIS_MODULE->name, &uart16550_dev_COM2);
                if (err) {
                        dprintk("Fail registering IRQ3 handler\n");
                        goto undo_dev_reg_COM2;
                }

                /* Setup the hardware device for COM2 */
                err = uart16550_hw_setup_device(COM2_BASEPORT, THIS_MODULE->name);
                if (err) {
                        dprintk("Fail setting up hw device COM2\n");
                        goto undo_handler_reg_COM2; 
                }
        }

        /*
         * Success
         */
        return 0;

        /*
         * Error handling
         * Undo things
         */
undo_handler_reg_COM2:
        if (have_com2)
                free_irq(COM2_IRQ, &uart16550_dev_COM2);
undo_dev_reg_COM2:
        if (have_com2)
                cdev_del(&uart16550_dev_COM2.cdev);
undo_dev_create_and_buf_init_COM2:
        if (have_com2) {
                kfifo_free(&uart16550_dev_COM2.read_buf); 
                kfifo_free(&uart16550_dev_COM2.write_buf); 
                device_destroy(uart16550_class, MKDEV(major, 1));       
        }

undo_hw_setup_COM1:
        if (have_com1)
                uart16550_hw_cleanup_device(COM1_BASEPORT);       
undo_handler_reg_COM1:
        if (have_com1)
                free_irq(COM1_IRQ, &uart16550_dev_COM1);
undo_dev_reg_COM1:
        if (have_com1)
                cdev_del(&uart16550_dev_COM1.cdev);
undo_dev_create_and_buf_init_COM1:
        if (have_com1) {
                kfifo_free(&uart16550_dev_COM1.read_buf); 
                kfifo_free(&uart16550_dev_COM1.write_buf); 
                device_destroy(uart16550_class, MKDEV(major, 0));       
        }

undo_sysfs_class_create:
        class_destroy(uart16550_class);
undo_reg_chrdev_region:      
        unregister_chrdev_region(MKDEV(major, first_minor), dev_count);
nothing_to_undo:
        return err;
}

static void uart16550_cleanup(void)
{
        /*
         * TODO: Write driver cleanup code here.
         * TODO: have_com1 & have_com2 need to be set according to the
         *      module parameters.
         */
        if (have_com1) {
                /* Reset the hardware device for COM1 */
                uart16550_hw_cleanup_device(COM1_BASEPORT);
                /* Unregister interrupt handler for IRQ4 */
                free_irq(COM1_IRQ, &uart16550_dev_COM1);
                /* Unregister COM1 from kernel */
                cdev_del(&uart16550_dev_COM1.cdev);
                /* Free buffers */
                kfifo_free(&uart16550_dev_COM1.read_buf); 
                kfifo_free(&uart16550_dev_COM1.write_buf); 
                /* Remove the sysfs info for /dev/com1 */
                device_destroy(uart16550_class, MKDEV(major, 0));
        }
        if (have_com2) {
                /* Reset the hardware device for COM2 */
                uart16550_hw_cleanup_device(COM2_BASEPORT);
                /* Unregister interrupt handler for IRQ3 */
                free_irq(COM2_IRQ, &uart16550_dev_COM2);
                /* Unregister COM2 from kernel */
                cdev_del(&uart16550_dev_COM2.cdev);
                /* Free buffers */
                kfifo_free(&uart16550_dev_COM2.read_buf); 
                kfifo_free(&uart16550_dev_COM2.write_buf); 
                /* Remove the sysfs info for /dev/com2 */
                device_destroy(uart16550_class, MKDEV(major, 1));
        }

        /*
         * Cleanup the sysfs device class.
         */
        class_destroy(uart16550_class);

        /*
         * unregister character device numbers
         */
        unregister_chrdev_region(MKDEV(major, first_minor), dev_count);
}

module_init(uart16550_init);
module_exit(uart16550_cleanup);
