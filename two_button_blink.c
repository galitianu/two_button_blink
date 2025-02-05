#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>           // ioremap, iounmap
#include <linux/kthread.h>      // kthread_create, kthread_run
#include <linux/delay.h>        // msleep
#include <linux/jiffies.h>      // jiffies, HZ

//-----------------------------------
// Adjust this if you're on Pi 4 => 0xFE200000
//-----------------------------------
#define GPIO_BASE 0x3F200000
#define GPIO_LEN  0xB4

// Register offsets
#define GPFSEL0_OFFSET 0x00
#define GPFSEL1_OFFSET 0x04
#define GPFSEL2_OFFSET 0x08
#define GPSET0_OFFSET  0x1C
#define GPCLR0_OFFSET  0x28
#define GPLEV0_OFFSET  0x34
#define GPPUD_OFFSET   0x94
#define GPPUDCLK0_OFFSET 0x98

// We use GPIO18 for LED, GPIO23 for "increment", GPIO24 for "decrement"
#define GPIO_LED       18
#define GPIO_BTN_INC   23
#define GPIO_BTN_DEC   24

// Helper for FSEL (3 bits per pin)
#define GPIO_FSEL_SHIFT(pin)  (((pin) % 10) * 3)

//-----------------------------------
// Globals
//-----------------------------------
static void __iomem *gpio_base;
static struct task_struct *poll_thread;
static bool running = true;

// Blink frequency (start at 2 blinks/sec)
static int blink_freq = 2;

// Track button states for short-press detection
static int last_inc_state = 1;  // 1 = HIGH at rest (pull-up)
static int last_dec_state = 1;  
static unsigned long inc_press_jiffies = 0;
static unsigned long dec_press_jiffies = 0;

//-----------------------------------
// GPIO Helpers
//-----------------------------------
static inline void gpio_set_pin(int pin)
{
	iowrite32((1 << pin), gpio_base + GPSET0_OFFSET);
}

static inline void gpio_clear_pin(int pin)
{
	iowrite32((1 << pin), gpio_base + GPCLR0_OFFSET);
}

static inline int gpio_read_pin(int pin)
{
	unsigned int val = ioread32(gpio_base + GPLEV0_OFFSET);
	return (val & (1 << pin)) ? 1 : 0;
}

//-----------------------------------
// Thread to poll buttons & blink LED
//-----------------------------------
static int poll_thread_fn(void *data)
{
	bool led_on = false;

	while (!kthread_should_stop() && running) {

		// --------------------------
		// 1) Check "Increment" button
		// --------------------------
		{
			int curr_inc = gpio_read_pin(GPIO_BTN_INC);
			if (curr_inc != last_inc_state) {
				// Pressed
				if (curr_inc == 0) {
					inc_press_jiffies = jiffies;
				} else {
					// Released => short press
					unsigned long diff = jiffies - inc_press_jiffies;
					if (diff < HZ) {  // < 1 sec => valid short press
						blink_freq+=5;
						pr_info("Increment button pressed: freq => %d\n", blink_freq);
					}
				}
				last_inc_state = curr_inc;
			}
		}

		// --------------------------
		// 2) Check "Decrement" button
		// --------------------------
		{
			int curr_dec = gpio_read_pin(GPIO_BTN_DEC);
			if (curr_dec != last_dec_state) {
				// Pressed
				if (curr_dec == 0) {
					dec_press_jiffies = jiffies;
				} else {
					// Released => short press
					unsigned long diff = jiffies - dec_press_jiffies;
					if (diff < HZ) {
						blink_freq-=5;
						if (blink_freq < 1)
							blink_freq = 1;
						pr_info("Decrement button pressed: freq => %d\n", blink_freq);
					}
				}
				last_dec_state = curr_dec;
			}
		}

		// --------------------------
		// 3) Blink the LED
		// --------------------------
		if (led_on) {
			gpio_clear_pin(GPIO_LED);
			led_on = false;
		} else {
			gpio_set_pin(GPIO_LED);
			led_on = true;
		}

		// half cycle = (1000 / freq) / 2
		if (blink_freq < 1)
			blink_freq = 1;
		msleep((1000 / blink_freq) / 2);
	}

	return 0;
}

//-----------------------------------
// Module Init
//-----------------------------------
static int __init two_button_init(void)
{
	unsigned int val;

	pr_info("two_button_blink: loading...\n");

	// 1) ioremap
	gpio_base = ioremap(GPIO_BASE, GPIO_LEN);
	if (!gpio_base) {
		pr_err("Failed to ioremap\n");
		return -ENOMEM;
	}

	// 2) Configure GPIO18 as output
	{
		unsigned int *gpfsel1 = (unsigned int *)(gpio_base + GPFSEL1_OFFSET);
		val = ioread32(gpfsel1);
		// Clear bits for GPIO18
		val &= ~(0x7 << GPIO_FSEL_SHIFT(GPIO_LED));
		// Set to 001 => output
		val |= (0x1 << GPIO_FSEL_SHIFT(GPIO_LED));
		iowrite32(val, gpfsel1);
	}

	// 3) Configure GPIO23 & GPIO24 as inputs, enable pull-ups
	{
		// For GPIO23 => GPFSEL2
		unsigned int *gpfsel2 = (unsigned int *)(gpio_base + GPFSEL2_OFFSET);
		val = ioread32(gpfsel2);
		val &= ~(0x7 << GPIO_FSEL_SHIFT(GPIO_BTN_INC)); // clear bits => input
		val &= ~(0x7 << GPIO_FSEL_SHIFT(GPIO_BTN_DEC)); // same for GPIO24
		iowrite32(val, gpfsel2);

		// GPPUD => 2 => pull-up
		iowrite32(0x2, gpio_base + GPPUD_OFFSET);
		ndelay(150);
		// Apply pull-up to both pins
		iowrite32((1 << GPIO_BTN_INC) | (1 << GPIO_BTN_DEC), gpio_base + GPPUDCLK0_OFFSET);
		ndelay(150);
		// remove config
		iowrite32(0, gpio_base + GPPUD_OFFSET);
		iowrite32(0, gpio_base + GPPUDCLK0_OFFSET);
	}

	// 4) Create polling thread
	running = true;
	poll_thread = kthread_run(poll_thread_fn, NULL, "poll_thread");
	if (IS_ERR(poll_thread)) {
		pr_err("Failed to create poll_thread\n");
		iounmap(gpio_base);
		return PTR_ERR(poll_thread);
	}

	pr_info("two_button_blink: loaded (freq=%d). Press buttons on GPIO23/24.\n", blink_freq);
	return 0;
}

//-----------------------------------
// Module Exit
//-----------------------------------
static void __exit two_button_exit(void)
{
	running = false;
	if (poll_thread) {
		kthread_stop(poll_thread);
	}
    gpio_clear_pin(GPIO_LED);
	if (gpio_base) {
		iounmap(gpio_base);
		gpio_base = NULL;
	}

	pr_info("two_button_blink: unloaded.\n");
}

module_init(two_button_init);
module_exit(two_button_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrei Galitianu");
MODULE_DESCRIPTION("Two-button LED frequency driver (polling) on Raspberry Pi");
