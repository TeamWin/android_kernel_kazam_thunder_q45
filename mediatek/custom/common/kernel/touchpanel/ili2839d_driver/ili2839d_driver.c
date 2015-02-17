 /*
	 Copyright (c) 2010 by ilitek Technology.
	 All rights reserved.

	 ilitek I2C touch screen driver for Android platform

	 Author:	  Steward Fu
	 Maintain:Michael Hsu 
	 Version: 1
	 History:
		 2010/10/26 Firstly released
		 2010/10/28 Combine both i2c and hid function together
		 2010/11/02 Support interrupt trigger for I2C interface
		 2010/11/10 Rearrange code and add new IOCTL command
		 2010/11/23 Support dynamic to change I2C address
		 2010/12/21 Support resume and suspend functions
		 2010/12/23 Fix synchronous problem when application and driver work at the same time
		 2010/12/28 Add erasing background before calibrating touch panel
		 2011/01/13 Rearrange code and add interrupt with polling method
		 2011/01/14 Add retry mechanism
		 2011/01/17 Support multi-point touch
		 2011/01/21 Support early suspend function
		 2011/02/14 Support key button function
		 2011/02/18 Rearrange code
		 2011/03/21 Fix counld not report first point
		 2011/03/25 Support linux 2.36.x 
		 2011/05/31 Added "echo dbg > /dev/ilitek_ctrl" to enable debug message
				    Added "echo info > /dev/ilitek_ctrl" to show tp informaiton
				    Added VIRTUAL_KEY_PAD to enable virtual key pad
				    Added CLOCK_INTERRUPT to change interrupt from Level to Edge
				    Changed report behavior from Interrupt to Interrupt with Polling
				    Added disable irq when doing firmware upgrade via APK, it needs to use APK_1.4.9
		 2011/06/21 Avoid button is pressed when press AA
		 2011/08/03 Added ilitek_i2c_calibration function
		 2011/08/18 Fixed multi-point tracking id
				    Added ROTATE_FLAG to change x-->y, y-->x
				    Fixed when draw line from non-AA to AA, the line will not be appeared on screen.
		 2011/09/29 Added Stop Polling in Interrupt mode
				    Fixed Multi-Touch return value
				    Added release last point
		 2011/10/26 Fixed ROTATE bug
				    Added release key button when finger up.
				    Added ilitek_i2c_calibration_status for read calibration status
		 2011/11/09 Fixed release last point issue
				    enable irq when i2c error.
		 2011/11/28 implement protocol 2.1.
		 2012/02/10 Added muti_touch key.
				    'a5'5b'a4'4af1'82'a2u-32738?u31720?u22841?
		 2012/04/02 'f8'c2'f8'50 input_report_key('f9'67'f1'4fandroid 4.0 event inputf2'd4f0'b0'b5'ad'd7f3'fd)
		
*/
#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>

#include <cust_eint.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#include <linux/wakelock.h>
#endif
#include "tpd.h"
#include "cust_gpio_usage.h"

#include <mach/mt_pm_ldo.h>
#include <mach/mt_typedefs.h>
#include <mach/mt_boot.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include "cust_gpio_usage.h"    

#include <linux/dma-mapping.h>    
//#define ROTATE_FLAG
#define DRIVER_VERSION "aimvF"
#define CUST_EINT_TOUCH_PANEL_DEBOUNCE_EN      CUST_EINT_DEBOUNCE_DISABLE
#define CUST_EINT_TOUCH_PANEL_SENSITIVE        CUST_EINT_EDGE_SENSITIVE
#define CUST_EINT_TOUCH_PANEL_POLARITY         CUST_EINT_POLARITY_LOW
#define CUST_EINT_POLARITY_LOW              0
#define CUST_EINT_DEBOUNCE_DISABLE          0
#define CUST_EINT_EDGE_SENSITIVE            0
int touch_key_hold_press = 0;
int touch_key_code[] = {KEY_MENU,KEY_HOMEPAGE,KEY_BACK,KEY_VOLUMEDOWN,KEY_VOLUMEUP};
int touch_key_press[] = {0, 0, 0, 0, 0};
unsigned long touch_time=0;
unsigned char CTPM_FW[]={
//#include "ilitek.ili"
};
#include "ili2839d_driver.h"
#define VIRTUAL_KEY_PAD
#define VIRTUAL_FUN_1	 1	 //0X81 with key_id
#define VIRTUAL_FUN_2	 2	 //0x81 with x position
#define VIRTUAL_FUN_3	 3	 //Judge x & y position
#define VIRTUAL_FUN		 VIRTUAL_FUN_1
#define BTN_DELAY_TIME	 500 //ms
#define VELOCITY_CUSTOM_ILI_MTK6589   
#define TOUCH_POINT    0x80
#define TOUCH_KEY      0xC0
#define RELEASE_KEY    0x40
#define RELEASE_POINT    0x00
//#define ROTATE_FLAG
extern struct tpd_device *tpd;

#ifdef TPD_HAVE_BUTTON 
static int tpd_keys_local[TPD_KEY_COUNT] = TPD_KEYS;
static int tpd_keys_dim_local[TPD_KEY_COUNT][4] = TPD_KEYS_DIM;
#endif

#define CLOCK_INTERRUPT

//define key pad range
#define KEYPAD01_X1	 0
#define KEYPAD01_X2	 200
#define KEYPAD02_X1	 200
#define KEYPAD02_X2	 400
#define KEYPAD03_X1	 400
#define KEYPAD03_X2	 600
#define KEYPAD04_X1	 3000
#define KEYPAD04_X2	 3968
#define KEYPAD_Y	 2100
// definitions
#define ILITEK_I2C_RETRY_COUNT			 3
#define ILITEK_I2C_DRIVER_NAME			 "ilitek_i2c"
#define ILITEK_FILE_DRIVER_NAME			 "ilitek_file"
#define ILITEK_DEBUG_LEVEL			 KERN_ALERT //KERN_INFO
#define ILITEK_ERROR_LEVEL			 KERN_ALERT

// i2c command for ilitek touch screen
#define ILITEK_TP_CMD_READ_DATA			 0x10
#define ILITEK_TP_CMD_READ_SUB_DATA		 0x11
#define ILITEK_TP_CMD_GET_RESOLUTION		 0x20
#define ILITEK_TP_CMD_GET_FIRMWARE_VERSION	 0x40
#define ILITEK_TP_CMD_GET_PROTOCOL_VERSION	 0x42
#define	 ILITEK_TP_CMD_CALIBRATION			 0xCC
#define	 ILITEK_TP_CMD_CALIBRATION_STATUS	 0xCD
#define ILITEK_TP_CMD_ERASE_BACKGROUND		 0xCE

// define the application command
#define ILITEK_IOCTL_BASE                       100
#define ILITEK_IOCTL_I2C_WRITE_DATA             _IOWR(ILITEK_IOCTL_BASE, 0, unsigned char*)
#define ILITEK_IOCTL_I2C_WRITE_LENGTH           _IOWR(ILITEK_IOCTL_BASE, 1, int)
#define ILITEK_IOCTL_I2C_READ_DATA              _IOWR(ILITEK_IOCTL_BASE, 2, unsigned char*)
#define ILITEK_IOCTL_I2C_READ_LENGTH            _IOWR(ILITEK_IOCTL_BASE, 3, int)
#define ILITEK_IOCTL_USB_WRITE_DATA             _IOWR(ILITEK_IOCTL_BASE, 4, unsigned char*)
#define ILITEK_IOCTL_USB_WRITE_LENGTH           _IOWR(ILITEK_IOCTL_BASE, 5, int)
#define ILITEK_IOCTL_USB_READ_DATA              _IOWR(ILITEK_IOCTL_BASE, 6, unsigned char*)
#define ILITEK_IOCTL_USB_READ_LENGTH            _IOWR(ILITEK_IOCTL_BASE, 7, int)
#define ILITEK_IOCTL_I2C_UPDATE_RESOLUTION      _IOWR(ILITEK_IOCTL_BASE, 8, int)
#define ILITEK_IOCTL_USB_UPDATE_RESOLUTION      _IOWR(ILITEK_IOCTL_BASE, 9, int)
#define ILITEK_IOCTL_I2C_SET_ADDRESS            _IOWR(ILITEK_IOCTL_BASE, 10, int)
#define ILITEK_IOCTL_I2C_UPDATE                 _IOWR(ILITEK_IOCTL_BASE, 11, int)
#define ILITEK_IOCTL_STOP_READ_DATA             _IOWR(ILITEK_IOCTL_BASE, 12, int)
#define ILITEK_IOCTL_START_READ_DATA            _IOWR(ILITEK_IOCTL_BASE, 13, int)
#define ILITEK_IOCTL_GET_INTERFANCE				       _IOWR(ILITEK_IOCTL_BASE, 14, int)//default setting is i2c interface
#define ILITEK_IOCTL_I2C_SWITCH_IRQ				       _IOWR(ILITEK_IOCTL_BASE, 15, int)

//#define printk(fmt, args...)   if (DBG_FLAG)printk("%s(%d): " fmt, __func__,__LINE__,  ## args)

// module information
MODULE_AUTHOR("Steward_Fu");
MODULE_DESCRIPTION("ILITEK I2C touch driver for Android platform");
MODULE_LICENSE("GPL");

// all implemented global functions must be defined in here 
// in order to know how many function we had implemented
static int ilitek_i2c_register_device(void);
static void ilitek_set_input_param(struct input_dev*, int, int, int);
static int ilitek_i2c_read_tp_info(void);
static int ilitek_init(void);
static void ilitek_exit(void);

// i2c functions
static int ilitek_i2c_transfer(struct i2c_client*, struct i2c_msg*, int);
static int ilitek_i2c_read(struct i2c_client*, uint8_t, uint8_t*, int);
static int ilitek_i2c_process_and_report(void);
static int ilitek_i2c_suspend(struct i2c_client*, pm_message_t);
static int ilitek_i2c_resume(struct i2c_client*);
static void ilitek_i2c_shutdown(struct i2c_client*);
static int ilitek_i2c_probe(struct i2c_client*, const struct i2c_device_id*);
static int ilitek_i2c_remove(struct i2c_client*);
#ifdef CONFIG_HAS_EARLYSUSPEND
        static void ilitek_i2c_early_suspend(struct early_suspend *h);
        static void ilitek_i2c_late_resume(struct early_suspend *h);
#endif
static int ilitek_i2c_polling_thread(void*);
//static irqreturn_t ilitek_i2c_isr(int, void*);
static void ilitek_i2c_isr(void);
static void ilitek_i2c_irq_work_queue_func(struct work_struct*);

// file operation functions
static int ilitek_file_open(struct inode*, struct file*);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
static long ilitek_file_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#else
static int  ilitek_file_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
#endif
static int ilitek_file_open(struct inode*, struct file*);
static ssize_t ilitek_file_write(struct file*, const char*, size_t, loff_t*);
static ssize_t ilitek_file_read(struct file*, char*, size_t, loff_t*);
static int ilitek_file_close(struct inode*, struct file*);

static void ilitek_i2c_irq_enable(void);//luca 20120120
static void ilitek_i2c_irq_disable(void);//luca 20120120

static int tpd_i2c_detect(struct i2c_client *client, struct i2c_board_info *info);
extern void mt65xx_eint_registration(kal_uint8 eintno, kal_bool Dbounce_En,
                                     kal_bool ACT_Polarity, void (EINT_FUNC_PTR)(void),
                                     kal_bool auto_umask);
extern mt65xx_eint_unmask(unsigned int line);
extern mt65xx_eint_mask(unsigned int line);
extern void mt65xx_eint_set_hw_debounce(kal_uint8 eintno,kal_uint32 ms);
extern kal_uint32 mt65xx_eint_set_sens(kal_uint8 eintno,kal_bool sens);
static int ilitek_upgrade_firmware(void);
extern int  fix_tp_proc_info(void  *tp_data, u8 data_len); //add by yangjuwei
//static DECLARE_WAIT_QUEUE_HEAD(waiter);
// declare touch point data
/*struct touch_data {
	 // x, y value
	 int x, y;
	 // check wehther this point is valid or not
	 int valid;
	 // id information
	 int id;
};
*/
// declare i2c data member
struct i2c_data {
	 // input device
        struct input_dev *input_dev;
        // i2c client
        struct i2c_client *client;
        // polling thread
        struct task_struct *thread;        
        unsigned char firmware_ver[4];
        // maximum x
        int max_x;
        // maximum y
        int max_y;
	 // maximum touch point
	 int max_tp;
	 // maximum key button
	 int max_btn;
        // the total number of x channel
        int x_ch;
        // the total number of y channel
        int y_ch;
        // check whether i2c driver is registered success
        int valid_i2c_register;
        // check whether input driver is registered success
        int valid_input_register;
	 // check whether the i2c enter suspend or not
	 int stop_polling;
	 // read semaphore
	 struct semaphore wr_sem;
	 // protocol version
	 int protocol_ver;
	 // valid irq request
	 int valid_irq_request;
	 // work queue for interrupt use only
	 struct workqueue_struct *irq_work_queue;
	 // work struct for work queue
	 struct work_struct irq_work;
	
    struct timer_list timer;
	
	 int irq_status;
	 //irq_status enable:1 disable:0
	 struct completion complete;
#ifdef CONFIG_HAS_EARLYSUSPEND
	 struct early_suspend early_suspend;
#endif
};

// device data
struct dev_data {
        // device number
        dev_t devno;
        // character device
        struct cdev cdev;
        // class device
        struct class *class;
};

// global variables
static struct i2c_data i2c;
static struct dev_data dev;
static char DBG_FLAG=1;
static char Report_Flag;

static u8 *I2CDMABuf_va = NULL;
static u32 I2CDMABuf_pa = NULL;

static struct i2c_client *i2c_client = NULL;
static const struct i2c_device_id tpd_i2c_id[] = {{"ilitek",0},{}};
static unsigned short force[] = {0, 0x82, I2C_CLIENT_END,I2C_CLIENT_END};
static const unsigned short * const forces[] = { force, NULL };
//static struct i2c_client_address_data addr_data = { .forces = forces,};
static struct i2c_board_info __initdata i2c_tpd={ I2C_BOARD_INFO("ilitek", (0x82>>1))};

#if 1
static struct i2c_driver ilitek_i2c_driver =
{                       
    .probe = ilitek_i2c_probe,                                   
    .remove = ilitek_i2c_remove,                           
    .detect = tpd_i2c_detect,                           
    .driver.name = "ilitek", 
    .id_table = tpd_i2c_id,                             
    .address_list = (const unsigned short*) forces,                        
}; 
#else
// i2c id table
static const struct i2c_device_id ilitek_i2c_id[] ={
	{ILITEK_I2C_DRIVER_NAME, 0}, {}
};
MODULE_DEVICE_TABLE(i2c, ilitek_i2c_id);

// declare i2c function table
static struct i2c_driver ilitek_i2c_driver = {
	 .id_table = ilitek_i2c_id,
	 .driver = {.name = ILITEK_I2C_DRIVER_NAME},
	 .resume = ilitek_i2c_resume,
  .suspend  = ilitek_i2c_suspend,
	 .shutdown = ilitek_i2c_shutdown,
	 .probe = ilitek_i2c_probe,
	 .remove = ilitek_i2c_remove,
};
#endif

// declare file operations
struct file_operations ilitek_fops = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	 .unlocked_ioctl = ilitek_file_ioctl,
#else
	 .ioctl = ilitek_file_ioctl,
#endif
	 .read = ilitek_file_read,
	 .write = ilitek_file_write,
	 .open = ilitek_file_open,
	 .release = ilitek_file_close,
};

static struct miscdevice tpd_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "touch",
	.fops = &ilitek_fops,	
};                 
/*
description
	 open function for character device driver
prarmeters
	 inode
	     inode
	 filp
	     file pointer
return
	 status
*/
static int 
ilitek_file_open(struct inode *inode, struct file *filp)
{
	 printk("%sn",__func__);
	 return 0; 
}
/*
description
	 calibration function
prarmeters
	 count
	     buffer length
return
	 status
*/
static int ilitek_i2c_calibration(size_t count)
{

	 int ret;
	 unsigned char buffer[128]={0};
	 struct i2c_msg msgs[] = {
		{.addr = i2c.client->addr, .flags = 0, .len = count, .buf = buffer,}
	};
	
	 buffer[0] = ILITEK_TP_CMD_ERASE_BACKGROUND;
	 msgs[0].len = 1;
	 ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
	 if(ret < 0){
		 printk(ILITEK_DEBUG_LEVEL "%s, i2c erase background, failed\n", __func__);
	}
	 else{
		 printk(ILITEK_DEBUG_LEVEL "%s, i2c erase background, success\n", __func__);
	}

	 buffer[0] = ILITEK_TP_CMD_CALIBRATION;
	 msgs[0].len = 1;
	 msleep(2000);
	 ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
	 msleep(1000);
	 return ret;
}
/*
description
	 read calibration status
prarmeters
	 count
	     buffer length
return
	 status
*/
static int ilitek_i2c_calibration_status(size_t count)
{
	 int ret;
	 unsigned char buffer[128]={0};
	 struct i2c_msg msgs[] = {
		{.addr = i2c.client->addr, .flags = 0, .len = count, .buf = buffer,}
	};
	 buffer[0] = ILITEK_TP_CMD_CALIBRATION_STATUS;
	 ilitek_i2c_transfer(i2c.client, msgs, 1);
	 msleep(500);
	 ilitek_i2c_read(i2c.client, ILITEK_TP_CMD_CALIBRATION_STATUS, buffer, 1);
	 printk("%s, i2c calibration status:0x%X\n",__func__,buffer[0]);
	 ret=buffer[0];
	 return ret;
}
/*
description
	 write function for character device driver
prarmeters
	 filp
	     file pointer
	 buf
	     buffer
	 count
	     buffer length
	 f_pos
	     offset
return
	 status
*/
static ssize_t 
ilitek_file_write(
	 struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	 int ret;
	 unsigned char buffer[128]={0};
        
	 // before sending data to touch device, we need to check whether the device is working or not
	 if(i2c.valid_i2c_register == 0){
		 printk(ILITEK_ERROR_LEVEL "%s, i2c device driver doesn't be registered\n", __func__);
		 return -1;
	}

	 // check the buffer size whether it exceeds the local buffer size or not
	 if(count > 128){
		 printk(ILITEK_ERROR_LEVEL "%s, buffer exceed 128 bytes\n", __func__);
		 return -1;
	}

	 // copy data from user space
	 ret = copy_from_user(buffer, buf, count-1);
	 if(ret < 0){
		 printk(ILITEK_ERROR_LEVEL "%s, copy data from user space, failed\n", __func__);
		 return -1;
	}
	
	 // parsing command
	 if(strcmp(buffer, "calibrate") == 0){
		 ret=ilitek_i2c_calibration(count);
		 if(ret < 0){
			 printk(ILITEK_DEBUG_LEVEL "%s, i2c send calibration command, failed\n", __func__);
		}
		 else{
			 printk(ILITEK_DEBUG_LEVEL "%s, i2c send calibration command, success\n", __func__);
		}
		 ret=ilitek_i2c_calibration_status(count);
		 if(ret == 0x5A){
			 printk(ILITEK_DEBUG_LEVEL "%s, i2c calibration, success\n", __func__);
		}
		 else if (ret == 0xA5){
			 printk(ILITEK_DEBUG_LEVEL "%s, i2c calibration, failed\n", __func__);
		}
		 else{
			 printk(ILITEK_DEBUG_LEVEL "%s, i2c calibration, i2c protoco failed\n", __func__);
		}
		 return count;
	}else if(strcmp(buffer, "dbg") == 0){
		 DBG_FLAG=!DBG_FLAG;
		 printk("%s, %s message(%X).\n",__func__,DBG_FLAG?"Enabled":"Disabled",DBG_FLAG);
	}else if(strcmp(buffer, "info") == 0){
		 ilitek_i2c_read_tp_info();
	}else if(strcmp(buffer, "report") == 0){
		 Report_Flag=!Report_Flag;
	}
	 return -1;
}

/*
description
        ioctl function for character device driver
prarmeters
	 inode
		 file node
        filp
            file pointer
        cmd
            command
        arg
            arguments
return
        status
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
static long ilitek_file_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
#else
static int  ilitek_file_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
#endif
{
	 static unsigned char buffer[64]={0};
	 static int len=0;
	 int ret;
	 struct i2c_msg msgs[] = {
		{.addr = i2c.client->addr, .flags = 0, .len = len, .buf = buffer,}
        };

	 // parsing ioctl command
	 switch(cmd){
		 case ILITEK_IOCTL_I2C_WRITE_DATA:
			 ret = copy_from_user(buffer, (unsigned char*)arg, len);
			 if(ret < 0){
						 printk(ILITEK_ERROR_LEVEL "%s, copy data from user space, failed\n", __func__);
						 return -1;
				}
			 ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
			 if(ret < 0){
				 printk(ILITEK_ERROR_LEVEL "%s, i2c write, failed\n", __func__);
				 return -1;
			}
			 break;
		 case ILITEK_IOCTL_I2C_READ_DATA:
			 msgs[0].flags = I2C_M_RD;
	
			 ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
			 if(ret < 0){
				 printk(ILITEK_ERROR_LEVEL "%s, i2c read, failed\n", __func__);
				 return -1;
			}
			 ret = copy_to_user((unsigned char*)arg, buffer, len);
			
			 if(ret < 0){
				 printk(ILITEK_ERROR_LEVEL "%s, copy data to user space, failed\n", __func__);
				 return -1;
			}
			 break;
		 case ILITEK_IOCTL_I2C_WRITE_LENGTH:
		 case ILITEK_IOCTL_I2C_READ_LENGTH:
			 len = arg;
			 break;
		 case ILITEK_IOCTL_I2C_UPDATE_RESOLUTION:
		 case ILITEK_IOCTL_I2C_SET_ADDRESS:
		 case ILITEK_IOCTL_I2C_UPDATE:
			 break;
		 case ILITEK_IOCTL_START_READ_DATA:
			 i2c.stop_polling = 0;
			 if(i2c.client->irq != 0 )
				 ilitek_i2c_irq_enable();
			 break;
		 case ILITEK_IOCTL_STOP_READ_DATA:
			 i2c.stop_polling = 1;
			 if(i2c.client->irq != 0 )
				 ilitek_i2c_irq_disable();
			 break;
		 case ILITEK_IOCTL_I2C_SWITCH_IRQ:
			 ret = copy_from_user(buffer, (unsigned char*)arg, 1);
			 if (buffer[0]==0)
			{
				 if(i2c.client->irq != 0 ){
					 ilitek_i2c_irq_disable();
				}
			}
			 else
			{
				 if(i2c.client->irq != 0 ){
					 ilitek_i2c_irq_enable();				
				}
			}
			 break;	
		 default:
			 return -1;
	}
    	 return 0;
}

/*
description
	 read function for character device driver
prarmeters
	 filp
	     file pointer
	 buf
	     buffer
	 count
	     buffer length
	 f_pos
	     offset
return
	 status
*/
static ssize_t
ilitek_file_read(
        struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	 return 0;
}

/*
description
	 close function
prarmeters
	 inode
	     inode
	 filp
	     file pointer
return
	 status
*/
static int 
ilitek_file_close(
	 struct inode *inode, struct file *filp)
{
	 printk("%s\n",__func__);
        return 0;
}

/*
description
	 set input device's parameter
prarmeters
	 input
		 input device data
	 max_tp
		 single touch or multi touch
	 max_x
		 maximum	 x value
	 max_y
		 maximum y value
return
	 nothing
*/
static void 
ilitek_set_input_param(
	 struct input_dev *input, 
	 int max_tp, 
	 int max_x, 
	 int max_y)
{
	 int key;
	 input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	 input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	 #ifndef ROTATE_FLAG
	 input_set_abs_params(input, ABS_MT_POSITION_X, 0, max_x+2, 0, 0);
	 input_set_abs_params(input, ABS_MT_POSITION_Y, 0, max_y+2, 0, 0);
	 #else
	 input_set_abs_params(input, ABS_MT_POSITION_X, 0, max_y+2, 0, 0);
	 input_set_abs_params(input, ABS_MT_POSITION_Y, 0, max_x+2, 0, 0);
	 #endif
	 input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	 input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	 input_set_abs_params(input, ABS_MT_TRACKING_ID, 0, max_tp, 0, 0);
	 for(key=0; key<sizeof(touch_key_code); key++){
        	 if(touch_key_code[key] <= 0){
            		 continue;
		}
        	 set_bit(touch_key_code[key] & KEY_MAX, input->keybit);
	}
	 input->name = ILITEK_I2C_DRIVER_NAME;
	 input->id.bustype = BUS_I2C;
	 input->dev.parent = &(i2c.client)->dev;
}

/*
description
	 send message to i2c adaptor
parameter
	 client
		 i2c client
	 msgs
		 i2c message
	 cnt
		 i2c message count
return
	 >= 0 if success
	 others if error
*/
/*static int ilitek_i2c_transfer(struct i2c_client *client, struct i2c_msg *msgs, int cnt)
{
	 int ret, count=ILITEK_I2C_RETRY_COUNT;
	 while(count >= 0){
		 count-= 1;
		 ret = down_interruptible(&i2c.wr_sem);
                ret = i2c_transfer(client->adapter, msgs, cnt);
                up(&i2c.wr_sem);
                if(ret < 0){
                        msleep(500);
			 continue;
                }
		 break;
	}
	 return ret;
}*/
	static int ilitek_dma_i2c_read(struct i2c_client *client, unsigned char *buf, int len)
	{
		int i = 0, err = 0;
	
		if(len < 8)
		{
			client->addr = client->addr & I2C_MASK_FLAG;
			return i2c_master_recv(client, buf, len);
		}
		else
		{
			client->addr = client->addr & I2C_MASK_FLAG | I2C_DMA_FLAG;
			err = i2c_master_recv(client, (u8 *)I2CDMABuf_pa, len);
	
			if(err < 0)
			{
				return err;
			}
	
			for(i = 0; i < len; i++)
			{
				buf[i] = I2CDMABuf_va[i];
			}
		}
	}
	
	
	
	static int ilitek_dma_i2c_write(struct i2c_client *client, unsigned char *pbt_buf, int dw_len)
	{
		int i = 0;
		for(i = 0 ; i < dw_len; i++)
		{
			I2CDMABuf_va[i] = pbt_buf[i];
		}
	
		if(dw_len <= 8)
		{
			client->addr = client->addr & I2C_MASK_FLAG;
			return i2c_master_send(client, pbt_buf, dw_len);
		}
		else
		{
			client->addr = client->addr & I2C_MASK_FLAG | I2C_DMA_FLAG;
			return i2c_master_send(client, (u8 *)I2CDMABuf_pa, dw_len);
		}
	}
	
	/*
	description
		 send message to i2c adaptor
	parameter
		 client
			 i2c client
		 msgs
			 i2c message
		 cnt
			 i2c message count
	return
		 >= 0 if success
		 others if error
	*/
	static int ilitek_i2c_transfer(struct i2c_client *client, struct i2c_msg *msgs, int cnt)
	{
		 int i,ret=0, count=ILITEK_I2C_RETRY_COUNT;
		for(i=0;i<cnt;i++){
			if(msgs[i].len <= 8){
				msgs[i].addr &= I2C_MASK_FLAG;
				msgs[i].timing = 400;		 
				msgs[i].ext_flag = 0;  
				while(count >= 0){
					 count-= 1;
					 ret = i2c_transfer(client->adapter, &msgs[i], 1);
					 if((count == 0)&&(ret<0)){
						printk("%s i2c transfer <=8 bytes error",__func__);
						return ret;
					 }
					 if(ret < 0){				  
						msleep(500);
						continue;
					 }
					 break;
				}
			}	 
			else{
				msgs[i].ext_flag = 0;
				if(msgs[i].flags == I2C_M_RD)				
					ret = ilitek_dma_i2c_read(client,msgs[i].buf,msgs[i].len);
				else if(msgs[i].flags == 0)
					ret = ilitek_dma_i2c_write(client,msgs[i].buf,msgs[i].len); 		   
				if(ret<0)
					printk("%s i2c transfer >8 bytes error",__func__);					  
			}		 
		}
		 return ret;
	}

/*
description
	 read data from i2c device
parameter
	 client
		 i2c client data
	 addr
		 i2c address
	 data
		 data for transmission
	 length
		 data length
return
	 status
*/
static int 
ilitek_i2c_read(
	 struct i2c_client *client,
	 uint8_t cmd, 
	 uint8_t *data, 
	 int length)
{
	 int ret;
    struct i2c_msg msgs[] = {
		{.addr = client->addr, .flags = 0, .len = 1, .buf = &cmd,},
		{.addr = client->addr, .flags = I2C_M_RD, .len = length, .buf = data,}
    };

    ret = ilitek_i2c_transfer(client, msgs, 2);
	 if(ret < 0){
		 printk(ILITEK_ERROR_LEVEL "%s, i2c read error, ret %d,addr %x \n", __func__, ret,client->addr);
	}
	 return ret;
}

/*
description
	 process i2c data and then report to kernel
parameters
	 none
return
	 status
*/

static  void tpd_down(int x, int y, int track_id)
{
   // input_report_abs(tpd->dev, ABS_PRESSURE, 128);
    input_report_key(tpd->dev, BTN_TOUCH, 1);
    input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 20);	//128);
   input_event(tpd->dev, EV_ABS, ABS_MT_TRACKING_ID, track_id);

    //input_report_abs(tpd->dev, ABS_MT_WIDTH_MAJOR, 128);
    input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
    input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
    input_mt_sync(tpd->dev);

   //add by lisong for cit test
   if (FACTORY_BOOT == get_boot_mode())
   {   
   	tpd_button(x, y, 1);  
   }
}

static void tpd_up(int x, int y)
{
    //input_report_abs(tpd->dev, ABS_PRESSURE, 0);
    input_report_key(tpd->dev, BTN_TOUCH, 0);
    //input_report_abs(tpd->dev, ABS_MT_TOUCH_MAJOR, 0);
    //input_report_abs(tpd->dev, ABS_MT_WIDTH_MAJOR, 0);
    //input_report_abs(tpd->dev, ABS_MT_POSITION_X, x);
    //input_report_abs(tpd->dev, ABS_MT_POSITION_Y, y);
    input_mt_sync(tpd->dev);

   //add by lisong for cit test
   if (FACTORY_BOOT == get_boot_mode())
   {   
   	tpd_button(x, y, 0);  
   }
}
static int ilitek_i2c_process_and_report(void)
{
	 int i, len, ret, x=0, y=0,key,org_x=0, org_y=0,mult_tp_id,j;
	 unsigned char key_id=0,key_flag=1;
	 static unsigned char last_id=0;
	 struct input_dev *input = i2c.input_dev;
    unsigned char buf[9]={0};
	 unsigned char tp_id;
	 unsigned char release_counter=0;
	 int temp_x,temp_y;
    // read i2c data from device

    ret = ilitek_i2c_read(i2c.client, ILITEK_TP_CMD_READ_DATA, buf, 8);
	 if(ret < 0){
		 return ret;
	}

	 // multipoint process
	 if(i2c.protocol_ver & 0x200){
		 len = buf[0];
		 ret = 1;
		 if(len > 16){
			 return ret;
		}
		 //printk("multi touch n");
		 // read touch point
		 for(i=0; i<len; i++){
			 // parse point

			 if(ilitek_i2c_read(i2c.client, ILITEK_TP_CMD_READ_SUB_DATA, buf, 5)){

				 #ifndef ROTATE_FLAG
				 x = (((int)buf[1]) << 8) + buf[2];
				 y = (((int)buf[3]) << 8) + buf[4];
				 temp_x = y;
				 temp_y = x;
			
				 x = temp_x;
				 y = temp_y;
				 y = i2c.max_y - y;
				 
				 //printk("\n###################### x= %d, y = %d ######\n", x , y);
				 #else
				 org_x = (((int)buf[1]) << 8) + buf[2];
				 org_y = (((int)buf[3]) << 8) + buf[4];
				 //x = org_y + 1;
				 x = i2c.max_y-(org_y + 1);//shaokai
				 y = org_x + 1;					
				 #endif
				 //x=x%540;
				 //y=y%960;
				
				 mult_tp_id = buf[0];
				
				// printk("mult_tp_id=%x \n",mult_tp_id);
				 switch ((mult_tp_id & 0xC0)){
					 case RELEASE_KEY:
					 	#if 0
						 //release key
						 printk("\n #####Key: Release line : %d\n", __LINE__);
						 for(key=0; key<sizeof(touch_key_code)/sizeof(touch_key_code[0]); key++){
							 if(touch_key_press[key]){
								 input_report_key(input, touch_key_code[key], 0);
								 touch_key_press[key] = 0;
								 printk("\n######Key:%d ID:%d release line : %d\n", touch_key_code[key], key,__LINE__);
								 printk(ILITEK_DEBUG_LEVEL "%s key release, %X, %d, %d\n", __func__, buf[0], x, y);
							}
							 touch_key_hold_press=0;
							 //ret = 1;// stop timer interrupt	
						}		
						#endif
						 break;
					
					 case TOUCH_KEY:
						 //touch key
						 //#if VIRTUAL_FUN==VIRTUAL_FUN_1
						 //printk("\n #####Key: touch  line : %d\n", __LINE__);

						 key_id = buf[1] - 1;
						 //printk("mult_tp_id_1=%x \n",mult_tp_id);
						 //printk("key_id=%x \n",key_id);

						x = tpd_keys_dim_local[key_id][0];
						y= tpd_keys_dim_local[key_id][1];

						tpd_down(x,y, (buf[0] & 0x3F)-1);
						 //#endif	
						 #if VIRTUAL_FUN==VIRTUAL_FUN_2
						 if (abs(jiffies-touch_time) < msecs_to_jiffies(BTN_DELAY_TIME))
							 break;
						 //printk("Key: Entern");
						 y = (((int)buf[2]) << 8) + buf[1];
						x = (((int)buf[4]) << 8) + buf[3];
						 //printk("%s,x=%dn",__func__,x);
						 //printk("%s,y=%dn",__func__,y);
						 if (x > KEYPAD01_X1 && x<KEYPAD01_X2)		 // btn 1
							 key_id=0;
						 else if (x > KEYPAD02_X1 && x<KEYPAD02_X2)	 // btn 2
							 key_id=1;
						 else if (x > KEYPAD03_X1 && x<KEYPAD03_X2)	 // btn 3
							 key_id=2;
						 else if (x > KEYPAD04_X1 && x<KEYPAD04_X2)	 // btn 4
							 key_id=3;
						 else 
							 key_flag=0;
						 #endif
						#if 0
						  if((touch_key_press[key_id] == 0) && (touch_key_hold_press == 0 && key_flag)){
							 input_report_key(input, touch_key_code[key_id], 1);
							 touch_key_press[key_id] = 1;
							 touch_key_hold_press = 1;
							 printk("\n#####Key:%d ID:%d press x=%d,touch_key_hold_press=%d,key_flag=%d\n", touch_key_code[key_id], key_id,x,touch_key_hold_press,key_flag);
						}
						#endif
						 break;
					case TOUCH_POINT:
						#if 0
						{				
						 // report to android system
						 //printk("\n####Point, ID=%02X, X=%04d, Y=%04d,touch_key_hold_press=%d\n",buf[0]  & 0x3F, x,y,touch_key_hold_press);	
						 input_report_key(input, BTN_TOUCH,  1);//Luca 20120402
						 input_event(input, EV_ABS, ABS_MT_TRACKING_ID, (buf[0] & 0x3F)-1);
						 input_event(input, EV_ABS, ABS_MT_POSITION_X, x+1);
						 input_event(input, EV_ABS, ABS_MT_POSITION_Y, y+1);
						 input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, 20);
						 input_mt_sync(input);
						 TPD_EM_PRINT(x, y, x, y, (buf[0] & 0x3F)-2, 1);
						 ret=0;
						}
						#endif
						tpd_down(x,y,(buf[0] & 0x3F)-1);

						 break;
						
					 case RELEASE_POINT:
						// printk("\n #####point  release  line : %d\n", __LINE__);
						#if 0
						 if (touch_key_hold_press !=0 && i==0){
							 for(key=0; key<sizeof(touch_key_code)/sizeof(touch_key_code[0]); key++){
								 if(touch_key_press[key]){
									 input_report_key(input, touch_key_code[key], 0);
									 touch_key_press[key] = 0;
									 printk("\nKey:%d ID:%d release, line :%d\n", touch_key_code[key], key,__LINE__);
									 printk(ILITEK_DEBUG_LEVEL "%s key release, %X, %d, %d,touch_key_hold_press=%d\n", __func__, buf[0], x, y,touch_key_hold_press);
								}
								 touch_key_hold_press=0;
								 //ret = 1;// stop timer interrupt	
							}		
						}	
						 #endif
						 break;
						
					 default:
						 break;
				}
			}
		}
		 // release point
		 if(len == 0){
		 	#if 0
			 printk("\n #####len : 0,  line : %d\n", __LINE__);

			 printk("\nRelease3, ID=%02X, X=%04d, Y=%04d\n",buf[0]  & 0x3F, x,y);
			 input_report_key(input, BTN_TOUCH,  0);
			 //input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, 0);
			 input_mt_sync(input);
			 TPD_EM_PRINT(x, y, x, y, 0, 0);
			 //ret = 1;
			 if (touch_key_hold_press !=0){
				 for(key=0; key<sizeof(touch_key_code)/sizeof(touch_key_code[0]); key++){
					 if(touch_key_press[key]){
						 input_report_key(input, touch_key_code[key], 0);
						 touch_key_press[key] = 0;
						 printk("\nKey:%d ID:%d release, line : %d\n", touch_key_code[key], key, __LINE__);
						 printk(ILITEK_DEBUG_LEVEL "%s key release, %X, %d, %d\n", __func__, buf[0], x, y);
					}
					 touch_key_hold_press=0;
					 //ret = 1;// stop timer interrupt	
				}		
			}

			#endif
			tpd_up(x,y);
		}
		 input_sync(input);
		 //printk("%s,ret=%d\n",__func__,ret);
	}
	  else{

		 // parse point
		 ret = 0;
		
		 tp_id = buf[0];
		 if (Report_Flag!=0){
			 printk("%s(%d):",__func__,__LINE__);
			 for (i=0;i<16;i++)
				 printk("%02X,",buf[i]);
			 printk("\n");
		}
		 switch (tp_id)
		{
			 case 0://release point
#ifdef VIRTUAL_KEY_PAD				
				 if (touch_key_hold_press !=0)
				{
					 for(key=0; key<sizeof(touch_key_code)/sizeof(touch_key_code[0]); key++){
						 if(touch_key_press[key]){
							 //input_report_key(input, touch_key_code[key], 0);
							 touch_key_press[key] = 0;
							 printk("Key:%d ID:%d release\n", touch_key_code[key], key);
						}
					}
					 touch_key_hold_press = 0;
				}
				 else
#endif
				{
					 for(i=0; i<i2c.max_tp; i++){
						 // check 
						 if (!(last_id & (1<<i)))
							 continue;	
							
						 #ifndef ROTATE_FLAG
						 x = (int)buf[1 + (i * 4)] + ((int)buf[2 + (i * 4)] * 256);
						 y = (int)buf[3 + (i * 4)] + ((int)buf[4 + (i * 4)] * 256);
						 #else
						 org_x = (int)buf[1 + (i * 4)] + ((int)buf[2 + (i * 4)] * 256);
						 org_y = (int)buf[3 + (i * 4)] + ((int)buf[4 + (i * 4)] * 256);
						 //x = i2c.max_y - org_y + 1;
						 x = i2c.max_y-(i2c.max_y - org_y + 1);
						 y = org_x + 1;					
						 #endif
						 touch_key_hold_press=2; //2: into available area
						 input_report_key(input, BTN_TOUCH,  1);//Luca 20120402
						 input_event(i2c.input_dev, EV_ABS, ABS_MT_TRACKING_ID, i);
						 input_event(i2c.input_dev, EV_ABS, ABS_MT_POSITION_X, x+1);
						 input_event(i2c.input_dev, EV_ABS, ABS_MT_POSITION_Y, y+1);
						 input_event(i2c.input_dev, EV_ABS, ABS_MT_TOUCH_MAJOR, 20);
						 input_mt_sync(i2c.input_dev);
						 printk("Last Point[%d]= %d, %d\n", buf[0]&0x3F, x, y);
						 last_id=0;
					}
					 input_sync(i2c.input_dev);//20120407				
					 input_event(i2c.input_dev, EV_ABS, ABS_MT_TOUCH_MAJOR, 0);
					 input_mt_sync(i2c.input_dev);
					 ret = 1; // stop timer interrupt
				}
				 break;
#ifdef VIRTUAL_KEY_PAD				
			 case 0x81:
				 if (abs(jiffies-touch_time) < msecs_to_jiffies(BTN_DELAY_TIME))
					 break;
				 printk("Key: Enter\n");
	
				 #if VIRTUAL_FUN==VIRTUAL_FUN_1
				 key_id = buf[1] - 1;
				 #endif
				
				 #if VIRTUAL_FUN==VIRTUAL_FUN_2
				 x = (int)buf[1] + ((int)buf[2] * 256);
				 if (x > KEYPAD01_X1 && x<KEYPAD01_X2)		 // btn 1
					 key_id=0;
				 else if (x > KEYPAD02_X1 && x<KEYPAD02_X2)	 // btn 2
					 key_id=1;
				 else if (x > KEYPAD03_X1 && x<KEYPAD03_X2)	 // btn 3
					 key_id=2;
				 else if (x > KEYPAD04_X1 && x<KEYPAD04_X2)	 // btn 4
					 key_id=3;
				 else 
					 key_flag=0;			
				 #endif
				 input_report_abs(input, ABS_MT_TOUCH_MAJOR, 0);
    				 input_mt_sync(input);
				 if((touch_key_press[key_id] == 0) && (touch_key_hold_press == 0 && key_flag)){
					 input_report_key(input, touch_key_code[key_id], 1);
					 touch_key_press[key_id] = 1;
					 touch_key_hold_press = 1;
					 printk("Key:%d ID:%d press\n", touch_key_code[key_id], key_id);
				}			
				 break;
			 case 0x80:
				 printk("Key: Release\n");
				 for(key=0; key<sizeof(touch_key_code)/sizeof(touch_key_code[0]); key++){
					 if(touch_key_press[key]){
						 input_report_key(input, touch_key_code[key], 0);
						 touch_key_press[key] = 0;
						 printk("Key:%d ID:%d release\n", touch_key_code[key], key);
                   	}
				}		
				 touch_key_hold_press=0;
				 ret = 1;// stop timer interrupt	
				 break;
#endif
			 default:
				 last_id=buf[0];
				 for(i=0; i<i2c.max_tp; i++){
					 // check 
					 if (!(buf[0] & (1<<i)))
						 continue;	
						
					 #ifndef ROTATE_FLAG
					 x = (int)buf[1 + (i * 4)] + ((int)buf[2 + (i * 4)] * 256);
					 y = (int)buf[3 + (i * 4)] + ((int)buf[4 + (i * 4)] * 256);
					 #else
					 org_x = (int)buf[1 + (i * 4)] + ((int)buf[2 + (i * 4)] * 256);
					 org_y = (int)buf[3 + (i * 4)] + ((int)buf[4 + (i * 4)] * 256);
					 //x = i2c.max_y - org_y + 1;
					 x = i2c.max_y-( i2c.max_y - org_y + 1);
					 y = org_x + 1;					
					 #endif
#ifdef VIRTUAL_KEY_PAD						
					 #if VIRTUAL_FUN==VIRTUAL_FUN_3
					 if (y > KEYPAD_Y){
						 if (abs(jiffies-touch_time) < msecs_to_jiffies(BTN_DELAY_TIME))
							 break;									
						 x = (int)buf[1] + ((int)buf[2] * 256);
						 if (x > KEYPAD01_X1 && x<KEYPAD01_X2)		 // btn 1
							 key_id=0;
						 else if (x > KEYPAD02_X1 && x<KEYPAD02_X2)	 // btn 2
							 key_id=1;
						 else if (x > KEYPAD03_X1 && x<KEYPAD03_X2)	 // btn 3
							 key_id=2;
						 else if (x > KEYPAD04_X1 && x < KEYPAD04_X2)	 // btn 4
							 key_id=3;
						 else 
							 key_flag=0;			
						 if (touch_key_hold_press==2){
							 input_report_key(input, BTN_TOUCH,  0);//Luca 20120402
							 input_event(i2c.input_dev, EV_ABS, ABS_MT_TOUCH_MAJOR, 0);
							 input_mt_sync(i2c.input_dev);
							 touch_key_hold_press=0;
						}
						 if((touch_key_press[key_id] == 0) && (touch_key_hold_press == 0 && key_flag)){
							 //input_report_key(input, touch_key_code[key_id], 1);
							 touch_key_press[key_id] = 1;
							 touch_key_hold_press = 1;
							 printk("Key:%d ID:%d press\n", touch_key_code[key_id], key_id);					
						}
					}
					 else if (touch_key_hold_press){
						 for(key=0; key<sizeof(touch_key_code)/sizeof(touch_key_code[0]) ; key++){
							 if(touch_key_press[key]){
								 //input_report_key(input, touch_key_code[key], 0);
								 touch_key_press[key] = 0;
								 printk("Key:%d ID:%d release\n", touch_key_code[key], key);
							}
						}
						 touch_key_hold_press = 0;
					}
					 else
					 #endif
					 touch_time=jiffies + msecs_to_jiffies(BTN_DELAY_TIME);
#endif					
					{
						 touch_key_hold_press=2; //2: into available area
						 input_report_key(input, BTN_TOUCH,  1);//Luca 20120402
						 input_event(i2c.input_dev, EV_ABS, ABS_MT_TRACKING_ID, i);
						 input_event(i2c.input_dev, EV_ABS, ABS_MT_POSITION_X, x+1);
						 input_event(i2c.input_dev, EV_ABS, ABS_MT_POSITION_Y, y+1);
						 input_event(i2c.input_dev, EV_ABS, ABS_MT_TOUCH_MAJOR, 20);
						 input_mt_sync(i2c.input_dev);
						 printk("Point[%d]= %d, %d\n", buf[0]&0x3F, x, y);
					}
					
				}
				 break;
		}

		 printk("process report x=%d,y=%d \n",x,y);
		 input_sync(i2c.input_dev);
	}
	
    return ret;
}

static void ilitek_i2c_timer(unsigned long handle)
{
    struct i2c_data *priv = (void *)handle;

    schedule_work(&priv->irq_work);
}

/*
description
	 work queue function for irq use
parameter
	 work
		 work queue
return
	 nothing
*/
static void 
ilitek_i2c_irq_work_queue_func(
	 struct work_struct *work)
{
	 int ret;
	 //wait_event_interruptible(waiter,tpd_flag!=0);//
	 struct i2c_data *priv =  
		 container_of(work, struct i2c_data, irq_work);

	 ret = ilitek_i2c_process_and_report();

#ifdef CLOCK_INTERRUPT
	 ilitek_i2c_irq_enable();
#else
    if (ret == 0){
		 if (!i2c.stop_polling)
			 mod_timer(&priv->timer, jiffies + msecs_to_jiffies(0));
	}
    else if (ret == 1){
		 if (!i2c.stop_polling){
			 ilitek_i2c_irq_enable();
			 mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT);
		  	 mt_set_gpio_dir(GPIO_CTP_EINT_PIN, GPIO_DIR_IN);
			 mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_DISABLE);
			 mt65xx_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM); 
			 mt65xx_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
		}
		 printk("stop_polling\n");
	}
	 else if(ret < 0){
		 msleep(100);
		 printk(ILITEK_ERROR_LEVEL "%s, process error\n", __func__);
		 ilitek_i2c_irq_enable();
    }	
#endif
}

/*
description
	 i2c interrupt service routine
parameters
	 irq
		 interrupt number
	 dev_id
		 device parameter
return
	 return status
*/
/*static irqreturn_t 
ilitek_i2c_isr(
	 int irq, void *dev_id)
	 */
static void ilitek_i2c_isr(void)
{
	 printk("Enter\n");
	 //tpd_flag = 1;
	 if(i2c.irq_status ==1){
		 disable_irq_nosync(i2c.client->irq);
		mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
		 printk("disable nosync\n");
		 i2c.irq_status = 0;
	}
	 //wake_up_interruptible(&waiter);
	 printk("\n@@@func :%s, line :%d", __func__, __LINE__);
	 queue_work(i2c.irq_work_queue, &i2c.irq_work);
	
	 //return IRQ_HANDLED;
}

/*
description
        i2c polling thread
parameters
        arg
                arguments
return
        return status
*/
static int 
ilitek_i2c_polling_thread(
	 void *arg)
{

	 int ret=0;
	 // check input parameter
	 printk(ILITEK_DEBUG_LEVEL "%s, enter\n", __func__);

	 // mainloop
	 while(1){
		 // check whether we should exit or not
		 if(kthread_should_stop()){
			 printk(ILITEK_DEBUG_LEVEL "%s, stop\n", __func__);
			 break;
		}

		 // this delay will influence the CPU usage and response latency
		 msleep(5);
		
		 // when i2c is in suspend or shutdown mode, we do nothing
		 if(i2c.stop_polling){
			 msleep(1000);
			 continue;
		}

		 // read i2c data
		 if(ilitek_i2c_process_and_report() < 0){
			 msleep(3000);
			 printk(ILITEK_ERROR_LEVEL "%s, process error\n", __func__);
		}
	}
	
	 printk(ILITEK_DEBUG_LEVEL "%s, exit\n", __func__);
	 return ret;
}

/*
description
	 i2c early suspend function
parameters
	 h
		 early suspend pointer
return
	 nothing
*/
#ifdef CONFIG_HAS_EARLYSUSPEND
static void ilitek_i2c_early_suspend(struct early_suspend *h)
{
	 ilitek_i2c_suspend(i2c.client, PMSG_SUSPEND);
	 printk("%s\n", __func__);
}
#endif

/*
description
        i2c later resume function
parameters
        h
                early suspend pointer
return
        nothing
*/
#ifdef CONFIG_HAS_EARLYSUSPEND
static void ilitek_i2c_late_resume(struct early_suspend *h)
{
	 ilitek_i2c_resume(i2c.client);
	 printk("%s\n", __func__);
}
#endif
/*
description
        i2c irq enable function
*/
static void ilitek_i2c_irq_enable(void)
{
	 if (i2c.irq_status == 0){
		 i2c.irq_status = 1;
		 enable_irq(i2c.client->irq);
		 printk("enable\n");
		
	}
	 else
		 printk("no enable\n");
}
/*
description
        i2c irq disable function
*/
static void ilitek_i2c_irq_disable(void)
{
	 if (i2c.irq_status == 1){
		 i2c.irq_status = 0;
		 disable_irq(i2c.client->irq);
		 printk("disablen");
	}
	 else
		 printk("no disablen");
}

/*
description
        i2c suspend function
parameters
        client
		 i2c client data
	 mesg
		 suspend data
return
        return status
*/

static int 
ilitek_i2c_suspend(
	 struct i2c_client *client, pm_message_t mesg)
{
	 int ret;
	 unsigned char buffer[128]={0};
	 struct i2c_msg msgs[] = {
		{.addr = i2c.client->addr, .flags = 0, .len = 0, .buf = buffer,}
	};
	 printk("%s,enter\n",__func__);
	 if(i2c.valid_irq_request != 0){
		 ilitek_i2c_irq_disable();
	}
	 else{
		 i2c.stop_polling = 1;
 	        	 printk(ILITEK_DEBUG_LEVEL "%s, stop i2c thread polling\n", __func__);
  	}
  
	 buffer[0] = 0x30;
	 msgs[0].len = 1;
	 ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
	 mt_eint_mask(CUST_EINT_TOUCH_PANEL_NUM);
	 return 0;
}

/*
description
        i2c resume function
parameters
        client
		 i2c client data
return
        return status
*/
static int ilitek_i2c_resume(struct i2c_client *client)
{
	 printk("%s,enter\n",__func__);
	 if(i2c.valid_irq_request != 0){
		 ilitek_i2c_irq_enable();
	}
	 else{
		 i2c.stop_polling = 0;
        printk(ILITEK_DEBUG_LEVEL "%s, start i2c thread polling\n", __func__);
	}
	#if 0
	hwPowerOn(MT65XX_POWER_LDO_VIO28, VOL_2800, "TP");
	#else
	hwPowerOn(MT65XX_POWER_LDO_VIO28, VOL_1800, "TP");
	hwPowerOn(MT65XX_POWER_LDO_VGP1, VOL_2800, "TP");
	hwPowerOn(MT6323_POWER_LDO_VGP1, VOL_2800, "TP");
	#endif
// reset
   mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
   mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
   mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
   msleep(50);

   mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);
   msleep(100);
   mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
   msleep(50);
  mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
   
	  mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT);
 	  mt_set_gpio_dir(GPIO_CTP_EINT_PIN, GPIO_DIR_IN);
	  mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_DISABLE);
	  mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM); 
	 return 0;
}

/*
description
        i2c shutdown function
parameters
        client
                i2c client data
return
        nothing
*/
static void
ilitek_i2c_shutdown(
        struct i2c_client *client)
{
        printk(ILITEK_DEBUG_LEVEL "%s\n", __func__);
        i2c.stop_polling = 1;
}

/*
description
	 when adapter detects the i2c device, this function will be invoked.
parameters
	 client
		 i2c client data
	 id
		 i2c data
return
	 status
*/
static int 
ilitek_i2c_probe(
	 struct i2c_client *client, 
	 const struct i2c_device_id *id)
{
	unsigned char  tp_info[512];//add by yangjuwei
	int len; //add by yangjuwei
	 if(!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)){
                printk(ILITEK_ERROR_LEVEL "%s, I2C_FUNC_I2C not support\n", __func__);
                return -1;
        }
	 i2c.client = client;
        printk(ILITEK_DEBUG_LEVEL "%s, i2c new style format\n", __func__);
	 printk("%s, IRQ: 0x%X\n", __func__, client->irq);
	//yangjuwei add TP info start
	
	len = sprintf(tp_info, "TP IC :%s,TP module :%s,TP I2C adr : 0x%x,","gt668","DiJing",client->addr);

	fix_tp_proc_info(tp_info, len);
	//yangjuwei add TP info end
	 return 0;
}

/*
description
	 when the i2c device want to detach from adapter, this function will be invoked.
parameters
	 client
		 i2c client data
return
	 status
*/
static int 
ilitek_i2c_remove(
	 struct i2c_client *client)
{
	 printk(ILITEK_DEBUG_LEVEL "%s\n", __func__);
	 i2c.stop_polling = 1;
	 return 0;
}

/*
description
	 read data from i2c device with delay between cmd & return data
parameter
	 client
		 i2c client data
	 addr
		 i2c address
	 data
		 data for transmission
	 length
		 data length
return
	 status
*/
static int 
ilitek_i2c_read_info(
	 struct i2c_client *client,
	 uint8_t cmd, 
	 uint8_t *data, 
	 int length)
{
	 int ret;
	 struct i2c_msg msgs_cmd[] = {
	{.addr = client->addr, .flags = 0, .len = 1, .buf = &cmd,},
	};
	
	 struct i2c_msg msgs_ret[] = {
	{.addr = client->addr, .flags = I2C_M_RD, .len = length, .buf = data,}
	};
  printk("iic address =%x \n",client->addr);
	 ret = ilitek_i2c_transfer(client, msgs_cmd, 1);
	 if(ret < 0){
		 //printk(ILITEK_ERROR_LEVEL "%s, i2c read error, ret %dn", __func__, ret);
		 printk(ILITEK_ERROR_LEVEL "%s, i2c read error, ret %d,addr %x \n", __func__, ret,client->addr);
	}
	
	 msleep(10);
	 ret = ilitek_i2c_transfer(client, msgs_ret, 1);
	 if(ret < 0){
		 //printk(ILITEK_ERROR_LEVEL "%s, i2c read error, ret %dn", __func__, ret);		
		 printk(ILITEK_ERROR_LEVEL "%s, i2c read error, ret %d,addr %x \n", __func__, ret,client->addr);
	}
	
	 printk(ILITEK_ERROR_LEVEL "%s, Driver Vesrion: %s\n", __func__, DRIVER_VERSION);
	 return ret;
}

/*
description
	 read touch information
parameters
	 none
return
	 status
*/
static int
ilitek_i2c_read_tp_info(
	 void)
{
	 int res_len,i;
	 unsigned char buf[32]={0};

	 // read firmware version
        if(ilitek_i2c_read_info(i2c.client, ILITEK_TP_CMD_GET_FIRMWARE_VERSION, buf, 4) < 0){
		 return -1;
	}
	
    for(i = 0;i<4;i++)  i2c.firmware_ver[i] = buf[i];
	 printk(ILITEK_DEBUG_LEVEL "%s, firmware version %d.%d.%d.%d\n", __func__, buf[0], buf[1], buf[2], buf[3]);

	 // read protocol version
        res_len = 6;
        if(ilitek_i2c_read_info(i2c.client, ILITEK_TP_CMD_GET_PROTOCOL_VERSION, buf, 2) < 0){
		 return -1;
	}	
        i2c.protocol_ver = (((int)buf[0]) << 8) + buf[1];
        printk(ILITEK_DEBUG_LEVEL "%s, protocol version: %d.%d\n", __func__, buf[0], buf[1]);
        if(i2c.protocol_ver >= 0x200){
               	 res_len = 8;
        }

        // read touch resolution
	 i2c.max_tp = 2;
        if(ilitek_i2c_read_info(i2c.client, ILITEK_TP_CMD_GET_RESOLUTION, buf, res_len) < 0){
		 return -1;
	}
        if(i2c.protocol_ver >= 0x200){
                // maximum touch point
                i2c.max_tp = buf[6];
                // maximum button number
                i2c.max_btn = buf[7];
        }
	 // calculate the resolution for x and y direction
        i2c.max_y = buf[0];
        i2c.max_y+= ((int)buf[1]) * 256;
        i2c.max_x = buf[2];
        i2c.max_x+= ((int)buf[3]) * 256;
        i2c.x_ch = buf[4];
        i2c.y_ch = buf[5];
        printk(ILITEK_DEBUG_LEVEL "%s, max_x: %d, max_y: %d, ch_x: %d, ch_y: %d\n", 
		 __func__, i2c.max_x, i2c.max_y, i2c.x_ch, i2c.y_ch);
        printk(ILITEK_DEBUG_LEVEL "%s, max_tp: %d, max_btn: %d\n", __func__, i2c.max_tp, i2c.max_btn);
	 return 0;
}

/*
description
	upgrade F/W
prarmeters
		
return
	status
*/       
static int ilitek_upgrade_firmware(void)
{
	int ret=0,upgrade_status=0,i,j,k = 0,ap_len = 0,df_len = 0;
	unsigned char buffer[128]={0};
	unsigned long ap_startaddr,df_startaddr,ap_endaddr,df_endaddr,ap_checksum = 0,df_checksum = 0;
	unsigned char firmware_ver[4];
	struct i2c_msg msgs[] = {
		{.addr = i2c.client->addr, .flags = 0, .len = 0, .buf = buffer,}
	};
	ap_startaddr = ( CTPM_FW[0] << 16 ) + ( CTPM_FW[1] << 8 ) + CTPM_FW[2];
	ap_endaddr = ( CTPM_FW[3] << 16 ) + ( CTPM_FW[4] << 8 ) + CTPM_FW[5];
	ap_checksum = ( CTPM_FW[6] << 16 ) + ( CTPM_FW[7] << 8 ) + CTPM_FW[8];
	df_startaddr = ( CTPM_FW[9] << 16 ) + ( CTPM_FW[10] << 8 ) + CTPM_FW[11];
	df_endaddr = ( CTPM_FW[12] << 16 ) + ( CTPM_FW[13] << 8 ) + CTPM_FW[14];
	df_checksum = ( CTPM_FW[15] << 16 ) + ( CTPM_FW[16] << 8 ) + CTPM_FW[17];
	firmware_ver[0] = CTPM_FW[18];
	firmware_ver[1] = CTPM_FW[19];
	firmware_ver[2] = CTPM_FW[20];
	firmware_ver[3] = CTPM_FW[21];
	df_len = ( CTPM_FW[22] << 16 ) + ( CTPM_FW[23] << 8 ) + CTPM_FW[24];
	ap_len = ( CTPM_FW[25] << 16 ) + ( CTPM_FW[26] << 8 ) + CTPM_FW[27];
	printk("ap_startaddr=0x%d,ap_endaddr=0x%d,ap_checksum=0x%d\n",ap_startaddr,ap_endaddr,ap_checksum);
	printk("df_startaddr=0x%d,df_endaddr=0x%d,df_checksum=0x%d\n",df_startaddr,df_endaddr,df_checksum);	
	buffer[0]=0xc0;
	msgs[0].len = 1;
	ret = ilitek_i2c_read(i2c.client, 0xc0, buffer, 1);
	if(ret < 0){
		return 3;
	}
	msleep(30);
	printk("ic. mode =%d\n",buffer[0]);
		
	if(buffer[0]!=0x55){
		for(i=0;i<4;i++){
			printk("i2c.firmware_ver[%d]=%d,firmware_ver[%d]=%d\n",i,i2c.firmware_ver[i],i,firmware_ver[i]);
		
			if((i2c.firmware_ver[i] > firmware_ver[i])||((i == 3) && (i2c.firmware_ver[3] == firmware_ver[3]))){
				return 1;				
			}
			else if(i2c.firmware_ver[i] < firmware_ver[i]){
				break;
			}
			
		}	
		

		
		buffer[0]=0xc4;
		msgs[0].len = 1;
		ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
		if(ret < 0){
		return 3;
		}
		msleep(30);
		buffer[0]=0xc2;
		ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
		if(ret < 0){
		return 3;
		}		
		msleep(100);
	}
	buffer[0]=0xc0;
	msgs[0].len = 1;
	ret = ilitek_i2c_read(i2c.client, 0xc0, buffer, 1);
	if(ret < 0){
	return 3;
	}

	msleep(30);
	printk("ILITEK:%s, upgrade firmware...\n", __func__);
	buffer[0]=0xc4;
	msgs[0].len = 10;
	buffer[1] = 0x5A;
	buffer[2] = 0xA5;
	buffer[3] = 0;
	buffer[4] = CTPM_FW[3];
	buffer[5] = CTPM_FW[4];
	buffer[6] = CTPM_FW[5];
	buffer[7] = CTPM_FW[6];
	buffer[8] = CTPM_FW[7];
	buffer[9] = CTPM_FW[8];
	ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
	if(ret < 0){
	return 3;
	}

	msleep(30);
	
	buffer[0]=0xc4;
	msgs[0].len = 10;
	buffer[1] = 0x5A;
	buffer[2] = 0xA5;
	buffer[3] = 1;
	buffer[4] = 0;
	buffer[5] = 0;
	buffer[6] = 0;
	buffer[7] = 0;
	buffer[8] = 0;
	buffer[9] = 0;
	ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
	if(ret < 0){
	return 3;
	}

	msleep(30);
	
	j=0;
	for(i=0; i < df_len; i+=32){
		j+= 1;
		if((j % 16) == 1){
			msleep(60);
		}
		for(k=0; k<32; k++){
			buffer[1 + k] = CTPM_FW[i + 32 + k];
		}

		buffer[0]=0xc3;
		msgs[0].len = 33;
		ret = ilitek_i2c_transfer(i2c.client, msgs, 1);	
		if(ret < 0){
		return 3;
		}
		upgrade_status = (i * 100) / df_len;
		msleep(10);
		printk("%cILITEK: Firmware Upgrade(Data flash), %02d%c. ",0x0D,upgrade_status,'%');
	}
	
	buffer[0]=0xc4;
	msgs[0].len = 10;
	buffer[1] = 0x5A;
	buffer[2] = 0xA5;
	buffer[3] = 0;
	buffer[4] = CTPM_FW[3];
	buffer[5] = CTPM_FW[4];
	buffer[6] = CTPM_FW[5];
	buffer[7] = CTPM_FW[6];
	buffer[8] = CTPM_FW[7];
	buffer[9] = CTPM_FW[8];
	ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
	if(ret < 0){
		return 3;
	}
	msleep(30);
	
	j=0;
	for(i = 0; i < ap_len; i+=32){
		j+= 1;
		if((j % 16) == 1){
			msleep(60);
		}
		for(k=0; k<32; k++){
			buffer[1 + k] = CTPM_FW[i + df_len + 32 + k];
		}

		buffer[0]=0xc3;
		msgs[0].len = 33;
		ret = ilitek_i2c_transfer(i2c.client, msgs, 1);	
		if(ret < 0){
		return 3;
		}
		upgrade_status = (i * 100) / ap_len;
		msleep(10);
		printk("%cILITEK: Firmware Upgrade(AP), %02d%c. ",0x0D,upgrade_status,'%');
	}
	
	printk("ILITEK:%s, upgrade firmware completed\n", __func__);
	buffer[0]=0xc4;
	msgs[0].len = 1;
	ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
	if(ret < 0){
	return 3;
	}

	msleep(30);
	buffer[0]=0xc1;
	ret = ilitek_i2c_transfer(i2c.client, msgs, 1);
	if(ret < 0){
	return 3;
	}

	buffer[0]=0xc0;
	msgs[0].len = 1;
	ret = ilitek_i2c_read(i2c.client, 0xc0, buffer, 1);
	if(ret < 0){
		return 3;
	}
	msleep(30);
	printk("ic. mode =%d , it's  %s \n",buffer[0],((buffer[0] == 0x5A)?"AP MODE":"BL MODE"));

	msleep(100);
	return 2;
}

/*
description
	 register i2c device and its input device
parameters
	 none
return
	 status
*/
static int ilitek_i2c_register_device(void)
{
	int err;
	 int ret = i2c_add_driver(&ilitek_i2c_driver);
	 
 	 printk("\n@@@func :%s, line :%d", __func__, __LINE__);

	 if(ret == 0){
		 i2c.valid_i2c_register = 1;
		 printk(ILITEK_DEBUG_LEVEL "%s, add i2c device, success\n", __func__);
		 if(i2c.client == NULL){
			 printk(ILITEK_ERROR_LEVEL "%s, no i2c board information\n", __func__);
			 return -1;
		}
		 printk(ILITEK_DEBUG_LEVEL "%s, client.addr: 0x%X\n", __func__, (unsigned int)i2c.client->addr);
		 printk(ILITEK_DEBUG_LEVEL "%s, client.adapter: 0x%X\n", __func__, (unsigned int)i2c.client->adapter);
		 printk(ILITEK_DEBUG_LEVEL "%s, client.driver: 0x%X\n", __func__, (unsigned int)i2c.client->driver);
		 if((i2c.client->addr == 0) || (i2c.client->adapter == 0) || (i2c.client->driver == 0)){
			 printk(ILITEK_ERROR_LEVEL "%s, invalid register\n", __func__);
			 return ret;
		}
		 //register dma
		 I2CDMABuf_va = (u8 *)dma_alloc_coherent(NULL, 16384, &I2CDMABuf_pa, GFP_KERNEL);
		 if(!I2CDMABuf_va)
		 {
			 printk("[TPD] dma_alloc_coherent error\n");
			 return -1;
		 }
		 i2c.client->ext_flag |= I2C_DMA_FLAG;
		  // read touch parameter
		 // read touch parameter
		 ilitek_i2c_read_tp_info();

		 //warren close firmware
     //    ret = ilitek_upgrade_firmware();
      //   if(ret==1)  printk("Do not need update\n"); 
       //  else if(ret==2) printk("update end\n");
        // else if(ret==3) printk("i2c communication error\n");

         mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);
         msleep(50);
         mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
         msleep(100);
        
         ret=ilitek_i2c_read_tp_info();
         if(ret < 0)
         {
             return ret;
         }       

	 // register input device
	#ifdef VELOCITY_CUSTOM_ILI_MTK6589
		#if 0
			if((err = misc_register(&tpd_misc_device)))
				{
					printk("mtk_tpd: tpd_misc_device register failed\n");
				}
		#endif
		i2c.input_dev = tpd->dev;
	#else
		 i2c.input_dev = input_allocate_device();
		 if(i2c.input_dev == NULL){
			 printk(ILITEK_ERROR_LEVEL "%s, allocate input device, error\n", __func__);
			 return -1;
		}
		 ilitek_set_input_param(i2c.input_dev, i2c.max_tp, i2c.max_x, i2c.max_y);
	    	 ret = input_register_device(i2c.input_dev);
	    	 if(ret){
	           		 printk(ILITEK_ERROR_LEVEL "%s, register input device, error\n", __func__);
	            	 return ret;
	    	}
	           	 printk(ILITEK_ERROR_LEVEL "%s, register input device, success\n", __func__);
		 i2c.valid_input_register = 1;
	#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
		 i2c.early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
		 i2c.early_suspend.suspend = ilitek_i2c_early_suspend;
		 i2c.early_suspend.resume = ilitek_i2c_late_resume;
		 register_early_suspend(&i2c.early_suspend);
#endif
		if (1)
		{ // == => polling mode, != => interrup mode
			 i2c.irq_work_queue = create_singlethread_workqueue("ilitek_i2c_irq_queue");
			 if(i2c.irq_work_queue){
				INIT_WORK(&i2c.irq_work, ilitek_i2c_irq_work_queue_func);
				#ifdef CLOCK_INTERRUPT
					//mt_eint_set_sens(CUST_EINT_TOUCH_PANEL_NUM,CUST_EINT_TOUCH_PANEL_SENSITIVE);
					//mt_eint_set_hw_debounce(CUST_EINT_TOUCH_PANEL_NUM,CUST_EINT_TOUCH_PANEL_DEBOUNCE_EN);
					mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT);
					mt_set_gpio_dir(GPIO_CTP_EINT_PIN, GPIO_DIR_IN);
					mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_DISABLE);
					
					//mt_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_POLARITY_LOW, ilitek_i2c_isr, 1);
					mt_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_TYPE, ilitek_i2c_isr, 1);
					mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM); 

					 /*if(request_irq(i2c.client->irq, ilitek_i2c_isr, IRQF_TRIGGER_FALLING , "ilitek_i2c_irq", &i2c)){
						 printk(ILITEK_ERROR_LEVEL "%s, request irq, errorn", __func__);
					}
					 else{
						 i2c.valid_irq_request = 1;
						 i2c.irq_status = 1;
						 printk(ILITEK_ERROR_LEVEL "%s, request irq, successn", __func__);
					}		 */		
				 #else
					 init_timer(&i2c.timer);
					 i2c.timer.data = (unsigned long)&i2c;
					 i2c.timer.function = ilitek_i2c_timer;
					 //mt_eint_set_sens(CUST_EINT_TOUCH_PANEL_NUM,CUST_EINT_TOUCH_PANEL_SENSITIVE);
					 //mt_eint_set_hw_debounce(CUST_EINT_TOUCH_PANEL_NUM,CUST_EINT_TOUCH_PANEL_DEBOUNCE_EN);
					 
					 mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT);
				 	  mt_set_gpio_dir(GPIO_CTP_EINT_PIN, GPIO_DIR_IN);
					 mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_DISABLE);
					 
					 mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM); 
					 //if(request_irq(i2c.client->irq, ilitek_i2c_isr, IRQF_TRIGGER_LOW, "ilitek_i2c_irq", &i2c)){
					mt_eint_registration(CUST_EINT_TOUCH_PANEL_NUM, CUST_EINT_TOUCH_PANEL_TYPE, ilitek_i2c_isr, 1);
						 //printk(ILITEK_ERROR_LEVEL "%s, request irq, errorn", __func__);
					 //}
					 //else{
						 i2c.valid_irq_request = 1;
						 i2c.irq_status = 1;
						 //printk(ILITEK_ERROR_LEVEL "%s, request irq, successn", __func__);
					 //}
					 mt_eint_unmask(CUST_EINT_TOUCH_PANEL_NUM);
				 #endif
			}
		}
	}
	 else{
		 printk(ILITEK_ERROR_LEVEL "%s, add i2c device, error\n", __func__);
		 return ret;
	}
	 return 0;
}

/*
description
	 initiali function for driver to invoke.
parameters

	 nothing
return
	 status
*/
static int ilitek_init(void)
{
	 int ret = 0;

 	 printk("\n@@@func :%s, line :%d", __func__, __LINE__);


	 printk(ILITEK_DEBUG_LEVEL "%s\n", __func__);
	 //warren added
	#if 0
	hwPowerOn(MT65XX_POWER_LDO_VIO28, VOL_2800, "TP");
	#else
	hwPowerOn(MT65XX_POWER_LDO_VIO28, VOL_1800, "TP");
	hwPowerOn(MT65XX_POWER_LDO_VGP1, VOL_2800, "TP");
	
	hwPowerOn(MT6323_POWER_LDO_VGP1, VOL_2800, "TP");

	#endif
	 // initialize global variable
    	 memset(&dev, 0, sizeof(struct dev_data));
    	 memset(&i2c, 0, sizeof(struct i2c_data));

	 // initialize mutex object
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)	
	 init_MUTEX(&i2c.wr_sem);
#else
	 sema_init(&i2c.wr_sem,1);
#endif

	 i2c.wr_sem.count = 1;
	
	 // register i2c device
	 ret = ilitek_i2c_register_device();
	 if(ret < 0){
		 printk(ILITEK_ERROR_LEVEL "%s, register i2c device, error\n", __func__);
		 return ret;
	}

	 // allocate character device driver buffer
	 ret = alloc_chrdev_region(&dev.devno, 0, 1, ILITEK_FILE_DRIVER_NAME);
    	 if(ret){
        	 printk(ILITEK_ERROR_LEVEL "%s, can't allocate chrdev\n", __func__);
		 return ret;
	}
    	 printk(ILITEK_DEBUG_LEVEL "%s, register chrdev(%d, %d)\n", __func__, MAJOR(dev.devno), MINOR(dev.devno));
	
	 // initialize character device driver
	 cdev_init(&dev.cdev, &ilitek_fops);
	 dev.cdev.owner = THIS_MODULE;
    	 ret = cdev_add(&dev.cdev, dev.devno, 1);
    	 if(ret < 0){
        	 printk(ILITEK_ERROR_LEVEL "%s, add character device error, ret %d\n", __func__, ret);
		 return ret;
	}
	 dev.class = class_create(THIS_MODULE, ILITEK_FILE_DRIVER_NAME);
	 if(IS_ERR(dev.class)){
        	 printk(ILITEK_ERROR_LEVEL "%s, create class, error\n", __func__);
		 return ret;
    	}
	 device_create(dev.class, NULL, dev.devno, NULL, "ilitek_ctrl");
	 Report_Flag=0;
	 return 0;
}

/*
description
	 driver exit function
parameters
	 none
return
	 nothing
*/
static void ilitek_exit(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	 unregister_early_suspend(&i2c.early_suspend);
#endif
	 // delete i2c driver
	 if(i2c.client->irq != 0){
        	 if(i2c.valid_irq_request != 0){
                	 free_irq(i2c.client->irq, &i2c);
                	 printk(ILITEK_DEBUG_LEVEL "%s, free irq\n", __func__);
                	 if(i2c.irq_work_queue){
                        	 destroy_workqueue(i2c.irq_work_queue);
                        	 printk(ILITEK_DEBUG_LEVEL "%s, destory work queue\n", __func__);
                	}
        	}
	}
	 else{
        	 if(i2c.thread != NULL){
                	 kthread_stop(i2c.thread);
                	 printk(ILITEK_DEBUG_LEVEL "%s, stop i2c thread\n", __func__);
        	}
	}
        if(i2c.valid_i2c_register != 0){
                i2c_del_driver(&ilitek_i2c_driver);
                printk(ILITEK_DEBUG_LEVEL "%s, delete i2c driver\n", __func__);
        }
        if(i2c.valid_input_register != 0){
                input_unregister_device(i2c.input_dev);
                printk(ILITEK_DEBUG_LEVEL "%s, unregister i2c input device\n", __func__);
        }
        
	 // delete character device driver
	 cdev_del(&dev.cdev);
	 unregister_chrdev_region(dev.devno, 1);
	 device_destroy(dev.class, dev.devno);
	 class_destroy(dev.class);
	 printk(ILITEK_DEBUG_LEVEL "%s\n", __func__);
}

static int tpd_i2c_detect(struct i2c_client *client, struct i2c_board_info *info)
{
    strcpy(info->type, "mtk-tpd");
    return 0;
}

static int tpd_local_init(void) 
{
  printk("\n@@@func :%s, line :%d", __func__, __LINE__);

	  //power on, need confirm with SA
#if 0
   hwPowerOn(MT65XX_POWER_LDO_VGP2, VOL_2800, "TP"); 
	  //hwPowerOn(MT65XX_POWER_LDO_VGP, VOL_1800, "TP"); 
#else
	#if 0
	hwPowerOn(MT65XX_POWER_LDO_VIO28, VOL_2800, "TP");
	#else
	hwPowerOn(MT65XX_POWER_LDO_VIO28, VOL_1800, "TP");
	hwPowerOn(MT65XX_POWER_LDO_VGP1, VOL_2800, "TP");
	hwPowerOn(MT6323_POWER_LDO_VGP1, VOL_2800, "TP");

	#endif
#endif
   msleep(50);
   // set INT mode
   mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_GPIO);
   mt_set_gpio_dir(GPIO_CTP_EINT_PIN, GPIO_DIR_IN);
   mt_set_gpio_pull_enable(GPIO_CTP_EINT_PIN, GPIO_PULL_DISABLE);

   // reset
   mt_set_gpio_mode(GPIO_CTP_RST_PIN, GPIO_CTP_RST_PIN_M_GPIO);
   mt_set_gpio_dir(GPIO_CTP_RST_PIN, GPIO_DIR_OUT);
   mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
   msleep(50);

   mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ZERO);
   msleep(50);
   mt_set_gpio_out(GPIO_CTP_RST_PIN, GPIO_OUT_ONE);
   msleep(100);
   
  if(ilitek_init()!=0)
   {
       TPD_DMESG("unable to add i2c driver.\n");
       return -1;
   }
#ifdef TPD_HAVE_BUTTON     
    tpd_button_setting(TPD_KEY_COUNT, tpd_keys_local, tpd_keys_dim_local);// initialize tpd button data
#endif  
   tpd_load_status = 1;
}

/* Function to manage low power suspend */
//void tpd_suspend(struct i2c_client *client, pm_message_t message)
static void tpd_suspend( struct early_suspend *h )
{
	
}

/* Function to manage power-on resume */
//void tpd_resume(struct i2c_client *client)
static void tpd_resume( struct early_suspend *h )
{
	
}

static struct tpd_driver_t tpd_device_driver =
{
    .tpd_device_name = "ilitek",
    .tpd_local_init = tpd_local_init,
    .suspend = tpd_suspend,
    .resume = tpd_resume,
#ifdef TPD_HAVE_BUTTON
    .tpd_have_button = 1,
#else
    .tpd_have_button = 0,
#endif		
};

/* called when loaded into kernel */
static int __init tpd_driver_init(void)
{
    TPD_DMESG("MediaTek gt818 touch panel driver init\n");
    i2c_register_board_info(0, &i2c_tpd, 1);
    if ( tpd_driver_add(&tpd_device_driver) < 0)
        TPD_DMESG("add generic driver failedn");

    return 0;
}

/* should never be called */
static void __exit tpd_driver_exit(void)
{
    TPD_DMESG("MediaTek ili2389d touch panel driver exit\n");
    //input_unregister_device(tpd->dev);
    tpd_driver_remove(&tpd_device_driver);
}

module_init(tpd_driver_init);
module_exit(tpd_driver_exit);

/* set init and exit function for this module */
//module_init(ilitek_init);
//module_exit(ilitek_exit);


