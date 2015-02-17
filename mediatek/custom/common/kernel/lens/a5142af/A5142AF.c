/*
 * MD218A voice coil motor driver
 *
 *
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>
#include "A5142AF.h"
#include "../camera/kd_camera_hw.h"

#define LENS_I2C_BUSNUM 1
static struct i2c_board_info __initdata kd_lens_dev={ I2C_BOARD_INFO("A5142AF", 0x6c)};


#define A5142AF_DRVNAME "A5142AF"
#define A5142AF_VCM_WRITE_ID           0x6c

#define A5142AF_DEBUG
#ifdef A5142AF_DEBUG
#define A5142AFDB printk
#else
#define A5142AFDB(x,...)
#endif

static spinlock_t g_A5142AF_SpinLock;

static struct i2c_client * g_pstA5142AF_I2Cclient = NULL;

static dev_t g_A5142AF_devno;
static struct cdev * g_pA5142AF_CharDrv = NULL;
static struct class *actuator_class = NULL;

static int  g_s4A5142AF_Opened = 0;
static long g_i4MotorStatus = 0;
static long g_i4Dir = 0;
static unsigned long g_u4A5142AF_INF = 0;
static unsigned long g_u4A5142AF_MACRO = 1023;
static unsigned long g_u4TargetPosition = 0;
static unsigned long g_u4CurrPosition   = 0;

static int g_sr = 3;

#if 0
extern s32 mt_set_gpio_mode(u32 u4Pin, u32 u4Mode);
extern s32 mt_set_gpio_out(u32 u4Pin, u32 u4PinOut);
extern s32 mt_set_gpio_dir(u32 u4Pin, u32 u4Dir);
#endif
extern int iReadReg(u16 a_u2Addr , u8 * a_puBuff , u16 i2cId);
extern int iWriteReg(u16 a_u2Addr , u32 a_u4Data , u32 a_u4Bytes , u16 i2cId);
#define A5142AF_write_cmos_sensor(addr, para) iWriteReg((u16) addr , (u32) para , 1, A5142AF_VCM_WRITE_ID)

kal_uint16 A5142AF_read_cmos_sensor(kal_uint32 addr)
{
kal_uint16 get_byte=0;
    iReadReg((u16) addr ,(u8*)&get_byte,A5142AF_VCM_WRITE_ID);
    return get_byte;
}


static int s4A5142AF_ReadReg(unsigned short * a_pu2Result)
{
    int  temp = 0;
    //char pBuff[2];

    temp = A5142AF_read_cmos_sensor(0x30F2);

    *a_pu2Result = temp*4;
         A5142AFDB("s4AR0542AF_ReadReg = %d \n", temp);
    return 0;
}

static int s4A5142AF_WriteReg(u16 a_u2Data)
{
    s32 temp; 
    A5142AFDB("s4AR0542AF_WriteReg = %d \n", a_u2Data);
                   
         temp = (a_u2Data<<4);   
         // AR0542_write_cmos_sensor(0x30F2, a_u2Data>>2);
    A5142AF_write_cmos_sensor(0x30F2, (temp>>8)&0xff);    
         A5142AF_write_cmos_sensor(0x30F2, temp&0xff);        
         A5142AFDB("s4AR0833AF_WriteReg = %d \n", a_u2Data);

    return 0;
}

inline static int getA5142AFInfo(__user stA5142AF_MotorInfo * pstMotorInfo)
{
    stA5142AF_MotorInfo stMotorInfo;
    stMotorInfo.u4MacroPosition   = g_u4A5142AF_MACRO;
    stMotorInfo.u4InfPosition     = g_u4A5142AF_INF;
    stMotorInfo.u4CurrentPosition = g_u4CurrPosition;
    stMotorInfo.bIsSupportSR      = TRUE;

	if (g_i4MotorStatus == 1)	{stMotorInfo.bIsMotorMoving = 1;}
	else						{stMotorInfo.bIsMotorMoving = 0;}

	if (g_s4A5142AF_Opened >= 1)	{stMotorInfo.bIsMotorOpen = 1;}
	else						{stMotorInfo.bIsMotorOpen = 0;}

    if(copy_to_user(pstMotorInfo , &stMotorInfo , sizeof(stA5142AF_MotorInfo)))
    {
        A5142AFDB("[A5142AF] copy to user failed when getting motor information \n");
    }

    return 0;
}

inline static int moveA5142AF(unsigned long a_u4Position)
{
    int ret = 0;
    
    if((a_u4Position > g_u4A5142AF_MACRO) || (a_u4Position < g_u4A5142AF_INF))
    {
        A5142AFDB("[A5142AF] out of range \n");
        return -EINVAL;
    }

    if (g_s4A5142AF_Opened == 1)
    {
        unsigned short InitPos;
        ret = s4A5142AF_ReadReg(&InitPos);
	    
        spin_lock(&g_A5142AF_SpinLock);
        if(ret == 0)
        {
            A5142AFDB("[A5142AF] Init Pos %6d \n", InitPos);
            g_u4CurrPosition = (unsigned long)InitPos;
        }
        else
        {		
            g_u4CurrPosition = 0;
        }
        g_s4A5142AF_Opened = 2;
        spin_unlock(&g_A5142AF_SpinLock);
    }

    if (g_u4CurrPosition < a_u4Position)
    {
        spin_lock(&g_A5142AF_SpinLock);	
        g_i4Dir = 1;
        spin_unlock(&g_A5142AF_SpinLock);	
    }
    else if (g_u4CurrPosition > a_u4Position)
    {
        spin_lock(&g_A5142AF_SpinLock);	
        g_i4Dir = -1;
        spin_unlock(&g_A5142AF_SpinLock);			
    }
    else										{return 0;}

    spin_lock(&g_A5142AF_SpinLock);    
    g_u4TargetPosition = a_u4Position;
    spin_unlock(&g_A5142AF_SpinLock);	

    //A5142AFDB("[A5142AF] move [curr] %d [target] %d\n", g_u4CurrPosition, g_u4TargetPosition);

            spin_lock(&g_A5142AF_SpinLock);
            g_sr = 3;
            g_i4MotorStatus = 0;
            spin_unlock(&g_A5142AF_SpinLock);	
		
            if(s4A5142AF_WriteReg((unsigned short)g_u4TargetPosition) == 0)
            {
                spin_lock(&g_A5142AF_SpinLock);		
                g_u4CurrPosition = (unsigned long)g_u4TargetPosition;
                spin_unlock(&g_A5142AF_SpinLock);				
            }
            else
            {
                A5142AFDB("[A5142AF] set I2C failed when moving the motor \n");			
                spin_lock(&g_A5142AF_SpinLock);
                g_i4MotorStatus = -1;
                spin_unlock(&g_A5142AF_SpinLock);				
            }

    return 0;
}

inline static int setA5142AFInf(unsigned long a_u4Position)
{
    spin_lock(&g_A5142AF_SpinLock);
    g_u4A5142AF_INF = a_u4Position;
    spin_unlock(&g_A5142AF_SpinLock);	
    return 0;
}

inline static int setA5142AFMacro(unsigned long a_u4Position)
{
    spin_lock(&g_A5142AF_SpinLock);
    g_u4A5142AF_MACRO = a_u4Position;
    spin_unlock(&g_A5142AF_SpinLock);	
    return 0;	
}

////////////////////////////////////////////////////////////////
static long A5142AF_Ioctl(
struct file * a_pstFile,
unsigned int a_u4Command,
unsigned long a_u4Param)
{
    long i4RetValue = 0;

    switch(a_u4Command)
    {
        case A5142AFIOC_G_MOTORINFO :
            i4RetValue = getA5142AFInfo((__user stA5142AF_MotorInfo *)(a_u4Param));
        break;

        case A5142AFIOC_T_MOVETO :
            i4RetValue = moveA5142AF(a_u4Param);
        break;
 
        case A5142AFIOC_T_SETINFPOS :
            i4RetValue = setA5142AFInf(a_u4Param);
        break;

        case A5142AFIOC_T_SETMACROPOS :
            i4RetValue = setA5142AFMacro(a_u4Param);
        break;
		
        default :
      	    A5142AFDB("[A5142AF] No CMD \n");
            i4RetValue = -EPERM;
        break;
    }

    return i4RetValue;
}

//Main jobs:
// 1.check for device-specified errors, device not ready.
// 2.Initialize the device if it is opened for the first time.
// 3.Update f_op pointer.
// 4.Fill data structures into private_data
//CAM_RESET
static int A5142AF_Open(struct inode * a_pstInode, struct file * a_pstFile)
{
    A5142AFDB("[A5142AF] A5142AF_Open - Start\n");

    spin_lock(&g_A5142AF_SpinLock);

    if(g_s4A5142AF_Opened)
    {
        spin_unlock(&g_A5142AF_SpinLock);
        A5142AFDB("[A5142AF] the device is opened \n");
        return -EBUSY;
    }

    g_s4A5142AF_Opened = 1;
		
    spin_unlock(&g_A5142AF_SpinLock);

    A5142AFDB("[A5142AF] A5142AF_Open - End\n");

    return 0;
}

//Main jobs:
// 1.Deallocate anything that "open" allocated in private_data.
// 2.Shut down the device on last close.
// 3.Only called once on last time.
// Q1 : Try release multiple times.
static int A5142AF_Release(struct inode * a_pstInode, struct file * a_pstFile)
{
    A5142AFDB("[A5142AF] A5142AF_Release - Start\n");

    if (g_s4A5142AF_Opened)
    {
        A5142AFDB("[A5142AF] feee \n");
        g_sr = 5;
	    s4A5142AF_WriteReg(200);
        msleep(10);
	    s4A5142AF_WriteReg(100);
        msleep(10);
            	            	    	    
        spin_lock(&g_A5142AF_SpinLock);
        g_s4A5142AF_Opened = 0;
        spin_unlock(&g_A5142AF_SpinLock);

    }

    A5142AFDB("[A5142AF] A5142AF_Release - End\n");

    return 0;
}

static const struct file_operations g_stA5142AF_fops = 
{
    .owner = THIS_MODULE,
    .open = A5142AF_Open,
    .release = A5142AF_Release,
    .unlocked_ioctl = A5142AF_Ioctl
};

inline static int Register_A5142AF_CharDrv(void)
{
    struct device* vcm_device = NULL;
	printk("sym [A5142AF] Register_A5142AF_CharDrv - Start\n");

    A5142AFDB("[A5142AF] Register_A5142AF_CharDrv - Start\n");

    //Allocate char driver no.
    if( alloc_chrdev_region(&g_A5142AF_devno, 0, 1,A5142AF_DRVNAME) )
    {
        A5142AFDB("[A5142AF] Allocate device no failed\n");

        return -EAGAIN;
    }

    //Allocate driver
    g_pA5142AF_CharDrv = cdev_alloc();

    if(NULL == g_pA5142AF_CharDrv)
    {
        unregister_chrdev_region(g_A5142AF_devno, 1);

        A5142AFDB("[A5142AF] Allocate mem for kobject failed\n");

        return -ENOMEM;
    }

    //Attatch file operation.
    cdev_init(g_pA5142AF_CharDrv, &g_stA5142AF_fops);

    g_pA5142AF_CharDrv->owner = THIS_MODULE;

    //Add to system
    if(cdev_add(g_pA5142AF_CharDrv, g_A5142AF_devno, 1))
    {
        A5142AFDB("[A5142AF] Attatch file operation failed\n");

        unregister_chrdev_region(g_A5142AF_devno, 1);

        return -EAGAIN;
    }

    actuator_class = class_create(THIS_MODULE, "actuatordrv");
    if (IS_ERR(actuator_class)) {
        int ret = PTR_ERR(actuator_class);
        A5142AFDB("Unable to create class, err = %d\n", ret);
        return ret;            
    }

    vcm_device = device_create(actuator_class, NULL, g_A5142AF_devno, NULL, A5142AF_DRVNAME);

    if(NULL == vcm_device)
    {
        return -EIO;
    }
    	printk("sym [A5142AF] Register_A5142AF_CharDrv - End\n");
    A5142AFDB("[A5142AF] Register_A5142AF_CharDrv - End\n");    
    return 0;
}

inline static void Unregister_A5142AF_CharDrv(void)
{
    A5142AFDB("[A5142AF] Unregister_A5142AF_CharDrv - Start\n");

    //Release char driver
    cdev_del(g_pA5142AF_CharDrv);

    unregister_chrdev_region(g_A5142AF_devno, 1);
    
    device_destroy(actuator_class, g_A5142AF_devno);

    class_destroy(actuator_class);

    A5142AFDB("[A5142AF] Unregister_A5142AF_CharDrv - End\n");    
}

//////////////////////////////////////////////////////////////////////

static int A5142AF_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int A5142AF_i2c_remove(struct i2c_client *client);
static const struct i2c_device_id A5142AF_i2c_id[] = {{A5142AF_DRVNAME,0},{}};   
struct i2c_driver A5142AF_i2c_driver = {                       
    .probe = A5142AF_i2c_probe,                                   
    .remove = A5142AF_i2c_remove,                           
    .driver.name = A5142AF_DRVNAME,                 
    .id_table = A5142AF_i2c_id,                             
};  

#if 0 
static int A5142AF_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info) {         
    strcpy(info->type, A5142AF_DRVNAME);                                                         
    return 0;                                                                                       
}      
#endif 
static int A5142AF_i2c_remove(struct i2c_client *client) {
    return 0;
}

/* Kirby: add new-style driver {*/
static int A5142AF_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int i4RetValue = 0;
printk("sym [A5142AF] A5142AF_i2c_probe\n");
    A5142AFDB("[A5142AF] A5142AF_i2c_probe\n");

    /* Kirby: add new-style driver { */
    g_pstA5142AF_I2Cclient = client;
    
    g_pstA5142AF_I2Cclient->addr = g_pstA5142AF_I2Cclient->addr >> 1;
    
    //Register char driver
    i4RetValue = Register_A5142AF_CharDrv();

    if(i4RetValue){

        A5142AFDB("[A5142AF] register char device failed!\n");

        return i4RetValue;
    }

    spin_lock_init(&g_A5142AF_SpinLock);

    A5142AFDB("[A5142AF] Attached!! \n");

    return 0;
}

static int A5142AF_probe(struct platform_device *pdev)
{
    return i2c_add_driver(&A5142AF_i2c_driver);
}

static int A5142AF_remove(struct platform_device *pdev)
{
    i2c_del_driver(&A5142AF_i2c_driver);
    return 0;
}

static int A5142AF_suspend(struct platform_device *pdev, pm_message_t mesg)
{
    return 0;
}

static int A5142AF_resume(struct platform_device *pdev)
{
    return 0;
}

// platform structure
static struct platform_driver g_stA5142AF_Driver = {
    .probe		= A5142AF_probe,
    .remove	= A5142AF_remove,
    .suspend	= A5142AF_suspend,
    .resume	= A5142AF_resume,
    .driver		= {
        .name	= "lens_actuator",
        .owner	= THIS_MODULE,
    }
};

static int __init A5142AF_i2C_init(void)
{
printk("sym [A5142AF] A5142AF_i2C_init\n");
    i2c_register_board_info(LENS_I2C_BUSNUM, &kd_lens_dev, 1);
	
    if(platform_driver_register(&g_stA5142AF_Driver)){
        A5142AFDB("failed to register A5142AF driver\n");
        return -ENODEV;
    }

    return 0;
}

static void __exit A5142AF_i2C_exit(void)
{
	platform_driver_unregister(&g_stA5142AF_Driver);
}

module_init(A5142AF_i2C_init);
module_exit(A5142AF_i2C_exit);

MODULE_DESCRIPTION("A5142AF lens module driver");
MODULE_AUTHOR("KY Chen <ky.chen@Mediatek.com>");
MODULE_LICENSE("GPL");


