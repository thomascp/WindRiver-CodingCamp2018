/* usrAppInit.c - stub application initialization routine */

/* Copyright (c) 1998,2006,2011, 2014 Wind River Systems, Inc. 
 *
 * The right to copy, distribute, modify or otherwise make use
 * of this software may be licensed only pursuant to the terms
 * of an applicable Wind River license agreement.
 */

/*
modification history
--------------------
07oct14,jpb  Added TODO flag.
26may11,pcs  Add stubs corresponding to the boot sequence hook callout
             mechanism.
16mar06,jmt  Add header file to find USER_APPL_INIT define
02jun98,ms   written
*/

/*
DESCRIPTION
Initialize user application code.
*/ 

#include <vxWorks.h>
#if defined(PRJ_BUILD)
#include "prjParams.h"
#endif /* defined PRJ_BUILD */

#include <string.h>
#include <semLib.h>
#include <taskLib.h>
#include <wdLib.h>
#include <fcntl.h>
#include <ioLib.h>
#include <sys/time.h>
#include <subsys/timer/vxbTimerLib.h>
#include <hwif/vxBus.h>
#include <hwif/buslib/vxbFdtLib.h>
#include <hwif/vxbus/vxbBusType.h>
#include <subsys/int/vxbIntLib.h>
#include <subsys/timer/vxbTimerLib.h>
#include <subsys/gpio/vxbGpioLib.h>
#include <subsys/pinmux/vxbPinMuxLib.h>

/******************************************************************************
*
* usrAppInit - initialize the users application
*/ 

void usrAppInit (void)
    {
#ifdef	USER_APPL_INIT
	USER_APPL_INIT;		/* for backwards compatibility */
#endif

    /* TODO: add application specific code here */
    extern TASK_ID taskIdFigure (long taskNameOrId);

    taskSuspend (taskIdFigure ((long)"tShell0"));
    }

#ifdef INCLUDE_USER_PRE_KERNEL_APPL_INIT
/******************************************************************************
*
* usrPreKernelAppInit - initialize the users pre-kernel application code
*/ 

void usrPreKernelAppInit (void)
    {

    /*
     * Add application specific code here.
     * No kernel feature is available at this point.
     */

    }
#endif

#ifdef INCLUDE_USER_POST_KERNEL_APPL_INIT
/******************************************************************************
*
* usrPostKernelAppInit - initialize the users post-kernel application code
*/ 

void usrPostKernelAppInit (void)
    {

    /*
     * Add application specific code here.
     * Core kernel feature is available at this point.
     * IO system and Network is not available.
     * kernel features dependent on system clock or timer not available.
     */

    }
#endif


#ifdef INCLUDE_USER_PRE_NETWORK_APPL_INIT
/******************************************************************************
*
* usrPreNetworkAppInit - initialize the users pre-network application code
*/ 

void usrPreNetworkAppInit (void)
    {

    /*
     * Add application specific code here.
     * Core kernel feature and IO system is available at this point.
     * Network is not available.
     */

    }
#endif

typedef struct cc_control
    {
    VXB_DEV_ID  pDev;
    SEM_ID      ccSem;
    int         snrPin;
    int         redPin;
    int         grnPin;

    WDOG_ID     wdId;
    int         shutPin;
    }   CC_CONTROL;

LOCAL STATUS                ccControlProbe (VXB_DEV_ID pDev);
LOCAL STATUS                ccControlAttach (VXB_DEV_ID pDev);
LOCAL void                  vxbFdtCcConGpioIsr (CC_CONTROL *  pDrvCtrl);
LOCAL void                  vxbFdtCcConGpioTask (CC_CONTROL *  pDrvCtrl);


LOCAL VXB_DRV_METHOD ccConMethodList[] =
    {
    { VXB_DEVMETHOD_CALL(vxbDevProbe),          (FUNCPTR)ccControlProbe},
    { VXB_DEVMETHOD_CALL(vxbDevAttach),         (FUNCPTR)ccControlAttach},
    { 0, NULL }
    };

LOCAL VXB_FDT_DEV_MATCH_ENTRY ccConMatch[] =
    {
        {
        "wr,cccontrol",                      /* compatible */
        NULL      
        },
        {}                                   /* empty terminated list */
    };

/* globals */

VXB_DRV vxbFdtccConDrv =
    {
    {NULL} ,
    "wr,cccontrol",                         /* Name */
    "Wind River Code Camp Control Module",  /* Description */
    VXB_BUSID_FDT,                          /* Class */
    0,                                      /* Flags */
    0,                                      /* Reference count */
    ccConMethodList                         /* Method table */
    };

VXB_DRV_DEF(vxbFdtccConDrv)

LOCAL STATUS ccControlProbe (VXB_DEV_ID pDev)
    {
    return (vxbFdtDevMatch (pDev, &ccConMatch[0], NULL));
    }

LOCAL STATUS ccControlAttach (VXB_DEV_ID pDev)
    {
    VXB_FDT_DEV *               pFdtDev = NULL;
    CC_CONTROL  *               pDrvCtrl = NULL;

    
    pFdtDev = vxbFdtDevGet (pDev);
    if (NULL == pFdtDev)
        {
        return ERROR;
        }

    pDrvCtrl = (CC_CONTROL *)vxbMemAlloc (sizeof (CC_CONTROL));
    if (NULL == pDrvCtrl)
        {
        return ERROR;
        }

    /* set pinmux */ 
        
    (void) vxbPinMuxEnable(pDev);

    pDrvCtrl->pDev = pDev;

    pDrvCtrl->snrPin   = vxbGpioGetByFdtIndex (pDev, "sensor-in", 0);
    pDrvCtrl->redPin   = vxbGpioGetByFdtIndex (pDev, "led-red", 0);
    pDrvCtrl->grnPin   = vxbGpioGetByFdtIndex (pDev, "led-green", 0);
    if ((pDrvCtrl->snrPin == (int)ERROR) || (pDrvCtrl->redPin == (int)ERROR) ||
        (pDrvCtrl->grnPin == (int)ERROR))
        {
        vxbMemFree (pDrvCtrl);
        return ERROR;
        }

    (void)vxbGpioAlloc (pDrvCtrl->snrPin);
    (void)vxbGpioSetDir (pDrvCtrl->snrPin, GPIO_DIR_INPUT);
    (void)vxbGpioIntConfig (pDrvCtrl->snrPin, INTR_TRIGGER_EDGE, 
                            INTR_POLARITY_HIGH);
    (void)vxbGpioAlloc (pDrvCtrl->redPin);
    (void)vxbGpioSetDir (pDrvCtrl->redPin, GPIO_DIR_OUTPUT);
    (void)vxbGpioAlloc (pDrvCtrl->grnPin);
    (void)vxbGpioSetDir (pDrvCtrl->grnPin, GPIO_DIR_OUTPUT);

    (void) vxbGpioSetValue (pDrvCtrl->redPin, GPIO_VALUE_LOW);
    (void) vxbGpioSetValue (pDrvCtrl->grnPin, GPIO_VALUE_LOW);

    pDrvCtrl->ccSem = semBCreate (SEM_Q_FIFO, SEM_EMPTY);

    (void)taskSpawn ("tCCConTask", 100, VX_FP_TASK, 0x1000, 
                     (FUNCPTR)vxbFdtCcConGpioTask,
                     (_Vx_usr_arg_t)pDrvCtrl, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    
    (void) vxbGpioIntConnect (pDrvCtrl->snrPin, vxbFdtCcConGpioIsr, 
                              (void *)pDrvCtrl);
    (void) vxbGpioIntEnable (pDrvCtrl->snrPin, vxbFdtCcConGpioIsr, pDrvCtrl);

    pDrvCtrl->wdId = wdCreate ();

    return OK;
    }

LOCAL void vxbFdtCcConGpioIsr
    (
    CC_CONTROL *  pDrvCtrl     /* pointer to the device specific data */
    )
    {
    /* disable interrupt */

    (void) vxbGpioIntDisable (pDrvCtrl->snrPin,
                              vxbFdtCcConGpioIsr, pDrvCtrl);

    (void) semGive (pDrvCtrl->ccSem);
    }

LOCAL void ccConWdRtn (CC_CONTROL *  pDrvCtrl)
    {
    (void) vxbGpioSetValue (pDrvCtrl->shutPin, GPIO_VALUE_LOW);
    }

char ccRxC = 0;
LOCAL void vxbFdtCcConGpioTask (CC_CONTROL *  pDrvCtrl)
    {
    UINT32  led = GPIO_VALUE_HIGH;
    int     serFd = (int)ERROR;
    fd_set  readFds;
    struct timeval timeO;
    char    rdBuf[4];

    while (serFd == (int)ERROR)
        {
        serFd = open ("/tyCo/0", O_RDWR, 0666);
        }

    while (1)
        {
        semTake (pDrvCtrl->ccSem, WAIT_FOREVER);

        ioctl (serFd, FIORFLUSH, 0);
        write (serFd, "D", 1);

        FD_ZERO (&readFds);
        FD_SET (serFd, &readFds);
        timeO.tv_sec = 8;
        timeO.tv_usec = 0;
        select (serFd + 1,  &readFds, NULL, NULL, &timeO);

        rdBuf[0] = 'F';
        if (FD_ISSET (serFd, &readFds))
            {
            read (serFd, rdBuf, 1);
            ccRxC = rdBuf[0];
            }

        if (rdBuf[0] == 'T')
            {
            (void) vxbGpioSetValue (pDrvCtrl->grnPin, GPIO_VALUE_HIGH);
            (void) vxbGpioSetValue (pDrvCtrl->redPin, GPIO_VALUE_LOW);
            pDrvCtrl->shutPin = pDrvCtrl->grnPin;
            }
        else
            {
            (void) vxbGpioSetValue (pDrvCtrl->redPin, GPIO_VALUE_HIGH);
            (void) vxbGpioSetValue (pDrvCtrl->grnPin, GPIO_VALUE_LOW);
            pDrvCtrl->shutPin = pDrvCtrl->redPin;
            }

        wdStart (pDrvCtrl->wdId, vxbSysClkRateGet () * 2, (FUNCPTR)ccConWdRtn,
                     (_Vx_usr_arg_t)pDrvCtrl);

        (void) vxbGpioIntEnable (pDrvCtrl->snrPin, vxbFdtCcConGpioIsr, pDrvCtrl);
        }
    }

