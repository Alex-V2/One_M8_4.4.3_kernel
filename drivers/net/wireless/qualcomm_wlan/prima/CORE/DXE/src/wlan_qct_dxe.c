/*
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */


/**=========================================================================
  
  @file  wlan_qct_dxe.c
  
  @brief 
               
   This file contains the external API exposed by the wlan data transfer abstraction layer module.
   Copyright (c) 2010-2011 Qualcomm Technologies, Inc.
   All Rights Reserved.
========================================================================*/



#include "wlan_qct_dxe.h"
#include "wlan_qct_dxe_i.h"
#include "wlan_qct_pal_device.h"
#ifdef FEATURE_R33D
#include "wlan_qct_pal_bus.h"
#endif 


#define T_WLANDXE_MAX_DESCRIPTOR_COUNT     40
#define T_WLANDXE_MAX_FRAME_SIZE           2000
#define T_WLANDXE_TX_INT_ENABLE_FCOUNT     1
#define T_WLANDXE_MEMDUMP_BYTE_PER_LINE    16
#define T_WLANDXE_MAX_RX_PACKET_WAIT       6000
#define T_WLANDXE_SSR_TIMEOUT              5000
#define T_WLANDXE_PERIODIC_HEALTH_M_TIME   2500
#define T_WLANDXE_MAX_HW_ACCESS_WAIT       2000
#define WLANDXE_MAX_REAPED_RX_FRAMES       512

#define WLANPAL_RX_INTERRUPT_PRO_MASK      0x20
#define WLANDXE_RX_INTERRUPT_PRO_UNMASK    0x5F

#define WLANDXE_CSR_NEXT_READ_WAIT         1000
#define WLANDXE_CSR_MAX_READ_COUNT         30


#define WDI_GET_PAL_CTX()                  NULL


static WLANDXE_CtrlBlkType    *tempDxeCtrlBlk;
static char                   *channelType[WDTS_CHANNEL_MAX] =
   {
      "TX_LOW_PRI",
      "TX_HIGH_PRI",
      "RX_LOW_PRI",
#ifndef WLANDXE_TEST_CHANNEL_ENABLE
      "RX_HIGH_PRI",
#else
      "H2H_TEST_TX",
      "H2H_TEST_RX"
#endif 
   };
static  wpt_packet               *rx_reaped_buf[WLANDXE_MAX_REAPED_RX_FRAMES];


static wpt_status dxeRXFrameSingleBufferAlloc
(
   WLANDXE_CtrlBlkType      *dxeCtxt,
   WLANDXE_ChannelCBType    *channelEntry,
   WLANDXE_DescCtrlBlkType  *currentCtrlBlock
);

static wpt_status dxeNotifySmsm
(
  wpt_boolean kickDxe,
  wpt_boolean ringEmpty
);

static void dxeStartSSRTimer
(
  WLANDXE_CtrlBlkType     *dxeCtxt
);

static wpt_status dxeChannelMonitor
(
   char                    *monitorDescription,
   WLANDXE_ChannelCBType   *channelEntry,
   wpt_log_data_stall_channel_type *channelLog
)
{
   wpt_status                status = eWLAN_PAL_STATUS_SUCCESS;

   if((NULL == monitorDescription) || (NULL == channelEntry))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "INVALID Input ARG");
      return eWLAN_PAL_STATUS_E_INVAL;
   }

   if(channelEntry->channelType > WDTS_CHANNEL_RX_HIGH_PRI)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "INVALID Channel type");
      return eWLAN_PAL_STATUS_E_INVAL;
   }

   wpalTrace(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
             "%11s : HCBO %d, HCBDP 0x%x, HCBDC 0x%x,",
             channelType[channelEntry->channelType],
             channelEntry->headCtrlBlk->ctrlBlkOrder,
             channelEntry->headCtrlBlk->linkedDescPhyAddr,
             channelEntry->headCtrlBlk->linkedDesc->descCtrl.ctrl);
   wpalTrace(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
             "%11s : TCBO %d, TCBDP 0x%x, TCBDC 0x%x",
             channelType[channelEntry->channelType],
             channelEntry->tailCtrlBlk->ctrlBlkOrder,
             channelEntry->tailCtrlBlk->linkedDescPhyAddr,
             channelEntry->tailCtrlBlk->linkedDesc->descCtrl.ctrl);
   wpalTrace(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
             "%11s : FDC %d, RDC %d, TFC %d",
             channelType[channelEntry->channelType],
             channelEntry->numFreeDesc,
             channelEntry->numRsvdDesc,
             channelEntry->numTotalFrame);

   if(channelLog)
   {
      channelLog->numDesc       = channelEntry->numDesc;
      channelLog->numFreeDesc   = channelEntry->numFreeDesc;
      channelLog->numRsvdDesc   = channelEntry->numRsvdDesc;
      channelLog->headDescOrder = channelEntry->headCtrlBlk->ctrlBlkOrder;
      channelLog->tailDescOrder = channelEntry->tailCtrlBlk->ctrlBlkOrder;
   }

   return status;
}

#ifdef WLANDXE_DEBUG_MEMORY_DUMP
static wpt_status dxeMemoryDump
(
   wpt_uint8    *dumpPointer,
   wpt_uint32    dumpSize,
   char         *dumpTarget
)
{
   wpt_status                status      = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                numBytes    = 0;
   wpt_uint32                idx;

   if((NULL == dumpPointer) ||
      (NULL == dumpTarget))
   {
      return status;
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Location 0x%x, Size %d", dumpTarget, dumpPointer, dumpSize);

   numBytes = dumpSize % T_WLANDXE_MEMDUMP_BYTE_PER_LINE;
   for(idx = 0; idx < dumpSize; idx++)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
               "0x%2x ", dumpPointer[idx]);
      if(0 == ((idx + 1) % T_WLANDXE_MEMDUMP_BYTE_PER_LINE))
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW, "\n");
      }
   }
   if(0 != numBytes)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW, "\n");
   }
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");

   return status;
}
#endif 

wpt_status dxeDescriptorDump
(
   WLANDXE_ChannelCBType   *channelEntry,
   WLANDXE_DescType        *targetDesc,
   wpt_uint32               fragmentOrder
)
{
   wpt_status                status      = eWLAN_PAL_STATUS_SUCCESS;


   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
            "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
            "Descriptor Dump for channel %s, %d / %d fragment",
                   channelType[channelEntry->channelType],
                   fragmentOrder + 1,
                   channelEntry->numFragmentCurrentChain);

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
            "CTRL WORD 0x%x, TransferSize %d",
                   WLANDXE_U32_SWAP_ENDIAN(targetDesc->descCtrl.ctrl),
            WLANDXE_U32_SWAP_ENDIAN(targetDesc->xfrSize));
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
            "SRC ADD 0x%x, DST ADD 0x%x, NEXT DESC 0x%x",
                   WLANDXE_U32_SWAP_ENDIAN(targetDesc->dxedesc.dxe_short_desc.srcMemAddrL),
                   WLANDXE_U32_SWAP_ENDIAN(targetDesc->dxedesc.dxe_short_desc.dstMemAddrL),
                   WLANDXE_U32_SWAP_ENDIAN(targetDesc->dxedesc.dxe_short_desc.phyNextL));
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
            "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");

   return status;
}

wpt_status dxeChannelRegisterDump
(
   WLANDXE_ChannelCBType   *channelEntry,
   char                    *dumpTarget,
   wpt_log_data_stall_channel_type *channelLog
)
{
   wpt_status   status      = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32   chStatusReg, chControlReg, chDescReg, chLDescReg;

   dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);
   dxeNotifySmsm(eWLAN_PAL_TRUE, eWLAN_PAL_FALSE);
   wpalSleep(10);

   if(channelEntry->channelType > WDTS_CHANNEL_RX_HIGH_PRI)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "INVALID Channel type");
      return eWLAN_PAL_STATUS_E_INVAL;
   }

   wpalReadRegister(channelEntry->channelRegister.chDXEDesclRegAddr, &chDescReg);
   wpalReadRegister(channelEntry->channelRegister.chDXELstDesclRegAddr, &chLDescReg);
   wpalReadRegister(channelEntry->channelRegister.chDXECtrlRegAddr, &chControlReg);
   wpalReadRegister(channelEntry->channelRegister.chDXEStatusRegAddr, &chStatusReg);

   wpalTrace(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
             "%11s : CCR 0x%x, CSR 0x%x, CDR 0x%x, CLDR 0x%x",
             channelType[channelEntry->channelType],
             chControlReg, chStatusReg, chDescReg, chLDescReg);

   if(channelLog)
   {
      channelLog->ctrlRegVal = chControlReg;
      channelLog->statRegVal = chStatusReg;
   }

   return status;
}

void dxeChannelAllDescDump
(
   WLANDXE_ChannelCBType   *channelEntry,
   WDTS_ChannelType         channel,
   wpt_log_data_stall_channel_type *channelLog
)
{
   wpt_uint32               channelLoop;
   WLANDXE_DescCtrlBlkType *targetCtrlBlk;
   wpt_uint32               previousCtrlValue = 0;
   wpt_uint32               previousCtrlValid = 0;
   wpt_uint32               currentCtrlValid = 0;
   wpt_uint32               valDescCount = 0;
   wpt_uint32               invalDescCount = 0;

   targetCtrlBlk = channelEntry->headCtrlBlk;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
            "%11s : %d descriptor chains, head desc ctrl 0x%x",
            channelType[channelEntry->channelType],
            channelEntry->numDesc,
            targetCtrlBlk->linkedDesc->descCtrl.ctrl);
   previousCtrlValue = targetCtrlBlk->linkedDesc->descCtrl.ctrl;

   if((WDTS_CHANNEL_RX_LOW_PRI == channel) ||
      (WDTS_CHANNEL_RX_HIGH_PRI == channel))
   {
      for(channelLoop = 0; channelLoop < channelEntry->numDesc; channelLoop++)
      {
         if(previousCtrlValue != targetCtrlBlk->linkedDesc->descCtrl.ctrl)
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                     "%5d : 0x%x", targetCtrlBlk->ctrlBlkOrder,
                     targetCtrlBlk->linkedDesc->descCtrl.ctrl);
         }
         if(targetCtrlBlk->linkedDesc->descCtrl.ctrl & WLANDXE_DESC_CTRL_VALID)
         {
            valDescCount++;
         }
         else
         {
            invalDescCount++;
         }
         previousCtrlValue = targetCtrlBlk->linkedDesc->descCtrl.ctrl;
         targetCtrlBlk = (WLANDXE_DescCtrlBlkType *)targetCtrlBlk->nextCtrlBlk;
      }
   }
   else
   {
      
      previousCtrlValid = targetCtrlBlk->linkedDesc->descCtrl.ctrl & WLANDXE_DESC_CTRL_VALID;
      targetCtrlBlk = (WLANDXE_DescCtrlBlkType *)targetCtrlBlk->nextCtrlBlk;
      for(channelLoop = 0; channelLoop < channelEntry->numDesc; channelLoop++)
      {
         currentCtrlValid = targetCtrlBlk->linkedDesc->descCtrl.ctrl & WLANDXE_DESC_CTRL_VALID;
         if(currentCtrlValid)
         {
            valDescCount++;
         }
         else
         {
            invalDescCount++;
         }
         if(currentCtrlValid != previousCtrlValid)
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                     "%5d : 0x%x", targetCtrlBlk->ctrlBlkOrder,
                     targetCtrlBlk->linkedDesc->descCtrl.ctrl);
         }
         previousCtrlValid = currentCtrlValid;
         targetCtrlBlk = (WLANDXE_DescCtrlBlkType *)targetCtrlBlk->nextCtrlBlk;
      }
   }

   if(channelLog)
   {
      channelLog->numValDesc   = valDescCount;
      channelLog->numInvalDesc = invalDescCount;
   }

   return;
}

void dxeErrChannelDebug
(
    WLANDXE_ChannelCBType    *channelCb,
    wpt_uint32                chStatusReg
)
{
   wpt_log_data_stall_channel_type channelLog;
   wpt_uint32 chLDescReg, channelLoop;
   WLANDXE_DescCtrlBlkType *targetCtrlBlk;

   dxeChannelMonitor("INT_ERR", channelCb, &channelLog);
   dxeDescriptorDump(channelCb, channelCb->headCtrlBlk->linkedDesc, 0);
   dxeChannelRegisterDump(channelCb, "INT_ERR", &channelLog);
   dxeChannelAllDescDump(channelCb, channelCb->channelType, &channelLog);
   wpalMemoryCopy(channelLog.channelName,
                  "INT_ERR",
                  WPT_TRPT_CHANNEL_NAME);
   wpalPacketStallUpdateInfo(NULL, NULL, &channelLog, channelCb->channelType);
#ifdef FEATURE_WLAN_DIAG_SUPPORT
   wpalPacketStallDumpLog();
#endif 
   switch ((chStatusReg & WLANDXE_CH_STAT_ERR_CODE_MASK) >>
            WLANDXE_CH_STAT_ERR_CODE_OFFSET)
   {

      case WLANDXE_ERROR_PRG_INV_B2H_SRC_QID:
      case WLANDXE_ERROR_PRG_INV_B2H_DST_QID:
      case WLANDXE_ERROR_PRG_INV_B2H_SRC_IDX:
      case WLANDXE_ERROR_PRG_INV_H2B_SRC_QID:
      case WLANDXE_ERROR_PRG_INV_H2B_DST_QID:
      case WLANDXE_ERROR_PRG_INV_H2B_DST_IDX:
      {
         dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);
         dxeNotifySmsm(eWLAN_PAL_TRUE, eWLAN_PAL_FALSE);
         wpalSleep(10);

         if(channelCb->channelType > WDTS_CHANNEL_RX_HIGH_PRI)
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                     "%s: Invalid Channel", __func__);
            break;
         }

         wpalReadRegister(channelCb->channelRegister.chDXELstDesclRegAddr, &chLDescReg);

         targetCtrlBlk = channelCb->headCtrlBlk;

         for(channelLoop = 0; channelLoop < channelCb->numDesc; channelLoop++)
         {
            if (targetCtrlBlk->linkedDescPhyAddr == chLDescReg)
            {
                HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                         "%11s :CHx_DESCL: desc ctrl 0x%x, src 0x%x, dst 0x%x, next 0x%x",
                         channelType[channelCb->channelType],
                         targetCtrlBlk->linkedDesc->descCtrl.ctrl,
                         targetCtrlBlk->linkedDesc->dxedesc.dxe_short_desc.srcMemAddrL,
                         targetCtrlBlk->linkedDesc->dxedesc.dxe_short_desc.dstMemAddrL,
                         targetCtrlBlk->linkedDesc->dxedesc.dxe_short_desc.phyNextL);

                targetCtrlBlk = (WLANDXE_DescCtrlBlkType *)targetCtrlBlk->nextCtrlBlk;

                HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                         "%11s :Next Desc: desc ctrl 0x%x, src 0x%x, dst 0x%x, next 0x%x",
                         channelType[channelCb->channelType],
                         targetCtrlBlk->linkedDesc->descCtrl.ctrl,
                         targetCtrlBlk->linkedDesc->dxedesc.dxe_short_desc.srcMemAddrL,
                         targetCtrlBlk->linkedDesc->dxedesc.dxe_short_desc.dstMemAddrL,
                         targetCtrlBlk->linkedDesc->dxedesc.dxe_short_desc.phyNextL);
                break;
            }
            targetCtrlBlk = (WLANDXE_DescCtrlBlkType *)targetCtrlBlk->nextCtrlBlk;
         }
         break;
      }
      default:
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                  "%s: No Debug Inormation", __func__);
         break;
      }

   }
   wpalFwDumpReq(17, 0, 0, 0, 0);
}
void dxeTxThreadChannelDebugHandler
(
    wpt_msg               *msgPtr
)
{
   wpt_uint8                channelLoop;
   wpt_log_data_stall_channel_type channelLog;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   for(channelLoop = 0; channelLoop < WDTS_CHANNEL_RX_LOW_PRI; channelLoop++)
   {
      dxeChannelMonitor("******** Get Descriptor Snapshot ",
                        &tempDxeCtrlBlk->dxeChannel[channelLoop],
                        &channelLog);
      dxeChannelRegisterDump(&tempDxeCtrlBlk->dxeChannel[channelLoop],
                             "Abnormal successive empty interrupt",
                             &channelLog);
      dxeChannelAllDescDump(&tempDxeCtrlBlk->dxeChannel[channelLoop],
                            channelLoop,
                            &channelLog);

      wpalMemoryCopy(channelLog.channelName,
                     channelType[channelLoop],
                     WPT_TRPT_CHANNEL_NAME);
      wpalPacketStallUpdateInfo(NULL, NULL, &channelLog, channelLoop);
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
            "================== DXE Dump End ======================",
             tempDxeCtrlBlk->hostPowerState, tempDxeCtrlBlk->rivaPowerState);
   wpalMemoryFree(msgPtr);

#ifdef FEATURE_WLAN_DIAG_SUPPORT
   wpalPacketStallDumpLog();
#endif 
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return;
}

void dxeRxThreadChannelDebugHandler
(
    wpt_msg               *msgPtr
)
{
   wpt_status               status = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint8                channelLoop;
   wpt_log_data_stall_channel_type channelLog;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   for(channelLoop = WDTS_CHANNEL_RX_LOW_PRI; channelLoop < WDTS_CHANNEL_MAX; channelLoop++)
   {
      dxeChannelMonitor("******** Get Descriptor Snapshot ",
                        &tempDxeCtrlBlk->dxeChannel[channelLoop],
                        &channelLog);
      dxeChannelRegisterDump(&tempDxeCtrlBlk->dxeChannel[channelLoop],
                             "Abnormal successive empty interrupt",
                             &channelLog);
      dxeChannelAllDescDump(&tempDxeCtrlBlk->dxeChannel[channelLoop],
                            channelLoop, &channelLog);

      wpalMemoryCopy(channelLog.channelName,
                     channelType[channelLoop],
                     WPT_TRPT_CHANNEL_NAME);
      wpalPacketStallUpdateInfo(NULL, NULL, &channelLog, channelLoop);

   }

   
   msgPtr->callback = dxeTxThreadChannelDebugHandler;
   status = wpalPostTxMsg(WDI_GET_PAL_CTX(),
                          msgPtr);
   if ( eWLAN_PAL_STATUS_SUCCESS != status )
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "Tx thread state dump req serialize fail status=%d",
               status, 0, 0);
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return;
}

void dxeRXHealthMonitor
(
   wpt_msg         *healthMonitorMsg
)
{
   WLANDXE_ChannelCBType    *channelCtrlBlk;
   WLANDXE_ChannelCBType    *testCHCtrlBlk;
   wpt_uint32                regValue;
   wpt_uint32                chStatusReg, chControlReg, chDescReg, chLDescReg;
   wpt_uint32                hwWakeLoop, chLoop;

   if(NULL == healthMonitorMsg)
   {
      return;
   }

   
   dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);
   dxeNotifySmsm(eWLAN_PAL_TRUE, eWLAN_PAL_FALSE);
   dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);

   for(hwWakeLoop = 0; hwWakeLoop < T_WLANDXE_MAX_HW_ACCESS_WAIT; hwWakeLoop++)
   {
      wpalReadRegister(WLANDXE_BMU_AVAILABLE_BD_PDU, &regValue);
      if(0 != regValue)
      {
         break;
      }
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
            "Scheduled RX, num free BD/PDU %d, loop Count %d",
            regValue, hwWakeLoop, 0);

   for(chLoop = WDTS_CHANNEL_RX_LOW_PRI; chLoop < WDTS_CHANNEL_MAX; chLoop++)
   {
      testCHCtrlBlk = &tempDxeCtrlBlk->dxeChannel[chLoop];
      wpalReadRegister(testCHCtrlBlk->channelRegister.chDXECtrlRegAddr, &chControlReg);
      wpalReadRegister(testCHCtrlBlk->channelRegister.chDXEStatusRegAddr, &chStatusReg);
      wpalReadRegister(testCHCtrlBlk->channelRegister.chDXEDesclRegAddr, &chDescReg);
      wpalReadRegister(testCHCtrlBlk->channelRegister.chDXELstDesclRegAddr, &chLDescReg);

      wpalTrace(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO,
                "%11s : CCR 0x%x, CSR 0x%x, CDR 0x%x, CLDR 0x%x, HCBO %d, HCBDP 0x%x, HCBDC 0x%x, TCBO %d,TCBDP 0x%x, TCBDC 0x%x",
                channelType[chLoop],
                chControlReg, chStatusReg, chDescReg, chLDescReg,
                testCHCtrlBlk->headCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->headCtrlBlk->linkedDescPhyAddr,
                testCHCtrlBlk->headCtrlBlk->linkedDesc->descCtrl.ctrl,
                testCHCtrlBlk->tailCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->tailCtrlBlk->linkedDescPhyAddr,
                testCHCtrlBlk->tailCtrlBlk->linkedDesc->descCtrl.ctrl);

      if((chControlReg & WLANDXE_DESC_CTRL_VALID) && 
         (chLDescReg != testCHCtrlBlk->headCtrlBlk->linkedDescPhyAddr))
      {
         wpalTrace(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                   "%11s : CCR 0x%x, CSR 0x%x, CDR 0x%x, CLDR 0x%x, "
                   "HCBO %d, HCBDP 0x%x, HCBDC 0x%x, TCBO %d,TCBDP 0x%x, TCBDC 0x%x",
                   channelType[chLoop],
                   chControlReg, chStatusReg, chDescReg, chLDescReg,
                   testCHCtrlBlk->headCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->headCtrlBlk->linkedDescPhyAddr,
                   testCHCtrlBlk->headCtrlBlk->linkedDesc->descCtrl.ctrl,
                   testCHCtrlBlk->tailCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->tailCtrlBlk->linkedDescPhyAddr,
                   testCHCtrlBlk->tailCtrlBlk->linkedDesc->descCtrl.ctrl);
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                  "%11s : RX CH EN Descriptor Async, resync it", channelType[chLoop], 0, 0);
         wpalWriteRegister(testCHCtrlBlk->channelRegister.chDXELstDesclRegAddr,
                           testCHCtrlBlk->headCtrlBlk->linkedDescPhyAddr);
      }
      else if(!(chControlReg & WLANDXE_DESC_CTRL_VALID) && 
               (chDescReg != testCHCtrlBlk->headCtrlBlk->linkedDescPhyAddr))
      {
         wpalTrace(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                   "%11s : CCR 0x%x, CSR 0x%x, CDR 0x%x, CLDR 0x%x, "
                   "HCBO %d, HCBDP 0x%x, HCBDC 0x%x, TCBO %d,TCBDP 0x%x, TCBDC 0x%x",
                   channelType[chLoop],
                   chControlReg, chStatusReg, chDescReg, chLDescReg,
                   testCHCtrlBlk->headCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->headCtrlBlk->linkedDescPhyAddr,
                   testCHCtrlBlk->headCtrlBlk->linkedDesc->descCtrl.ctrl,
                   testCHCtrlBlk->tailCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->tailCtrlBlk->linkedDescPhyAddr,
                   testCHCtrlBlk->tailCtrlBlk->linkedDesc->descCtrl.ctrl);
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                  "%11s : RX CH DIS Descriptor Async, resync it", channelType[chLoop], 0, 0);
         wpalWriteRegister(testCHCtrlBlk->channelRegister.chDXEDesclRegAddr,
                           testCHCtrlBlk->headCtrlBlk->linkedDescPhyAddr);
      }
   }

   channelCtrlBlk = (WLANDXE_ChannelCBType *)healthMonitorMsg->pContext;
   if(channelCtrlBlk->hitLowResource)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "%11s : Still Low Resource, kick DXE TX and restart timer",
               channelType[channelCtrlBlk->channelType], 0, 0);
      
      wpalTimerStart(&channelCtrlBlk->healthMonitorTimer,
                     T_WLANDXE_PERIODIC_HEALTH_M_TIME);
   }
   else
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "%11s : Out from Low resource condition, do nothing",
               channelType[channelCtrlBlk->channelType], 0, 0);
   }

   return;
}

void dxeTXHealthMonitor
(
   wpt_msg         *healthMonitorMsg
)
{
   WLANDXE_ChannelCBType    *channelCtrlBlk;
   WLANDXE_ChannelCBType    *testCHCtrlBlk;
   wpt_uint32                regValue;
   wpt_uint32                chStatusReg, chControlReg, chDescReg, chLDescReg;
   wpt_uint32                hwWakeLoop, chLoop;
   wpt_status                status  = eWLAN_PAL_STATUS_SUCCESS;

   if(NULL == healthMonitorMsg)
   {
      return;
   }

   dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);
   dxeNotifySmsm(eWLAN_PAL_TRUE, eWLAN_PAL_FALSE);
   dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);

   
   for(hwWakeLoop = 0; hwWakeLoop < T_WLANDXE_MAX_HW_ACCESS_WAIT; hwWakeLoop++)
   {
      wpalReadRegister(WLANDXE_BMU_AVAILABLE_BD_PDU, &regValue);
      if(0 != regValue)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                  "num free BD/PDU %d, loop Count %d",
                  regValue, hwWakeLoop, 0);
         break;
      }
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
            "Scheduled TX, num free BD/PDU %d, loop Count %d",
            regValue, hwWakeLoop, 0);

   for(chLoop = 0; chLoop < WDTS_CHANNEL_RX_LOW_PRI; chLoop++)
   {
      testCHCtrlBlk = &tempDxeCtrlBlk->dxeChannel[chLoop];
      wpalReadRegister(testCHCtrlBlk->channelRegister.chDXECtrlRegAddr, &chControlReg);
      wpalReadRegister(testCHCtrlBlk->channelRegister.chDXEStatusRegAddr, &chStatusReg);
      wpalReadRegister(testCHCtrlBlk->channelRegister.chDXEDesclRegAddr, &chDescReg);
      wpalReadRegister(testCHCtrlBlk->channelRegister.chDXELstDesclRegAddr, &chLDescReg);

      wpalTrace(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO,
                "%11s : CCR 0x%x, CSR 0x%x, CDR 0x%x, CLDR 0x%x, HCBO %d, HCBDP 0x%x, HCBDC 0x%x, TCBO %d,TCBDP 0x%x, TCBDC 0x%x",
                channelType[chLoop],
                chControlReg, chStatusReg, chDescReg, chLDescReg,
                testCHCtrlBlk->headCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->headCtrlBlk->linkedDescPhyAddr,
                testCHCtrlBlk->headCtrlBlk->linkedDesc->descCtrl.ctrl,
                testCHCtrlBlk->tailCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->tailCtrlBlk->linkedDescPhyAddr,
                testCHCtrlBlk->tailCtrlBlk->linkedDesc->descCtrl.ctrl);

      if((chControlReg & WLANDXE_DESC_CTRL_VALID) && 
         (chLDescReg != testCHCtrlBlk->tailCtrlBlk->linkedDescPhyAddr))
      {
         wpalTrace(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                   "%11s : CCR 0x%x, CSR 0x%x, CDR 0x%x, CLDR 0x%x, "
                   "HCBO %d, HCBDP 0x%x, HCBDC 0x%x, TCBO %d,TCBDP 0x%x, TCBDC 0x%x",
                   channelType[chLoop],
                   chControlReg, chStatusReg, chDescReg, chLDescReg,
                   testCHCtrlBlk->headCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->headCtrlBlk->linkedDescPhyAddr,
                   testCHCtrlBlk->headCtrlBlk->linkedDesc->descCtrl.ctrl,
                   testCHCtrlBlk->tailCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->tailCtrlBlk->linkedDescPhyAddr,
                   testCHCtrlBlk->tailCtrlBlk->linkedDesc->descCtrl.ctrl);
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                  "%11s : TX CH EN Descriptor Async, resync it", channelType[chLoop], 0, 0);
         wpalWriteRegister(testCHCtrlBlk->channelRegister.chDXELstDesclRegAddr,
                           testCHCtrlBlk->tailCtrlBlk->linkedDescPhyAddr);
      }
      else if(!(chControlReg & WLANDXE_DESC_CTRL_VALID) && 
               (chDescReg != testCHCtrlBlk->tailCtrlBlk->linkedDescPhyAddr))
      {
         wpalTrace(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                   "%11s : CCR 0x%x, CSR 0x%x, CDR 0x%x, CLDR 0x%x, "
                   "HCBO %d, HCBDP 0x%x, HCBDC 0x%x, TCBO %d,TCBDP 0x%x, TCBDC 0x%x",
                   channelType[chLoop],
                   chControlReg, chStatusReg, chDescReg, chLDescReg,
                   testCHCtrlBlk->headCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->headCtrlBlk->linkedDescPhyAddr,
                   testCHCtrlBlk->headCtrlBlk->linkedDesc->descCtrl.ctrl,
                   testCHCtrlBlk->tailCtrlBlk->ctrlBlkOrder, testCHCtrlBlk->tailCtrlBlk->linkedDescPhyAddr,
                   testCHCtrlBlk->tailCtrlBlk->linkedDesc->descCtrl.ctrl);
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                  "%11s : TX CH DIS Descriptor Async, resync it", channelType[chLoop], 0, 0);
         wpalWriteRegister(testCHCtrlBlk->channelRegister.chDXEDesclRegAddr,
                           testCHCtrlBlk->tailCtrlBlk->linkedDescPhyAddr);
      }
   }

   
   channelCtrlBlk = (WLANDXE_ChannelCBType *)healthMonitorMsg->pContext;
   channelCtrlBlk->healthMonitorMsg->callback = dxeRXHealthMonitor;
   status = wpalPostRxMsg(WDI_GET_PAL_CTX(),
                          channelCtrlBlk->healthMonitorMsg);
   if (eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "TX Low resource Kick DXE MSG Serialize fail",
               status, 0, 0);
   }

   return;
}

void dxeHealthMonitorTimeout
(
   void         *channelCtxt
)
{
   WLANDXE_ChannelCBType    *channelCtrlBlk;
   wpt_status                status  = eWLAN_PAL_STATUS_SUCCESS;

   if(NULL == channelCtxt)
   {
      return;
   }

   channelCtrlBlk = (WLANDXE_ChannelCBType *)channelCtxt;
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO,
            "%11s : Health Monitor timer expired",
            channelType[channelCtrlBlk->channelType], 0, 0);

   channelCtrlBlk->healthMonitorMsg->callback = dxeTXHealthMonitor;
   status = wpalPostTxMsg(WDI_GET_PAL_CTX(),
                          channelCtrlBlk->healthMonitorMsg);
   if (eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "TX Low resource Kick DXE MSG Serialize fail",
               status, 0, 0);
   }

   return;
}

static wpt_status dxeCtrlBlkAlloc
(
   WLANDXE_CtrlBlkType     *dxeCtrlBlk,
   WLANDXE_ChannelCBType   *channelEntry
)
{
   wpt_status                status = eWLAN_PAL_STATUS_SUCCESS;
   unsigned int              idx, fIdx;
   WLANDXE_DescCtrlBlkType  *currentCtrlBlk = NULL;
   WLANDXE_DescCtrlBlkType  *freeCtrlBlk = NULL;
   WLANDXE_DescCtrlBlkType  *prevCtrlBlk = NULL;
   WLANDXE_DescCtrlBlkType  *nextCtrlBlk = NULL;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if((NULL == dxeCtrlBlk) || (NULL == channelEntry))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeCtrlBlkAlloc Channel Entry is not valid");
      return eWLAN_PAL_STATUS_E_INVAL;
   }

   
   for(idx = 0; idx < channelEntry->numDesc; idx++)
   {
      currentCtrlBlk = (WLANDXE_DescCtrlBlkType *)wpalMemoryAllocate(sizeof(WLANDXE_DescCtrlBlkType));
      if(NULL == currentCtrlBlk)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeCtrlBlkOpen MemAlloc Fail for channel %d",
                  channelEntry->channelType);
         freeCtrlBlk = channelEntry->headCtrlBlk;
         for(fIdx = 0; fIdx < idx; fIdx++)
         {
            if(NULL == freeCtrlBlk)
            {
               break;
            }

            nextCtrlBlk = freeCtrlBlk->nextCtrlBlk;
            wpalMemoryFree((void *)freeCtrlBlk);
            freeCtrlBlk = nextCtrlBlk;
         }
         return eWLAN_PAL_STATUS_E_FAULT;
      }

      memset((wpt_uint8 *)currentCtrlBlk, 0, sizeof(WLANDXE_DescCtrlBlkType));
      
      currentCtrlBlk->xfrFrame          = NULL;
      currentCtrlBlk->linkedDesc        = NULL;
      currentCtrlBlk->linkedDescPhyAddr = 0;
      currentCtrlBlk->ctrlBlkOrder      = idx;

      if(0 == idx)
      {
         currentCtrlBlk->nextCtrlBlk = NULL;
         channelEntry->headCtrlBlk   = currentCtrlBlk;
         channelEntry->tailCtrlBlk   = currentCtrlBlk;
      }
      else if((0 < idx) && (idx < (channelEntry->numDesc - 1)))
      {
         prevCtrlBlk->nextCtrlBlk = currentCtrlBlk;
      }
      else if((channelEntry->numDesc - 1) == idx)
      {
         prevCtrlBlk->nextCtrlBlk    = currentCtrlBlk;
         currentCtrlBlk->nextCtrlBlk = channelEntry->headCtrlBlk;
      }
      else
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeCtrlBlkOpen Invalid Ctrl Blk location %d",
                  channelEntry->channelType);
         wpalMemoryFree(currentCtrlBlk);
         return eWLAN_PAL_STATUS_E_FAULT;
      }

      prevCtrlBlk = currentCtrlBlk;
      channelEntry->numFreeDesc++;
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,"%s Exit", __func__);
   return status;
}

static wpt_status dxeDescAllocAndLink
(
   WLANDXE_CtrlBlkType     *dxeCtrlBlk,
   WLANDXE_ChannelCBType   *channelEntry
)
{
   wpt_status                status      = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_DescType         *currentDesc = NULL;
   WLANDXE_DescType         *prevDesc    = NULL;
   WLANDXE_DescCtrlBlkType  *currentCtrlBlk = NULL;
   unsigned int              idx;
   void                     *physAddress = NULL;
#ifdef WLANDXE_TEST_CHANNEL_ENABLE
   WLANDXE_ChannelCBType    *testTXChannelCB = &dxeCtrlBlk->dxeChannel[WDTS_CHANNEL_H2H_TEST_TX];
   WLANDXE_DescCtrlBlkType  *currDescCtrlBlk = testTXChannelCB->headCtrlBlk;
#endif 

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if((NULL == dxeCtrlBlk) || (NULL == channelEntry))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeDescLinkAlloc Channel Entry is not valid");
      return eWLAN_PAL_STATUS_E_INVAL;
   }

   currentCtrlBlk = channelEntry->headCtrlBlk;

#if !(defined(FEATURE_R33D) || defined(WLANDXE_TEST_CHANNEL_ENABLE))
   
   channelEntry->descriptorAllocation = (WLANDXE_DescType *)
      wpalDmaMemoryAllocate(sizeof(WLANDXE_DescType)*channelEntry->numDesc,
                            &physAddress);
   if(NULL == channelEntry->descriptorAllocation)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeDescLinkAlloc Descriptor Alloc Fail");
      return eWLAN_PAL_STATUS_E_RESOURCES;
   }
   currentDesc = channelEntry->descriptorAllocation;
#endif

   
   for(idx = 0; idx < channelEntry->numDesc; idx++)
   {
#ifndef FEATURE_R33D
#ifndef WLANDXE_TEST_CHANNEL_ENABLE
      
      memset((wpt_uint8 *)currentDesc, 0, sizeof(WLANDXE_DescType));
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
               "Allocated Descriptor VA 0x%x, PA 0x%x", currentDesc, physAddress);
#else
      if(WDTS_CHANNEL_H2H_TEST_RX != channelEntry->channelType)
      {
         
         currentDesc = (WLANDXE_DescType *)wpalDmaMemoryAllocate(sizeof(WLANDXE_DescType),
                                                                 &physAddress);
         memset((wpt_uint8 *)currentDesc, 0, sizeof(WLANDXE_DescType));
      }
      else
      {
         currentDesc     = currDescCtrlBlk->linkedDesc;
         physAddress     = (void *)currDescCtrlBlk->linkedDescPhyAddr;
         currDescCtrlBlk = (WLANDXE_DescCtrlBlkType *)currDescCtrlBlk->nextCtrlBlk;
      }
#endif 
#else
#ifndef WLANDXE_TEST_CHANNEL_ENABLE
      currentDesc = (WLANDXE_DescType *)wpalAcpuDdrDxeDescMemoryAllocate(&physAddress);
      memset((wpt_uint8 *)currentDesc, 0, sizeof(WLANDXE_DescType));
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
               "Allocated Descriptor VA 0x%x, PA 0x%x", currentDesc, physAddress);
#else
      if(WDTS_CHANNEL_H2H_TEST_RX != channelEntry->channelType)
      {
         currentDesc = (WLANDXE_DescType *)wpalAcpuDdrDxeDescMemoryAllocate(&physAddress);
         memset((wpt_uint8 *)currentDesc, 0, sizeof(WLANDXE_DescType));
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
                  "Allocated Descriptor VA 0x%x, PA 0x%x", currentDesc, physAddress);
      }
      else
      {
         currentDesc     = currDescCtrlBlk->linkedDesc;
         physAddress     = (void *)currDescCtrlBlk->linkedDescPhyAddr;
         currDescCtrlBlk = (WLANDXE_DescCtrlBlkType *)currDescCtrlBlk->nextCtrlBlk;
      }
#endif 
#endif 
      if(NULL == currentDesc)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeDescLinkAlloc MemAlloc Fail for channel %d",
                  channelEntry->channelType);
         return eWLAN_PAL_STATUS_E_FAULT;
      }

      currentCtrlBlk->linkedDesc        = currentDesc;
      currentCtrlBlk->linkedDescPhyAddr = (unsigned int)physAddress;
      if(0 == idx)
      {
         currentDesc->dxedesc.dxe_short_desc.phyNextL = 0;
         channelEntry->DescBottomLoc                  = currentDesc;
         channelEntry->descBottomLocPhyAddr           = (unsigned int)physAddress;
      }
      else if((0 < idx) && (idx < (channelEntry->numDesc - 1)))
      {
         prevDesc->dxedesc.dxe_short_desc.phyNextL = 
                                  WLANDXE_U32_SWAP_ENDIAN((wpt_uint32)physAddress);
      }
      else if((channelEntry->numDesc - 1) == idx)
      {
         prevDesc->dxedesc.dxe_short_desc.phyNextL    = 
                                  WLANDXE_U32_SWAP_ENDIAN((wpt_uint32)physAddress);
         currentDesc->dxedesc.dxe_short_desc.phyNextL =
                                  WLANDXE_U32_SWAP_ENDIAN((wpt_uint32)channelEntry->headCtrlBlk->linkedDescPhyAddr);
      }

#ifdef WLANDXE_TEST_CHANNEL_ENABLE
      if((WDTS_CHANNEL_RX_LOW_PRI  == channelEntry->channelType) ||
         (WDTS_CHANNEL_RX_HIGH_PRI == channelEntry->channelType) ||
         (WDTS_CHANNEL_H2H_TEST_RX == channelEntry->channelType))
#else
      if((WDTS_CHANNEL_RX_LOW_PRI  == channelEntry->channelType) ||
         (WDTS_CHANNEL_RX_HIGH_PRI == channelEntry->channelType))
#endif 
      {
         status = dxeRXFrameSingleBufferAlloc(dxeCtrlBlk,
                                              channelEntry,
                                              currentCtrlBlk);
         if( !WLAN_PAL_IS_STATUS_SUCCESS(status) )
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                     "dxeDescLinkAlloc RX Buffer Alloc Fail for channel %d",
                     channelEntry->channelType);
            return status;
         }
         --channelEntry->numFreeDesc;
      }

      if((WDTS_CHANNEL_TX_LOW_PRI == channelEntry->channelType) ||
         (WDTS_CHANNEL_TX_HIGH_PRI == channelEntry->channelType))
      {
         currentDesc->descCtrl.ctrl = channelEntry->extraConfig.cw_ctrl_write;
         currentDesc->dxedesc.dxe_short_desc.dstMemAddrL = channelEntry->extraConfig.refWQ_swapped;
      }
      else if((WDTS_CHANNEL_RX_LOW_PRI == channelEntry->channelType) ||
              (WDTS_CHANNEL_RX_HIGH_PRI == channelEntry->channelType))
      {
         currentDesc->descCtrl.ctrl = channelEntry->extraConfig.cw_ctrl_read;
         currentDesc->dxedesc.dxe_short_desc.srcMemAddrL = channelEntry->extraConfig.refWQ_swapped;
      }
      else
      {
      }

      currentCtrlBlk = currentCtrlBlk->nextCtrlBlk;
      prevDesc       = currentDesc;

#ifndef FEATURE_R33D
#ifndef WLANDXE_TEST_CHANNEL_ENABLE
      
      currentDesc++;
      physAddress = ((wpt_int8 *)physAddress) + sizeof(WLANDXE_DescType);
#endif
#endif
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

static wpt_status dxeSetInterruptPath
(
   WLANDXE_CtrlBlkType     *dxeCtrlBlk
)
{
   wpt_status                 status        = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                 interruptPath = 0;
   wpt_uint32                 idx;
   WLANDXE_ChannelCBType     *channelEntry = NULL;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   for(idx = 0; idx < WDTS_CHANNEL_MAX; idx++)
   {
      channelEntry = &dxeCtrlBlk->dxeChannel[idx];
#ifdef WLANDXE_TEST_CHANNEL_ENABLE
      if((WDTS_CHANNEL_TX_LOW_PRI == channelEntry->channelType) ||
         (WDTS_CHANNEL_TX_HIGH_PRI == channelEntry->channelType) ||
         (WDTS_CHANNEL_H2H_TEST_TX == channelEntry->channelType))
#else
      if((WDTS_CHANNEL_TX_LOW_PRI == channelEntry->channelType) ||
         (WDTS_CHANNEL_TX_HIGH_PRI == channelEntry->channelType))
#endif 
      {
         interruptPath |= (1 << channelEntry->assignedDMAChannel);
      }
      else if((WDTS_CHANNEL_RX_LOW_PRI == channelEntry->channelType) ||
              (WDTS_CHANNEL_RX_HIGH_PRI == channelEntry->channelType))
      {
         interruptPath |= (1 << (channelEntry->assignedDMAChannel + 16));
      }
      else
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "H2H TEST RX???? %d", channelEntry->channelType);
      }
   }
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
            "Interrupt Path Must be 0x%x", interruptPath);
   dxeCtrlBlk->interruptPath = interruptPath;
   wpalWriteRegister(WLANDXE_CCU_DXE_INT_SELECT, interruptPath);

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

static wpt_status dxeEngineCoreStart
(
   WLANDXE_CtrlBlkType     *dxeCtrlBlk
)
{
   wpt_status                 status = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                 registerData = 0;
   wpt_uint8                  readRetry;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

#ifdef WCN_PRONTO
   
   wpalReadRegister(WLANDXE_CCU_SOFT_RESET, &registerData);
   registerData |= WLANDXE_DMA_CCU_DXE_RESET_MASK;

   
   wpalWriteRegister(WLANDXE_CCU_SOFT_RESET, registerData);

   
   registerData &= ~WLANDXE_DMA_CCU_DXE_RESET_MASK;
   wpalWriteRegister(WLANDXE_CCU_SOFT_RESET, registerData);
#else
   
   
   registerData = WLANDXE_DMA_CSR_RESET_MASK;
   wpalWriteRegister(WALNDEX_DMA_CSR_ADDRESS,
                          registerData);
#endif 

   for(readRetry = 0; readRetry < WLANDXE_CSR_MAX_READ_COUNT; readRetry++)
   {
   wpalWriteRegister(WALNDEX_DMA_CSR_ADDRESS,
                        WLANDXE_CSR_DEFAULT_ENABLE);
      wpalReadRegister(WALNDEX_DMA_CSR_ADDRESS, &registerData);
      if(!(registerData & WLANDXE_DMA_CSR_EN_MASK))
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "%s CSR 0x%x, count %d",
                  __func__, registerData, readRetry);
         
         wpalBusyWait(WLANDXE_CSR_NEXT_READ_WAIT);
      }
      else
      {
         break;
      }
   }
   if(WLANDXE_CSR_MAX_READ_COUNT == readRetry)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "%s CSR INVALID 0x%x", __func__, registerData);
      wpalDevicePanic();
   }
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
            "%s CSR 0x%x", __func__, registerData);

   

   dxeSetInterruptPath(dxeCtrlBlk);
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

static wpt_status dxeChannelInitProgram
(
   WLANDXE_CtrlBlkType     *dxeCtrlBlk,
   WLANDXE_ChannelCBType   *channelEntry
)
{
   wpt_status                status = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                idx;
   WLANDXE_DescType         *currentDesc = NULL;
   WLANDXE_DescCtrlBlkType  *currentCtrlBlk = NULL;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if((NULL == dxeCtrlBlk) || (NULL == channelEntry))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelInitProgram Channel Entry is not valid");
      return eWLAN_PAL_STATUS_E_INVAL;
   }

   
   if(!channelEntry->channelConfig.useShortDescFmt)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelInitProgram Long Descriptor not support yet");
      return eWLAN_PAL_STATUS_E_FAILURE;
   }

   
   
   status = wpalWriteRegister(channelEntry->channelRegister.chDXEDesclRegAddr,
                                   channelEntry->headCtrlBlk->linkedDescPhyAddr);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelInitProgram Write DESC Address register fail");
      return status;
   }

   if((WDTS_CHANNEL_TX_LOW_PRI  == channelEntry->channelType) ||
      (WDTS_CHANNEL_TX_HIGH_PRI == channelEntry->channelType))
   {
      
      
      status = wpalWriteRegister(channelEntry->channelRegister.chDXEDadrlRegAddr,
                                      channelEntry->channelConfig.refWQ);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeChannelInitProgram Write TX DAddress register fail");
         return status;
      }
   }
   else if((WDTS_CHANNEL_RX_LOW_PRI  == channelEntry->channelType) ||
           (WDTS_CHANNEL_RX_HIGH_PRI == channelEntry->channelType))
   {
      
      currentCtrlBlk = channelEntry->headCtrlBlk;
      for(idx = 0; idx < channelEntry->channelConfig.nDescs; idx++)
      {
         currentDesc = currentCtrlBlk->linkedDesc;
         currentCtrlBlk = currentCtrlBlk->nextCtrlBlk;         
      }

      
      status = wpalWriteRegister(channelEntry->channelRegister.chDXESadrlRegAddr,
                                      channelEntry->channelConfig.refWQ);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeChannelInitProgram Write RX SAddress WQ register fail");
         return status;
      }

      
      status = wpalWriteRegister(channelEntry->channelRegister.chDXEDadrlRegAddr,
                                      WLANDXE_U32_SWAP_ENDIAN(channelEntry->DescBottomLoc->dxedesc.dxe_short_desc.phyNextL));
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeChannelInitProgram Write RX DAddress register fail");
         return status;
      }

      
      wpalWriteRegister(channelEntry->channelRegister.chDXECtrlRegAddr,
                             channelEntry->extraConfig.chan_mask);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeChannelInitProgram Write RX Control register fail");
         return status;
      }
   }
   else
   {
      
      
      status = wpalWriteRegister(channelEntry->channelRegister.chDXEDadrlRegAddr,
                                      WLANDXE_U32_SWAP_ENDIAN(channelEntry->DescBottomLoc->dxedesc.dxe_short_desc.phyNextL));
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeChannelInitProgram Write RX DAddress register fail");
         return status;
      }
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}


static wpt_status dxeChannelStart
(
   WLANDXE_CtrlBlkType     *dxeCtrlBlk,
   WLANDXE_ChannelCBType   *channelEntry
)
{
   wpt_status                status     = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                regValue   = 0;
   wpt_uint32                intMaskVal = 0;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   channelEntry->extraConfig.chEnabled    = eWLAN_PAL_TRUE;
   channelEntry->extraConfig.chConfigured = eWLAN_PAL_TRUE;

   status = wpalReadRegister(WALNDEX_DMA_CH_EN_ADDRESS,
                                  &regValue);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelStart Read Channel Enable register fail");
      return status;
   }

   
   status = wpalReadRegister(WLANDXE_INT_MASK_REG_ADDRESS,
                                  &intMaskVal);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelStart Read INT_MASK register fail");
      return status;         
   }
   intMaskVal |= channelEntry->extraConfig.intMask;
   status = wpalWriteRegister(WLANDXE_INT_MASK_REG_ADDRESS,
                                   intMaskVal);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelStart Write INT_MASK register fail");
      return status;         
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

static wpt_status dxeChannelStop
(
   WLANDXE_CtrlBlkType     *dxeCtrlBlk,
   WLANDXE_ChannelCBType   *channelEntry
)
{
   wpt_status                status     = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                intMaskVal = 0;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if((NULL == dxeCtrlBlk) || (NULL == channelEntry))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelStop Invalid arg input");
      return eWLAN_PAL_STATUS_E_INVAL; 
   }

   if ( (channelEntry->extraConfig.chEnabled != eWLAN_PAL_TRUE) ||
        (channelEntry->extraConfig.chConfigured != eWLAN_PAL_TRUE))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelStop channels are not enabled ");
      return status; 
   }
   
   status = wpalReadRegister(WLANDXE_INT_MASK_REG_ADDRESS,
                                  &intMaskVal);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelStop Read INT_MASK register fail");
      return status;         
   }
   intMaskVal ^= channelEntry->extraConfig.intMask;
   status = wpalWriteRegister(WLANDXE_INT_MASK_REG_ADDRESS,
                                   intMaskVal);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelStop Write INT_MASK register fail");
      return status;         
   }

   channelEntry->extraConfig.chEnabled    = eWLAN_PAL_FALSE;

   
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

static wpt_status dxeChannelClose
(
   WLANDXE_CtrlBlkType     *dxeCtrlBlk,
   WLANDXE_ChannelCBType   *channelEntry
)
{
   wpt_status                status            = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                idx;
   WLANDXE_DescCtrlBlkType  *currentCtrlBlk    = NULL;
   WLANDXE_DescCtrlBlkType  *nextCtrlBlk       = NULL;
   WLANDXE_DescType         *currentDescriptor = NULL;
   WLANDXE_DescType         *nextDescriptor    = NULL;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if((NULL == dxeCtrlBlk) || (NULL == channelEntry))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelStop Invalid arg input");
      return eWLAN_PAL_STATUS_E_INVAL; 
   }

   currentCtrlBlk    = channelEntry->headCtrlBlk;
   if(NULL != currentCtrlBlk)
   {
      currentDescriptor = currentCtrlBlk->linkedDesc;
      for(idx = 0; idx < channelEntry->numDesc; idx++)
      {
          if (idx + 1 != channelEntry->numDesc)
          {
              nextCtrlBlk    = currentCtrlBlk->nextCtrlBlk;
              nextDescriptor = nextCtrlBlk->linkedDesc;
          }
          else
          {
              nextCtrlBlk = NULL;
              nextDescriptor = NULL;
          }
          if((WDTS_CHANNEL_RX_LOW_PRI  == channelEntry->channelType) ||
             (WDTS_CHANNEL_RX_HIGH_PRI == channelEntry->channelType))
          {
            if (NULL != currentCtrlBlk->xfrFrame)
            {
               wpalUnlockPacket(currentCtrlBlk->xfrFrame);
               wpalPacketFree(currentCtrlBlk->xfrFrame);
            }
         }
         if((WDTS_CHANNEL_TX_LOW_PRI  == channelEntry->channelType) ||
            (WDTS_CHANNEL_TX_HIGH_PRI == channelEntry->channelType))
         {
             if((NULL != currentCtrlBlk->xfrFrame) && 
                     (eWLAN_PAL_STATUS_SUCCESS == wpalIsPacketLocked(currentCtrlBlk->xfrFrame)))
               {
                  wpalUnlockPacket(currentCtrlBlk->xfrFrame);
                  wpalPacketFree(currentCtrlBlk->xfrFrame);
               }
         }
#if (defined(FEATURE_R33D) || defined(WLANDXE_TEST_CHANNEL_ENABLE))
         
         wpalDmaMemoryFree(currentDescriptor);
#endif
         wpalMemoryFree(currentCtrlBlk);

         currentCtrlBlk    = nextCtrlBlk;
         currentDescriptor = nextDescriptor;
         if(NULL == currentCtrlBlk)
         {
            break;
         }
      }
   }

#if !(defined(FEATURE_R33D) || defined(WLANDXE_TEST_CHANNEL_ENABLE))
   
   if(NULL != channelEntry->descriptorAllocation)
   {
      wpalDmaMemoryFree(channelEntry->descriptorAllocation);
   }
#endif

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

static wpt_status dxeChannelCleanInt
(
   WLANDXE_ChannelCBType   *channelEntry,
   wpt_uint32              *chStat
)
{
   wpt_status                status = eWLAN_PAL_STATUS_SUCCESS;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   status = wpalReadRegister(channelEntry->channelRegister.chDXEStatusRegAddr,
                                  chStat);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelCleanInt Read CH STAT register fail");
      return eWLAN_PAL_STATUS_E_FAULT;         
   }
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Channel INT Clean, Status 0x%x",
            channelType[channelEntry->channelType], *chStat);

   
   status = wpalWriteRegister(WLANDXE_INT_CLR_ADDRESS,
                                   (1 << channelEntry->assignedDMAChannel));
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeChannelCleanInt Write CH Clean register fail");
      return eWLAN_PAL_STATUS_E_FAULT;         
   }

   
   if(WLANDXE_CH_STAT_INT_ERR_MASK & *chStat)
   {
      status = wpalWriteRegister(WLANDXE_INT_ERR_CLR_ADDRESS,
                                      (1 << channelEntry->assignedDMAChannel));
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeChannelCleanInt Read CH STAT register fail");
         return eWLAN_PAL_STATUS_E_FAULT;         
      }
   }

   
   if(WLANDXE_CH_STAT_INT_DONE_MASK & *chStat)
   {
      status = wpalWriteRegister(WLANDXE_INT_DONE_CLR_ADDRESS,
                                      (1 << channelEntry->assignedDMAChannel));
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeChannelCleanInt Read CH STAT register fail");
         return eWLAN_PAL_STATUS_E_FAULT;         
      }
   }

   
   if(WLANDXE_CH_STAT_INT_ED_MASK & *chStat)
   {
      status = wpalWriteRegister(WLANDXE_INT_ED_CLR_ADDRESS,
                                      (1 << channelEntry->assignedDMAChannel));
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeChannelCleanInt Read CH STAT register fail");
         return eWLAN_PAL_STATUS_E_FAULT;         
      }
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

#ifdef WLAN_DXE_LOW_RESOURCE_TIMER
void dxeRXResourceAvailableTimerExpHandler
(
   void    *usrData
)
{
   WLANDXE_CtrlBlkType      *dxeCtxt    = NULL;
   wpt_uint32               numRxFreePackets;

   dxeCtxt = (WLANDXE_CtrlBlkType *)usrData;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
            "RX Low resource, Durign wait time period %d, RX resource not allocated",
            T_WLANDXE_MAX_RX_PACKET_WAIT);

   
   wpalGetNumRxFreePacket(&numRxFreePackets);

   if (numRxFreePackets > 0)
   {
      if (NULL != dxeCtxt)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                  "%s: Replenish successful. Restart the Rx Low resource timer",
                  __func__);
         wpalTimerStart(&dxeCtxt->rxResourceAvailableTimer,
                        T_WLANDXE_MAX_RX_PACKET_WAIT);
         return;
      }
   }

   if (NULL != dxeCtxt)
      dxeCtxt->driverReloadInProcessing = eWLAN_PAL_TRUE;

   wpalWlanReload();

   if (NULL != usrData)
      dxeStartSSRTimer((WLANDXE_CtrlBlkType *)usrData);

   return;
}
#endif

static void dxeStartSSRTimer
(
  WLANDXE_CtrlBlkType     *dxeCtxt
)
{
   if(VOS_TIMER_STATE_RUNNING !=
      wpalTimerGetCurStatus(&dxeCtxt->dxeSSRTimer))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "%s: Starting SSR Timer",__func__);
      wpalTimerStart(&dxeCtxt->dxeSSRTimer,
                     T_WLANDXE_SSR_TIMEOUT);
   }
}

void dxeSSRTimerExpHandler
(
   void    *usrData
)
{
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
            "DXE not shutdown %d ms after FIQ!! Issue SSR",
            T_WLANDXE_SSR_TIMEOUT);
   wpalRivaSubystemRestart();

   return;
}

void dxeRXPacketAvailableCB
(
   wpt_packet   *freePacket,
   v_VOID_t     *usrData
)
{
   WLANDXE_CtrlBlkType       *dxeCtxt = NULL;
   wpt_status                status;

   
   if((NULL == freePacket) || (NULL == usrData))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "Get Free RX Buffer fail, Critical Error");
      HDXE_ASSERT(0);
      return;
   }

   dxeCtxt = (WLANDXE_CtrlBlkType *)usrData;

   if(WLANDXE_CTXT_COOKIE != dxeCtxt->dxeCookie)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "DXE Context data corrupted, Critical Error");
      HDXE_ASSERT(0);
      return;
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO,
            "DXE RX packet available, post MSG to RX Thread");

   dxeCtxt->freeRXPacket = freePacket;

   
   if (NULL == dxeCtxt->rxPktAvailMsg)
   {
       HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "DXE NULL pkt");
       HDXE_ASSERT(0);
       return;
   }

   status = wpalPostRxMsg(WDI_GET_PAL_CTX(),
                          dxeCtxt->rxPktAvailMsg);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "dxeRXPacketAvailableCB serialize fail");
   }

   return;
}

static wpt_status dxeRXFrameSingleBufferAlloc
(
   WLANDXE_CtrlBlkType      *dxeCtxt,
   WLANDXE_ChannelCBType    *channelEntry,
   WLANDXE_DescCtrlBlkType  *currentCtrlBlock
)
{
   wpt_status                status = eWLAN_PAL_STATUS_SUCCESS;
   wpt_packet               *currentPalPacketBuffer = NULL;
   WLANDXE_DescType         *currentDesc            = NULL;
#ifdef FEATURE_R33D
   wpt_uint32                virtualAddressPCIe;
   wpt_uint32                physicalAddressPCIe;   
#else
   wpt_iterator              iterator;
   wpt_uint32                allocatedSize          = 0;
   void                     *physAddress            = NULL;
#endif 


   currentDesc            = currentCtrlBlock->linkedDesc;

   if(currentDesc->descCtrl.valid)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "This Descriptor is valid, Do not refill");
      return eWLAN_PAL_STATUS_E_EXISTS;
   }

   if(dxeCtxt->rxPalPacketUnavailable && (NULL != dxeCtxt->freeRXPacket))
   {
      currentPalPacketBuffer = dxeCtxt->freeRXPacket;
      dxeCtxt->rxPalPacketUnavailable = eWLAN_PAL_FALSE;
      dxeCtxt->freeRXPacket = NULL;
   }
   else if(!dxeCtxt->rxPalPacketUnavailable)
   {
      
      currentPalPacketBuffer = wpalPacketAlloc(eWLAN_PAL_PKT_TYPE_RX_RAW,
                                            WLANDXE_DEFAULT_RX_OS_BUFFER_SIZE,
                                            dxeRXPacketAvailableCB,
                                            (void *)dxeCtxt);

      if(NULL == currentPalPacketBuffer)
      {
         dxeCtxt->rxPalPacketUnavailable = eWLAN_PAL_TRUE;
#ifdef WLAN_DXE_LOW_RESOURCE_TIMER
         if(VOS_TIMER_STATE_RUNNING !=
            wpalTimerGetCurStatus(&dxeCtxt->rxResourceAvailableTimer))
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                     "RX Low resource, wait available resource");
            wpalTimerStart(&dxeCtxt->rxResourceAvailableTimer,
                           T_WLANDXE_MAX_RX_PACKET_WAIT);
         }
#endif
      }
   }
   
   if(NULL == currentPalPacketBuffer)
   {
      return eWLAN_PAL_STATUS_E_RESOURCES;
   }

   currentCtrlBlock->xfrFrame       = currentPalPacketBuffer;
   currentPalPacketBuffer->pktType  = eWLAN_PAL_PKT_TYPE_RX_RAW;
   currentPalPacketBuffer->pBD      = NULL;
   currentPalPacketBuffer->pBDPhys  = NULL;
   currentPalPacketBuffer->BDLength = 0;
#ifdef FEATURE_R33D
   status = wpalAllocateShadowRxFrame(currentPalPacketBuffer,
                                           &physicalAddressPCIe,
                                           &virtualAddressPCIe);
   if((0 == physicalAddressPCIe) || (0 = virtualAddressPCIe))
   {
       HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_MED,
               "RX NULL Shadow Memory");
       HDXE_ASSERT(0);
       return eWLAN_PAL_STATUS_E_FAULT;
   }
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_MED,
            "RX Shadow Memory Va 0x%x, Pa 0x%x",
            virtualAddressPCIe, physicalAddressPCIe);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXFrameBufferAlloc Shadow Mem Alloc fail");
      return status;
   }
   currentCtrlBlock->shadowBufferVa = virtualAddressPCIe;
   currentPalPacketBuffer->pBDPhys  = (void *)physicalAddressPCIe;
   memset((wpt_uint8 *)currentCtrlBlock->shadowBufferVa, 0, WLANDXE_DEFAULT_RX_OS_BUFFER_SIZE);
#else
   status = wpalLockPacketForTransfer(currentPalPacketBuffer);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXFrameBufferAlloc unable to lock packet");
      return status;
   }

   
   status = wpalIteratorInit(&iterator, currentPalPacketBuffer);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXFrameBufferAlloc iterator init fail");
      return status;
   }
   status = wpalIteratorNext(&iterator,
                             currentPalPacketBuffer,
                             &physAddress,
                             &allocatedSize);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXFrameBufferAlloc iterator Get Next pointer fail");
      return status;
   }
   currentPalPacketBuffer->pBDPhys = physAddress;
#endif 

   currentDesc->dxedesc.dxe_short_desc.dstMemAddrL =
                                       WLANDXE_U32_SWAP_ENDIAN((wpt_uint32)currentPalPacketBuffer->pBDPhys);

   return status;
}

static wpt_status dxeRXFrameRefillRing
(
   WLANDXE_CtrlBlkType      *dxeCtxt,
   WLANDXE_ChannelCBType    *channelEntry
)
{
   wpt_status                status = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_DescCtrlBlkType  *currentCtrlBlk = channelEntry->tailCtrlBlk;
   WLANDXE_DescType         *currentDesc    = NULL;

   while(channelEntry->numFreeDesc > 0)
   {
      status = dxeRXFrameSingleBufferAlloc(dxeCtxt,
                                           channelEntry,
                                           currentCtrlBlk);

      if((eWLAN_PAL_STATUS_SUCCESS != status) &&
         (eWLAN_PAL_STATUS_E_EXISTS != status))
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                  "dxeRXFrameRefillRing, out of RX buffer pool, break here");
         break;
      }

      if(eWLAN_PAL_STATUS_E_EXISTS == status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeRXFrameRefillRing, Descriptor Non-Empry");
      }

      currentDesc = currentCtrlBlk->linkedDesc;
      currentDesc->descCtrl.ctrl = channelEntry->extraConfig.cw_ctrl_read;

      if(channelEntry->extraConfig.cw_ctrl_read != currentDesc->descCtrl.ctrl)
      {
         
      }

      
      if(WLANDXE_POWER_STATE_FULL == dxeCtxt->hostPowerState)
      {
         wpalWriteRegister(WALNDEX_DMA_ENCH_ADDRESS,
                           1 << channelEntry->assignedDMAChannel);
      }
      currentCtrlBlk = currentCtrlBlk->nextCtrlBlk;
      if(eWLAN_PAL_STATUS_E_EXISTS != status)
      {
         --channelEntry->numFreeDesc;
      }
   }
   
   channelEntry->tailCtrlBlk = currentCtrlBlk;

   return status;
}

static wpt_int32 dxeRXFrameRouteUpperLayer
(
   WLANDXE_CtrlBlkType     *dxeCtxt,
   WLANDXE_ChannelCBType   *channelEntry
)
{
   wpt_status                status = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_DescCtrlBlkType  *currentCtrlBlk = NULL;
   WLANDXE_DescType         *currentDesc    = NULL;
   wpt_uint32                descCtrl, frameCount = 0, i;
   wpt_int32                 ret_val = -1;

   currentCtrlBlk = channelEntry->headCtrlBlk;
   currentDesc    = currentCtrlBlk->linkedDesc;

   
   descCtrl = currentDesc->descCtrl.ctrl;

   while(!(WLANDXE_U32_SWAP_ENDIAN(descCtrl) & WLANDXE_DESC_CTRL_VALID) &&
         (eWLAN_PAL_STATUS_SUCCESS == wpalIsPacketLocked(currentCtrlBlk->xfrFrame)) &&
         (currentCtrlBlk->xfrFrame->pInternalData != NULL) &&
         (frameCount < WLANDXE_MAX_REAPED_RX_FRAMES) )
   {
      channelEntry->numTotalFrame++;
      channelEntry->numFreeDesc++;
#ifdef FEATURE_R33D
      
      currentDesc->xfrSize = WLANDXE_U32_SWAP_ENDIAN(WLANDXE_DEFAULT_RX_OS_BUFFER_SIZE);
      status = wpalPrepareRxFrame(&currentCtrlBlk->xfrFrame,
                                       (wpt_uint32)currentCtrlBlk->xfrFrame->pBDPhys,
                                       currentCtrlBlk->shadowBufferVa,
                                       WLANDXE_DEFAULT_RX_OS_BUFFER_SIZE);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeRXFrameReady Prepare RX Frame fail");
         return ret_val;
      }
      status = wpalFreeRxFrame(currentCtrlBlk->shadowBufferVa);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeRXFrameReady Free Shadow RX Frame fail");
         return ret_val;
      }

#else 
      status = wpalUnlockPacket(currentCtrlBlk->xfrFrame);
      if (eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeRXFrameReady unable to unlock packet");
         return ret_val;
      }
#endif 
       
      rx_reaped_buf[frameCount] = currentCtrlBlk->xfrFrame;
      frameCount++;
      currentCtrlBlk->xfrFrame = NULL;

      
      dxeRXFrameRefillRing(dxeCtxt, channelEntry);

      currentCtrlBlk = (WLANDXE_DescCtrlBlkType *)currentCtrlBlk->nextCtrlBlk;
      currentDesc    = currentCtrlBlk->linkedDesc;
      descCtrl       = currentDesc->descCtrl.ctrl;
   }

   channelEntry->headCtrlBlk = currentCtrlBlk;

   
   i = 0;
   while(i < frameCount)
   {
      dxeCtxt->rxReadyCB(dxeCtxt->clientCtxt, rx_reaped_buf[i], channelEntry->channelType);
      i++;
   }

   return frameCount;
}

static wpt_status dxeRXFrameReady
(
   WLANDXE_CtrlBlkType     *dxeCtxt,
   WLANDXE_ChannelCBType   *channelEntry,
   wpt_uint32               chStat
)
{
   wpt_status                status = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_DescCtrlBlkType  *currentCtrlBlk = NULL;
   WLANDXE_DescType         *currentDesc    = NULL;
   wpt_uint32                descCtrl;
   wpt_int32                 frameCount = 0;

   wpt_uint32                descLoop;
   wpt_uint32                invalidatedFound = 0;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if((NULL == dxeCtxt) || (NULL == channelEntry))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXFrameReady Channel Entry is not valid");
      return eWLAN_PAL_STATUS_E_INVAL;
   }

   frameCount = dxeRXFrameRouteUpperLayer(dxeCtxt, channelEntry);

   if(0 > frameCount)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXFrameReady RX frame route fail");
      return eWLAN_PAL_STATUS_E_INVAL;
   }

   if((0 == frameCount) &&
      ((WLANDXE_POWER_STATE_BMPS == dxeCtxt->hostPowerState) ||
       (WLANDXE_POWER_STATE_FULL == dxeCtxt->hostPowerState)))
   {
      if(!(chStat & WLANDXE_CH_CTRL_EN_MASK))
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                  "dxeRXFrameReady %s RING Wrapped, RX Free Low 0x%x",
                  channelType[channelEntry->channelType], chStat);
         channelEntry->numFragmentCurrentChain = 1;
         return eWLAN_PAL_STATUS_SUCCESS;
      }

      currentCtrlBlk = channelEntry->headCtrlBlk;
      currentDesc    = currentCtrlBlk->linkedDesc;
      descCtrl       = currentDesc->descCtrl.ctrl;

      if(WLANDXE_POWER_STATE_BMPS != dxeCtxt->hostPowerState)
      {
          HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
                   "RX ISR called but no frame handled PWS %d, channel %s",
                   (int)dxeCtxt->hostPowerState,
                   channelType[channelEntry->channelType]);
      }

      if(0 == channelEntry->numFragmentCurrentChain)
      {
         dxeChannelMonitor("RX Ready", channelEntry, NULL);
         dxeDescriptorDump(channelEntry, channelEntry->headCtrlBlk->linkedDesc, 0);
         dxeChannelRegisterDump(channelEntry, "RX successive empty interrupt", NULL);
         dxeChannelAllDescDump(channelEntry, channelEntry->channelType, NULL);
         
         for(descLoop = 0; descLoop < channelEntry->numDesc; descLoop++)
         {
            if(!(WLANDXE_U32_SWAP_ENDIAN(descCtrl) & WLANDXE_DESC_CTRL_VALID))
            {
               HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                        "Found Invalidated Descriptor %d", (int)descLoop);
               if(eWLAN_PAL_STATUS_SUCCESS == wpalIsPacketLocked(currentCtrlBlk->xfrFrame))
               {
                  HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                           "Packet locked, Resync Host and HW");
                  channelEntry->headCtrlBlk = currentCtrlBlk;
                  invalidatedFound = 1;
                  break;
               }
               else
               {
                  HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                           "Packet Not Locked, cannot transfer frame");
               }
            }
            currentCtrlBlk = (WLANDXE_DescCtrlBlkType *)currentCtrlBlk->nextCtrlBlk;
            currentDesc    = currentCtrlBlk->linkedDesc;
            descCtrl       = currentDesc->descCtrl.ctrl;
         }

         if((invalidatedFound) && (0 != descLoop))
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                     "Found New Sync location with HW, handle frames from there");
            frameCount = dxeRXFrameRouteUpperLayer(dxeCtxt, channelEntry);
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                     "re-sync routed %d frames to upper layer", (int)frameCount);
            channelEntry->numFragmentCurrentChain = frameCount;
         }
         else if((invalidatedFound) && (0 == descLoop))
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                     "Out of RX Low resource, and INT came in, do nothing till get RX resource");
         }
         
         else
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                     "Could not found invalidated descriptor");
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                     "RX successive empty interrupt, Could not find invalidated DESC reload driver");
            dxeCtxt->driverReloadInProcessing = eWLAN_PAL_TRUE;
            wpalWlanReload();
            dxeStartSSRTimer(dxeCtxt);
         }
      }
   }
   channelEntry->numFragmentCurrentChain = frameCount;
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

static wpt_status dxeNotifySmsm
(
  wpt_boolean kickDxe,
  wpt_boolean ringEmpty
)
{
   wpt_uint32 clrSt = 0;
   wpt_uint32 setSt = 0;

   if(kickDxe)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_MED, "Kick off DXE");

     if(tempDxeCtrlBlk->lastKickOffDxe == 0)
     {
       setSt |= WPAL_SMSM_WLAN_TX_ENABLE; 
       tempDxeCtrlBlk->lastKickOffDxe = 1;
     }
     else if(tempDxeCtrlBlk->lastKickOffDxe == 1)
     {
       clrSt |= WPAL_SMSM_WLAN_TX_ENABLE;
       tempDxeCtrlBlk->lastKickOffDxe = 0;
     }
     else
     {
       HDXE_ASSERT(0);
     }
   }
   else
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_MED, "no need to kick off DXE");
   }

   if(ringEmpty)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_MED, "SMSM Tx Ring Empty");
     clrSt |= WPAL_SMSM_WLAN_TX_RINGS_EMPTY; 
   }
   else
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_MED, "SMSM Tx Ring Not Empty");
     setSt |= WPAL_SMSM_WLAN_TX_RINGS_EMPTY; 
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_HIGH, "C%x S%x", clrSt, setSt);

   wpalNotifySmsm(clrSt, setSt);

   return eWLAN_PAL_STATUS_SUCCESS;
}

static void dxePsComplete(WLANDXE_CtrlBlkType *dxeCtxt, wpt_boolean intr_based)
{
   if( dxeCtxt->hostPowerState == WLANDXE_POWER_STATE_FULL )
   {
     return;
   }

   
   
   if((0 == dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI].numRsvdDesc) &&
      (0 == dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_HIGH_PRI].numRsvdDesc))
   {
      tempDxeCtrlBlk->ringNotEmpty = eWLAN_PAL_FALSE;
      
      if(WLANDXE_POWER_STATE_BMPS == dxeCtxt->hostPowerState)
      {
         dxeCtxt->rivaPowerState = WLANDXE_RIVA_POWER_STATE_BMPS_UNKNOWN;
         dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);
      }
   }
   else 
   {
      tempDxeCtrlBlk->ringNotEmpty = eWLAN_PAL_TRUE;

      switch(dxeCtxt->rivaPowerState)
      {
         case WLANDXE_RIVA_POWER_STATE_ACTIVE:
            
            break;
         case WLANDXE_RIVA_POWER_STATE_BMPS_UNKNOWN:
            if(intr_based)
            {
               dxeCtxt->rivaPowerState = WLANDXE_RIVA_POWER_STATE_ACTIVE;
               dxeNotifySmsm(eWLAN_PAL_TRUE, eWLAN_PAL_FALSE);
            }
            break;
         default:
            
            break;
      }
   }
}

void dxeRXEventHandler
(
   wpt_msg                 *rxReadyMsg
)
{
   wpt_msg                  *msgContent = (wpt_msg *)rxReadyMsg;
   WLANDXE_CtrlBlkType      *dxeCtxt    = NULL;
   wpt_status                status     = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                intSrc     = 0;
   WLANDXE_ChannelCBType    *channelCb  = NULL;
   wpt_uint32                chHighStat = 0;
   wpt_uint32                chLowStat  = 0;
   wpt_uint32                regValue, chanMask;

   dxeCtxt = (WLANDXE_CtrlBlkType *)(msgContent->pContext);

   if(eWLAN_PAL_TRUE == dxeCtxt->driverReloadInProcessing)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "RX Ready WLAN Driver re-loading in progress");
      return;
   }

   
   dxeRXFrameRefillRing(dxeCtxt, &dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_LOW_PRI]);
   dxeRXFrameRefillRing(dxeCtxt, &dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_HIGH_PRI]);

   dxeCtxt = (WLANDXE_CtrlBlkType *)(msgContent->pContext);
      
   if((!dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_HIGH_PRI].extraConfig.chEnabled) ||
      (!dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_LOW_PRI].extraConfig.chEnabled))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
         "DXE already stopped in RX event handler. Just return");
      return;
   }

   if((WLANDXE_POWER_STATE_IMPS == dxeCtxt->hostPowerState) ||
      (WLANDXE_POWER_STATE_DOWN == dxeCtxt->hostPowerState))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
         "%s Riva is in %d, Just Pull frames without any register touch ",
           __func__, dxeCtxt->hostPowerState);

      channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_HIGH_PRI];
      status = dxeRXFrameReady(dxeCtxt,
                               channelCb,
                               chHighStat);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeRXEventHandler Pull from RX high channel fail");        
      }
      channelCb->numFragmentCurrentChain = 1;

       
      channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_LOW_PRI];
      status = dxeRXFrameReady(dxeCtxt,
                               channelCb,
                               chLowStat);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeRXEventHandler Pull from RX low channel fail");        
      }
      
      channelCb->numFragmentCurrentChain = 1;

      
      tempDxeCtrlBlk->rxIntDisabledByIMPS = eWLAN_PAL_TRUE;

      return;
   }

   
   
   status = wpalReadRegister(WLANDXE_INT_SRC_RAW_ADDRESS,
                             &intSrc);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXEventHandler Read INT_SRC register fail");
      return;         
   }
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_MED,
            "RX Event Handler INT Source 0x%x", intSrc);

#ifndef WLANDXE_TEST_CHANNEL_ENABLE
   
   channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_HIGH_PRI];
   if(intSrc & (1 << channelCb->assignedDMAChannel))
   {
      status = dxeChannelCleanInt(channelCb, &chHighStat);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeRXEventHandler INT Clean up fail");
         return;         
      }
      if(WLANDXE_CH_STAT_INT_ERR_MASK & chHighStat)
      {
         
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                  "%11s : 0x%x Error Reported, Reload Driver",
                  channelType[channelCb->channelType], chHighStat);

         dxeErrChannelDebug(channelCb, chHighStat);

         dxeCtxt->driverReloadInProcessing = eWLAN_PAL_TRUE;
         wpalWlanReload();
         dxeStartSSRTimer(dxeCtxt);
      }
      else if((WLANDXE_CH_STAT_INT_DONE_MASK & chHighStat) ||
              (WLANDXE_CH_STAT_INT_ED_MASK & chHighStat))
      {
         
         status = dxeRXFrameReady(dxeCtxt,
                                  channelCb,
                                  chHighStat);
      }
      else if(WLANDXE_CH_STAT_MASKED_MASK & chHighStat)
      {
         status = dxeRXFrameReady(dxeCtxt,
                                  channelCb,
                                  chHighStat);
      }
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO,
               "RX HIGH CH EVNT STAT 0x%x, %d frames handled", chHighStat, channelCb->numFragmentCurrentChain);
      
      channelCb->rxDoneHistogram = (channelCb->rxDoneHistogram << 1);
      if(WLANDXE_CH_STAT_INT_DONE_MASK & chHighStat)
      {
         channelCb->rxDoneHistogram |= 1;
      }
      else
      {
         channelCb->rxDoneHistogram &= ~1;
      }
   }
#else
   
   channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_H2H_TEST_RX];
   if(intSrc & (1 << channelCb->assignedDMAChannel))
   {
      status = dxeChannelCleanInt(channelCb, &chStat);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeRXEventHandler INT Clean up fail");
         return;         
      }

      if(WLANDXE_CH_STAT_INT_ERR_MASK & chStat)
      {
         
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                  "%11s : 0x%x Error Reported, Reload Driver",
                  channelType[channelCb->channelType], chStat);

         dxeErrChannelDebug(channelCb, chStat);

         dxeCtxt->driverReloadInProcessing = eWLAN_PAL_TRUE;
         wpalWlanReload();
         dxeStartSSRTimer(dxeCtxt);
      }
      else if(WLANDXE_CH_STAT_INT_ED_MASK & chStat)
      {
         
         status = dxeRXFrameReady(dxeCtxt,
                                  channelCb,
                                  chStat);
      }
      
      channelCb->rxDoneHistogram = (channelCb->rxDoneHistogram << 1);
      if(WLANDXE_CH_STAT_INT_DONE_MASK & chStat)
      {
         channelCb->rxDoneHistogram |= 1;
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
            "DXE Channel Number %d, Rx DONE Histogram 0x%016llx",
            channelCb->assignedDMAChannel, channelCb->rxDoneHistogram);
      }
      else
      {
         channelCb->rxDoneHistogram &= ~1;
      }
   }
#endif 

   
       channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_LOW_PRI];
   if(intSrc & (1 << channelCb->assignedDMAChannel))
   {
      status = dxeChannelCleanInt(channelCb, &chLowStat);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeRXEventHandler INT Clean up fail");
         return;         
      }

      if(WLANDXE_CH_STAT_INT_ERR_MASK & chLowStat)
      {
         
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                  "%11s : 0x%x Error Reported, Reload Driver",
                  channelType[channelCb->channelType], chLowStat);

         dxeErrChannelDebug(channelCb, chLowStat);

         dxeCtxt->driverReloadInProcessing = eWLAN_PAL_TRUE;
         wpalWlanReload();
         dxeStartSSRTimer(dxeCtxt);
      }
      else if((WLANDXE_CH_STAT_INT_ED_MASK & chLowStat) ||
               (WLANDXE_CH_STAT_INT_DONE_MASK & chLowStat))
      {
         
         status = dxeRXFrameReady(dxeCtxt,
                                  channelCb,
                                  chLowStat);
       }

      
      channelCb->rxDoneHistogram = (channelCb->rxDoneHistogram << 1);
      if(WLANDXE_CH_STAT_INT_DONE_MASK & chLowStat)
      {
         channelCb->rxDoneHistogram |= 1;
      }
      else
      {
         channelCb->rxDoneHistogram &= ~1;
      }
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO,
               "RX LOW CH EVNT STAT 0x%x, %d frames handled", chLowStat, channelCb->numFragmentCurrentChain);
   }
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXEventHandler Handle Frame Ready Fail");
      return;         
   }

   
   if(!(dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_HIGH_PRI].extraConfig.chan_mask & WLANDXE_CH_CTRL_EN_MASK))
   {
      HDXE_ASSERT(0);
   }

   if (dxeCtxt->rxPalPacketUnavailable &&
       (WLANDXE_CH_STAT_INT_DONE_MASK & chHighStat))
   {
     chanMask = dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_HIGH_PRI].extraConfig.chan_mask &
                (~WLANDXE_CH_CTRL_INE_DONE_MASK);
   }
   else
   {
     chanMask = dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_HIGH_PRI].extraConfig.chan_mask;
   }
   wpalWriteRegister(dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_HIGH_PRI].channelRegister.chDXECtrlRegAddr,
                     chanMask);

   
   if(!(dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_LOW_PRI].extraConfig.chan_mask & WLANDXE_CH_CTRL_EN_MASK))
   {
      HDXE_ASSERT(0);
   }

   if (dxeCtxt->rxPalPacketUnavailable &&
       (WLANDXE_CH_STAT_INT_DONE_MASK & chLowStat))
   {
     chanMask = dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_LOW_PRI].extraConfig.chan_mask &
                (~WLANDXE_CH_CTRL_INE_DONE_MASK);
   }
   else
   {
     chanMask = dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_LOW_PRI].extraConfig.chan_mask;
   }
   wpalWriteRegister(dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_LOW_PRI].channelRegister.chDXECtrlRegAddr,
                     chanMask);


   wpalReadRegister(WLANDXE_INT_MASK_REG_ADDRESS, &regValue);
   regValue &= WLANDXE_RX_INTERRUPT_PRO_UNMASK;
   wpalWriteRegister(WLANDXE_INT_MASK_REG_ADDRESS, regValue);

   
   
   status = wpalEnableInterrupt(DXE_INTERRUPT_RX_READY);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXEventHandler Enable RX Ready interrupt fail");
      return;
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return;
}

void dxeRXPacketAvailableEventHandler
(
   wpt_msg                 *rxPktAvailMsg
)
{
   WLANDXE_CtrlBlkType      *dxeCtxt    = NULL;
   wpt_status                status     = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_ChannelCBType    *channelCb  = NULL;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if(NULL == rxPktAvailMsg)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXPacketAvailableEventHandler Context is not valid");
      return;
   }

   dxeCtxt    = (WLANDXE_CtrlBlkType *)(rxPktAvailMsg->pContext);

#ifdef WLAN_DXE_LOW_RESOURCE_TIMER
   if(VOS_TIMER_STATE_RUNNING ==
      wpalTimerGetCurStatus(&dxeCtxt->rxResourceAvailableTimer))
   {
      wpalTimerStop(&dxeCtxt->rxResourceAvailableTimer);
   }
#endif

   do
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "dxeRXPacketAvailableEventHandler, start refilling ring");

      channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_HIGH_PRI];
      status = dxeRXFrameRefillRing(dxeCtxt,channelCb);
   
      
      
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         break;
      }
   
      channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_RX_LOW_PRI];
      status = dxeRXFrameRefillRing(dxeCtxt,channelCb);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         break;
      }
   } while(0);

   if((WLANDXE_POWER_STATE_IMPS == dxeCtxt->hostPowerState) ||
      (WLANDXE_POWER_STATE_DOWN == dxeCtxt->hostPowerState))
   {
      
      tempDxeCtrlBlk->rxIntDisabledByIMPS = eWLAN_PAL_TRUE;
   }
}

static void dxeRXISR
(
   void                    *hostCtxt
)
{
   WLANDXE_CtrlBlkType      *dxeCtxt    = (WLANDXE_CtrlBlkType *)hostCtxt;
   wpt_status                status     = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                regValue;

#ifdef FEATURE_R33D
   status = wpalReadRegister(WLANDXE_INT_SRC_RAW_ADDRESS,
                                  &regValue);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXCompISR Read INT_SRC_RAW fail");
      return;
   }
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
            "INT_SRC_RAW 0x%x", regValue);
   if(0 == regValue)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "This is not DXE Interrupt, Reject it 0x%x", regValue);
      return;
   }
#endif 

   wpalReadRegister(WLANDXE_INT_MASK_REG_ADDRESS, &regValue);
   regValue |= WLANPAL_RX_INTERRUPT_PRO_MASK;
   wpalWriteRegister(WLANDXE_INT_MASK_REG_ADDRESS, regValue);

   status = wpalDisableInterrupt(DXE_INTERRUPT_RX_READY);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXFrameReadyISR Disable RX ready interrupt fail");
      return;         
   }

   
   if(NULL == dxeCtxt->rxIsrMsg)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeRXFrameReadyISR NULL message");
      HDXE_ASSERT(0);
      return;
   }

   status = wpalPostRxMsg(WDI_GET_PAL_CTX(),
                          dxeCtxt->rxIsrMsg);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "dxeRXFrameReadyISR interrupt serialize fail");
   }

   return;
}

static wpt_status dxeTXPushFrame
(
   WLANDXE_ChannelCBType   *channelEntry,
   wpt_packet              *palPacket
)
{
   wpt_status                  status = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_DescCtrlBlkType    *currentCtrlBlk = NULL;
   WLANDXE_DescType           *currentDesc    = NULL;
   WLANDXE_DescType           *firstDesc      = NULL;
   WLANDXE_DescType           *LastDesc       = NULL;
   void                       *sourcePhysicalAddress = NULL;
   wpt_uint32                  xferSize = 0;
#ifdef FEATURE_R33D
   tx_frm_pcie_vector_t        frameVector;
   wpt_uint32                  Va;
   wpt_uint32                  fragCount = 0;
#else
   wpt_iterator                iterator;
#endif 
   wpt_uint32                  isEmpty = 0;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   tempDxeCtrlBlk->smsmToggled = eWLAN_PAL_FALSE;
   if((0 == tempDxeCtrlBlk->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI].numRsvdDesc) &&
      (0 == tempDxeCtrlBlk->dxeChannel[WDTS_CHANNEL_TX_HIGH_PRI].numRsvdDesc))
   {
      isEmpty = 1;
   }

   channelEntry->numFragmentCurrentChain = 0;
   currentCtrlBlk = channelEntry->headCtrlBlk;

   
#ifdef FEATURE_R33D
   memset(&frameVector, 0, sizeof(tx_frm_pcie_vector_t));
   status = wpalPrepareTxFrame(palPacket,
                                    &frameVector,
                                    &Va);
#else
   status = wpalLockPacketForTransfer(palPacket);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXPushFrame unable to lock packet");
      return status;
   }

   status = wpalIteratorInit(&iterator, palPacket);
#endif 
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXPushFrame iterator init fail");
      return status;
   }

   
   while(1)
   {
      
      currentDesc = currentCtrlBlk->linkedDesc;
      if(NULL == firstDesc)
      {
         firstDesc = currentCtrlBlk->linkedDesc;
      }
      currentCtrlBlk->xfrFrame = palPacket;

#ifdef FEATURE_R33D
      if(fragCount == frameVector.num_frg)
      {
         break;
      }
      currentCtrlBlk->shadowBufferVa = frameVector.frg[0].va;
      sourcePhysicalAddress          = (void *)frameVector.frg[fragCount].pa;
      xferSize                       = frameVector.frg[fragCount].size;
      fragCount++;
      if(0 == xferSize)
      {
          HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXPushFrame invalid transfer size");

          HDXE_ASSERT(0);
          return eWLAN_PAL_STATUS_E_FAILURE;
      }
      if(NULL == sourcePhysicalAddress)
      {
          HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
              "dxeTXPushFrame invalid sourcePhysicalAddress");
          HDXE_ASSERT(0);
          return eWLAN_PAL_STATUS_E_FAILURE;
      }
#else
      status = wpalIteratorNext(&iterator,
                                palPacket,
                                &sourcePhysicalAddress,
                                &xferSize);
      if((NULL == sourcePhysicalAddress) ||
         (0    == xferSize))
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
                  "dxeTXPushFrame end of current frame");
         break;
      }
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXPushFrame Get next frame fail");
         return status;
      }
#endif 

      
      LastDesc    = currentCtrlBlk->linkedDesc;

      
      currentDesc->dxedesc.dxe_short_desc.srcMemAddrL =
                               WLANDXE_U32_SWAP_ENDIAN((wpt_uint32)sourcePhysicalAddress);

      
      if((WDTS_CHANNEL_TX_LOW_PRI  == channelEntry->channelType) ||
         (WDTS_CHANNEL_TX_HIGH_PRI == channelEntry->channelType))
      {
         currentDesc->dxedesc.dxe_short_desc.dstMemAddrL =
                                WLANDXE_U32_SWAP_ENDIAN(channelEntry->channelConfig.refWQ);
      }
      else
      {
      }
      currentDesc->xfrSize = WLANDXE_U32_SWAP_ENDIAN(xferSize);

      
      
      if(0 == channelEntry->numFragmentCurrentChain)
      {
         currentDesc->descCtrl.ctrl = channelEntry->extraConfig.cw_ctrl_write;
      }
      else
      {
         currentDesc->descCtrl.ctrl = channelEntry->extraConfig.cw_ctrl_write_valid;
      }

      
      channelEntry->numFragmentCurrentChain++;
      channelEntry->numFreeDesc--;
      channelEntry->numRsvdDesc++;

      
      currentCtrlBlk = currentCtrlBlk->nextCtrlBlk;
   }
   channelEntry->numTotalFrame++;
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "NUM TX FRAG %d, Total Frame %d",
            channelEntry->numFragmentCurrentChain, channelEntry->numTotalFrame);

   if(NULL == LastDesc)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXPushFrame NULL Last Descriptor, broken chain");
      return eWLAN_PAL_STATUS_E_FAULT;
   }
   LastDesc->descCtrl.ctrl  = channelEntry->extraConfig.cw_ctrl_write_eop_int;
   firstDesc->descCtrl.ctrl = channelEntry->extraConfig.cw_ctrl_write_valid;

   
   if(WLANDXE_RIVA_POWER_STATE_BMPS_UNKNOWN == tempDxeCtrlBlk->rivaPowerState)
   {
      
      channelEntry->headCtrlBlk = currentCtrlBlk;
      if(isEmpty)
      {
         tempDxeCtrlBlk->ringNotEmpty = eWLAN_PAL_TRUE;
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
                  "SMSM_ret LO=%d HI=%d",
                  tempDxeCtrlBlk->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI].numRsvdDesc,
                  tempDxeCtrlBlk->dxeChannel[WDTS_CHANNEL_TX_HIGH_PRI].numRsvdDesc );
         dxeNotifySmsm(eWLAN_PAL_TRUE, eWLAN_PAL_FALSE);
         tempDxeCtrlBlk->smsmToggled = eWLAN_PAL_TRUE;
      }
      return status;
   }

   if(channelEntry->extraConfig.chan_mask & WLANDXE_CH_CTRL_EDEN_MASK)
   {
      if(channelEntry->extraConfig.cw_ctrl_write_valid != firstDesc->descCtrl.ctrl)
      {
         
      }

      status = wpalWriteRegister(channelEntry->channelRegister.chDXECtrlRegAddr,
                                      channelEntry->extraConfig.chan_mask);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXPushFrame Write Channel Ctrl Register fail");
         return status;
      }

      
      channelEntry->headCtrlBlk = currentCtrlBlk;

      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
               "%s Exit", __func__);
      return status;
   }

   
   if((WDTS_CHANNEL_TX_LOW_PRI  == channelEntry->channelType) ||
      (WDTS_CHANNEL_TX_HIGH_PRI == channelEntry->channelType))
   {
      
      status = wpalWriteRegister(channelEntry->channelRegister.chDXEDadrlRegAddr,
                                      channelEntry->channelConfig.refWQ);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXPushFrame Program dest address register fail");
         return status;
      }
      
      if(channelEntry->channelConfig.useShortDescFmt)
      {
         status = wpalWriteRegister(channelEntry->channelRegister.chDXEDadrhRegAddr,
                                         0);
         if(eWLAN_PAL_STATUS_SUCCESS != status)
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                     "dxeTXPushFrame Program dest address register fail");
            return status;
         }
      }
      else
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXPushFrame LONG Descriptor Format!!!");
      }
   }
#ifdef WLANDXE_TEST_CHANNEL_ENABLE
   else if(WDTS_CHANNEL_H2H_TEST_TX  == channelEntry->channelType)
   {
      
      status = wpalWriteRegister(channelEntry->channelRegister.chDXEDadrlRegAddr,
                                      WLANDXE_U32_SWAP_ENDIAN(firstDesc->dxedesc.dxe_short_desc.dstMemAddrL));
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXPushFrame Program dest address register fail");
         return status;
      }
      
      if(channelEntry->channelConfig.useShortDescFmt)
      {
         status = wpalWriteRegister(channelEntry->channelRegister.chDXEDadrhRegAddr,
                                         0);
         if(eWLAN_PAL_STATUS_SUCCESS != status)
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                     "dxeTXPushFrame Program dest address register fail");
            return status;
         }
      }
      else
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXPushFrame LONG Descriptor Format!!!");
      }
   }
#endif 

   status = wpalWriteRegister(channelEntry->channelRegister.chDXESadrlRegAddr,
                                   WLANDXE_U32_SWAP_ENDIAN(firstDesc->dxedesc.dxe_short_desc.srcMemAddrL));
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXPushFrame Program src address register fail");
      return status;
   }
   
   if(channelEntry->channelConfig.useShortDescFmt)
   {
      status = wpalWriteRegister(channelEntry->channelRegister.chDXESadrhRegAddr,
                                      0);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXPushFrame Program dest address register fail");
         return status;
      }
   }
   else
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXPushFrame LONG Descriptor Format!!!");
   }

   
   status = wpalWriteRegister(channelEntry->channelRegister.chDXEDesclRegAddr,
                                   channelEntry->headCtrlBlk->linkedDescPhyAddr);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXPushFrame Write DESC Address register fail");
      return status;
   }
   
   if(channelEntry->channelConfig.useShortDescFmt)
   {
      status = wpalWriteRegister(channelEntry->channelRegister.chDXEDeschRegAddr,
                                      0);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXPushFrame Program dest address register fail");
         return status;
      }
   }
   else
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXPushFrame LONG Descriptor Format!!!");
   }

   
   xferSize = WLANDXE_U32_SWAP_ENDIAN(firstDesc->xfrSize);
   status = wpalWriteRegister(channelEntry->channelRegister.chDXESzRegAddr,
                                   xferSize);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXPushFrame Write DESC Address register fail");
      return status;
   }

   status = wpalWriteRegister(channelEntry->channelRegister.chDXECtrlRegAddr,
                                   channelEntry->extraConfig.chan_mask);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXPushFrame Write Channel Ctrl Register fail");
      return status;
   }

   
   channelEntry->headCtrlBlk = currentCtrlBlk;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

static wpt_status dxeTXCompFrame
(
   WLANDXE_CtrlBlkType     *hostCtxt,
   WLANDXE_ChannelCBType   *channelEntry
)
{
   wpt_status                status = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_DescCtrlBlkType  *currentCtrlBlk = NULL;
   WLANDXE_DescType         *currentDesc    = NULL;
   wpt_uint32                descCtrlValue  = 0;
   unsigned int             *lowThreshold   = NULL;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if((NULL == hostCtxt) || (NULL == channelEntry))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXCompFrame Invalid ARG");
      return eWLAN_PAL_STATUS_E_INVAL;
   }

   if(NULL == hostCtxt->txCompCB)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXCompFrame TXCompCB is not registered");
      return eWLAN_PAL_STATUS_SUCCESS;
   }

   status = wpalMutexAcquire(&channelEntry->dxeChannelLock);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXCompFrame Mutex Acquire fail");
      return status;
   }
   
   currentCtrlBlk = channelEntry->tailCtrlBlk;
   currentDesc    = currentCtrlBlk->linkedDesc;

   if( currentCtrlBlk == channelEntry->headCtrlBlk )
   {
      status = wpalMutexRelease(&channelEntry->dxeChannelLock);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXCompFrame Mutex Release fail");
         return status;
      }
      return eWLAN_PAL_STATUS_SUCCESS;
   }

   
   while(1)
   {
      descCtrlValue = currentDesc->descCtrl.ctrl;
      if((descCtrlValue & WLANDXE_DESC_CTRL_VALID))
      {
         
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_MED,
                  "dxeTXCompFrame caught up with head - next DESC has VALID set");
         break;
      }

      if(currentCtrlBlk->xfrFrame == NULL)
      {
          HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "Invalid transfer frame");
          HDXE_ASSERT(0);
          break;
      }
      channelEntry->numFreeDesc++;
      channelEntry->numRsvdDesc--;

      
      if(WLANDXE_U32_SWAP_ENDIAN(descCtrlValue) & WLANDXE_DESC_CTRL_EOP)
      {
         hostCtxt->txCompletedFrames--;
#ifdef FEATURE_R33D
         wpalFreeTxFrame(currentCtrlBlk->shadowBufferVa);
#else
         status = wpalUnlockPacket(currentCtrlBlk->xfrFrame);
         if (eWLAN_PAL_STATUS_SUCCESS != status)
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                     "dxeRXFrameReady unable to unlock packet");
            status = wpalMutexRelease(&channelEntry->dxeChannelLock);
            if(eWLAN_PAL_STATUS_SUCCESS != status)
            {
               HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                        "dxeTXCompFrame Mutex Release fail");
            }
            return status;
         }
#endif 
         hostCtxt->txCompCB(hostCtxt->clientCtxt,
                            currentCtrlBlk->xfrFrame,
                            eWLAN_PAL_STATUS_SUCCESS);
         channelEntry->numFragmentCurrentChain = 0;
      }
      currentCtrlBlk = currentCtrlBlk->nextCtrlBlk;
      currentDesc    = currentCtrlBlk->linkedDesc;

      if(currentCtrlBlk == channelEntry->headCtrlBlk)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_MED,
                  "dxeTXCompFrame caught up with head ptr");
         break;
      }
      
   }

   
   channelEntry->tailCtrlBlk = currentCtrlBlk;

   lowThreshold = channelEntry->channelType == WDTS_CHANNEL_TX_LOW_PRI?
      &(hostCtxt->txCompInt.txLowResourceThreshold_LoPriCh):
      &(hostCtxt->txCompInt.txLowResourceThreshold_HiPriCh);

   
   if((eWLAN_PAL_TRUE == channelEntry->hitLowResource) &&
      (channelEntry->numFreeDesc > *lowThreshold))
   {
      
      if(WLANDXE_TX_LOW_RES_THRESHOLD > *lowThreshold)
      {
         *lowThreshold = WLANDXE_TX_LOW_RES_THRESHOLD;
      }

      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "DXE TX %d channel recovered from low resource", channelEntry->channelType);
      hostCtxt->lowResourceCB(hostCtxt->clientCtxt,
                              channelEntry->channelType,
                              eWLAN_PAL_TRUE);
      channelEntry->hitLowResource = eWLAN_PAL_FALSE;
      wpalTimerStop(&channelEntry->healthMonitorTimer);
   }

   status = wpalMutexRelease(&channelEntry->dxeChannelLock);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXCompFrame Mutex Release fail");
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

void dxeTXEventHandler
(
   wpt_msg               *msgPtr
)
{
   wpt_msg                  *msgContent = (wpt_msg *)msgPtr;
   WLANDXE_CtrlBlkType      *dxeCtxt    = NULL;
   wpt_status                status     = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                intSrc     = 0;
   wpt_uint32                chStat     = 0;
   WLANDXE_ChannelCBType    *channelCb  = NULL;

   wpt_uint8                 bEnableISR = 0;
   static wpt_uint8          successiveIntWithIMPS;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   dxeCtxt = (WLANDXE_CtrlBlkType *)(msgContent->pContext);
   dxeCtxt->ucTxMsgCnt = 0;
   
   if(eWLAN_PAL_TRUE == dxeCtxt->driverReloadInProcessing)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "wlan: TX COMP WLAN Driver re-loading in progress");
      return;
   }

   
   if(WLANDXE_POWER_STATE_IMPS == dxeCtxt->hostPowerState)
   {
      successiveIntWithIMPS++;
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXEventHandler IMPS TX COMP INT successiveIntWithIMPS %d", successiveIntWithIMPS);
      status = dxeTXCompFrame(dxeCtxt, &dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_HIGH_PRI]);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXEventHandler IMPS HC COMP interrupt fail");
      }

      status = dxeTXCompFrame(dxeCtxt, &dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI]);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXEventHandler IMPS LC COMP interrupt fail");
      }

      if(((dxeCtxt->txCompletedFrames) &&
         (eWLAN_PAL_FALSE == dxeCtxt->txIntEnable)) &&
         (successiveIntWithIMPS == 1))
      {
         dxeCtxt->txIntEnable =  eWLAN_PAL_TRUE; 
         wpalEnableInterrupt(DXE_INTERRUPT_TX_COMPLE);
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                     "TX COMP INT Enabled, remain TX frame count on ring %d",
                     dxeCtxt->txCompletedFrames);
         dxePsComplete(dxeCtxt, eWLAN_PAL_TRUE);
      }
      else
      {
         dxeCtxt->txIntEnable =  eWLAN_PAL_FALSE;
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "TX COMP INT NOT Enabled, RIVA still wake up? remain TX frame count on ring %d, successiveIntWithIMPS %d",
                  dxeCtxt->txCompletedFrames, successiveIntWithIMPS);
      }
      return;
   }

   successiveIntWithIMPS = 0;
   if((!dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_HIGH_PRI].extraConfig.chEnabled) ||
      (!dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI].extraConfig.chEnabled))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
         "DXE already stopped in TX event handler. Just return");
      return;
   }

   
   
   status = wpalReadRegister(WLANDXE_INT_SRC_RAW_ADDRESS,
                                  &intSrc);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXCompleteEventHandler Read INT_DONE_SRC register fail");
      return;         
   }
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_MED,
            "TX Event Handler INT Source 0x%x", intSrc);

   
   channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_HIGH_PRI];
   if(intSrc & (1 << channelCb->assignedDMAChannel))
   {
      status = dxeChannelCleanInt(channelCb, &chStat);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXEventHandler INT Clean up fail");
         return;         
      }

      if(WLANDXE_CH_STAT_INT_ERR_MASK & chStat)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                  "%11s : 0x%x Error Reported, Reload Driver",
                  channelType[channelCb->channelType], chStat);

         dxeErrChannelDebug(channelCb, chStat);

         dxeCtxt->driverReloadInProcessing = eWLAN_PAL_TRUE;
         wpalWlanReload();
         dxeStartSSRTimer(dxeCtxt);
      }
      else if(WLANDXE_CH_STAT_INT_DONE_MASK & chStat)
      {
         
         status = dxeTXCompFrame(dxeCtxt,
                                 channelCb);
         bEnableISR = 1;
      }
      else if(WLANDXE_CH_STAT_INT_ED_MASK & chStat)
      {
         
         status = dxeTXCompFrame(dxeCtxt,
                                 channelCb);
         bEnableISR = 1;
      }
      else
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                 "dxeTXEventHandler TX HI status=%x", chStat);
      }

      if(WLANDXE_CH_STAT_MASKED_MASK & chStat)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_HIGH,
                  "dxeTXEventHandler TX HIGH Channel Masked Unmask it!!!!");
      }

      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_HIGH,
               "TX HIGH STAT 0x%x RESRVD %d", chStat, channelCb->numRsvdDesc);
   }

   
   channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI];
   if(intSrc & (1 << channelCb->assignedDMAChannel))
   {
      status = dxeChannelCleanInt(channelCb, &chStat);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeTXEventHandler INT Clean up fail");
         return;         
      }

      if(WLANDXE_CH_STAT_INT_ERR_MASK & chStat)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                  "%11s : 0x%x Error Reported, Reload Driver",
                  channelType[channelCb->channelType], chStat);

         dxeErrChannelDebug(channelCb, chStat);

         dxeCtxt->driverReloadInProcessing = eWLAN_PAL_TRUE;
         wpalWlanReload();
         dxeStartSSRTimer(dxeCtxt);
      }
      else if(WLANDXE_CH_STAT_INT_DONE_MASK & chStat)
      {
         
         status = dxeTXCompFrame(dxeCtxt,
                                 channelCb);
         bEnableISR = 1;
      }
      else if(WLANDXE_CH_STAT_INT_ED_MASK & chStat)
      {
         
         status = dxeTXCompFrame(dxeCtxt,
                                 channelCb);
         bEnableISR = 1;
      }
      else
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                 "dxeTXEventHandler TX LO status=%x", chStat);
      }

      if(WLANDXE_CH_STAT_MASKED_MASK & chStat)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_HIGH,
                  "dxeTXEventHandler TX Low Channel Masked Unmask it!!!!");
      }
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO,
               "TX LOW STAT 0x%x RESRVD %d", chStat, channelCb->numRsvdDesc);
   }


#ifdef WLANDXE_TEST_CHANNEL_ENABLE
   
   channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_H2H_TEST_TX];
   if(intSrc & (1 << channelCb->assignedDMAChannel))
   {
      status = wpalReadRegister(channelCb->channelRegister.chDXEStatusRegAddr,
                                     &chStat);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeChannelCleanInt Read CH STAT register fail");
         return;         
      }

      if(WLANDXE_CH_STAT_INT_ERR_MASK & chStat)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                  "%11s : 0x%x Error Reported, Reload Driver",
                  channelType[channelCb->channelType], chStat);

         dxeErrChannelDebug(channelCb, chStat);

         dxeCtxt->driverReloadInProcessing = eWLAN_PAL_TRUE;
         wpalWlanReload();
         dxeStartSSRTimer(dxeCtxt);
      }
      else if(WLANDXE_CH_STAT_INT_DONE_MASK & chStat)
      {
         
         status = dxeTXCompFrame(dxeCtxt,
                                 channelCb);
         if(eWLAN_PAL_STATUS_SUCCESS != status)
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                     "dxeTXEventHandler INT Clean up fail");
            return;         
         }
      }
      else
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "unexpected channel state %d", chStat);
      }
   }
#endif 

   if((bEnableISR || (dxeCtxt->txCompletedFrames)) &&
      (eWLAN_PAL_FALSE == dxeCtxt->txIntEnable))
   {
      dxeCtxt->txIntEnable =  eWLAN_PAL_TRUE; 
      wpalEnableInterrupt(DXE_INTERRUPT_TX_COMPLE);
      if(0 != dxeCtxt->txCompletedFrames)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                  "TX COMP INT Enabled, remain TX frame count on ring %d",
                  dxeCtxt->txCompletedFrames);
      }
   }

   dxePsComplete(dxeCtxt, eWLAN_PAL_TRUE);

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return;
}


void dxeTXCompleteProcessing
(
   WLANDXE_CtrlBlkType *dxeCtxt
)
{
   wpt_status                status     = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_ChannelCBType    *channelCb  = NULL;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);
  
   
   channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_HIGH_PRI];

   
   status = dxeTXCompFrame(dxeCtxt, channelCb);

   
   channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI];

   
   status = dxeTXCompFrame(dxeCtxt, channelCb);
  
   if((eWLAN_PAL_FALSE == dxeCtxt->txIntEnable) &&
      ((dxeCtxt->txCompletedFrames > 0) ||
       (WLANDXE_POWER_STATE_FULL == dxeCtxt->hostPowerState)))
   {
      dxeCtxt->txIntEnable =  eWLAN_PAL_TRUE; 
      wpalEnableInterrupt(DXE_INTERRUPT_TX_COMPLE);
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "%s %s : %d, %s : %d", __func__,
               channelType[dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_HIGH_PRI].channelType],
               dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_HIGH_PRI].numRsvdDesc,
               channelType[dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI].channelType],
               dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI].numRsvdDesc);

      if((WLANDXE_POWER_STATE_FULL != dxeCtxt->hostPowerState) &&
         (eWLAN_PAL_FALSE == tempDxeCtrlBlk->smsmToggled))
      {
         dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);
         dxeNotifySmsm(eWLAN_PAL_TRUE, eWLAN_PAL_FALSE);
      }
   }
   
   dxePsComplete(dxeCtxt, eWLAN_PAL_FALSE);
   
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return;
}

void dxeTXReSyncDesc
(
   wpt_msg                  *msgPtr
)
{
   wpt_msg                  *msgContent = (wpt_msg *)msgPtr;
   WLANDXE_CtrlBlkType      *pDxeCtrlBlk;
   wpt_uint32                nextDescReg;
   WLANDXE_ChannelCBType    *channelEntry;
   WLANDXE_DescCtrlBlkType  *validCtrlBlk;
   wpt_uint32                descLoop;
   wpt_uint32                channelLoop;

   if(NULL == msgContent)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXReSyncDesc Invalid Control Block");
      return;
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "dxeTXReSyncDesc Try to re-sync TX channel if any problem");
   pDxeCtrlBlk = (WLANDXE_CtrlBlkType *)(msgContent->pContext);

   for(channelLoop = WDTS_CHANNEL_TX_LOW_PRI; channelLoop < WDTS_CHANNEL_RX_LOW_PRI; channelLoop++)
   {
      channelEntry = &pDxeCtrlBlk->dxeChannel[channelLoop];
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
               "%11s : Try to detect TX descriptor async", channelType[channelEntry->channelType]);
      wpalReadRegister(channelEntry->channelRegister.chDXEDesclRegAddr,
                       &nextDescReg);
      
      if(channelEntry->tailCtrlBlk == channelEntry->headCtrlBlk)
      {
         if(nextDescReg != channelEntry->tailCtrlBlk->linkedDescPhyAddr)
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                     "TX Async no Pending frame");

            dxeChannelMonitor("!!! TX Async no Pending frame !!!", channelEntry, NULL);
            dxeChannelRegisterDump(channelEntry, "!!! TX Async no Pending frame !!!", NULL);

            wpalWriteRegister(channelEntry->channelRegister.chDXEDesclRegAddr,
                              channelEntry->tailCtrlBlk->linkedDescPhyAddr);
         }
      }
      else
      {
         validCtrlBlk = channelEntry->tailCtrlBlk;
         for(descLoop = 0; descLoop < channelEntry->numDesc; descLoop++)
         {
            if(validCtrlBlk->linkedDesc->descCtrl.ctrl & WLANDXE_DESC_CTRL_VALID)
            {
               if(nextDescReg != validCtrlBlk->linkedDescPhyAddr)
               {
                  HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                           "TX Async");

                  dxeChannelMonitor("!!! TX Async !!!", channelEntry, NULL);
                  dxeChannelRegisterDump(channelEntry, "!!! TX Async !!!", NULL);

                  wpalWriteRegister(channelEntry->channelRegister.chDXEDesclRegAddr,
                                    validCtrlBlk->linkedDescPhyAddr);
               }
               break;
            }
            validCtrlBlk = (WLANDXE_DescCtrlBlkType *)validCtrlBlk->nextCtrlBlk;
            if(validCtrlBlk == channelEntry->headCtrlBlk->nextCtrlBlk)
            {
               if(nextDescReg != channelEntry->headCtrlBlk->linkedDescPhyAddr)
               {
                  HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                           "TX Async with not completed transferred frames, next descriptor must be head");

                  dxeChannelMonitor("!!! TX Async !!!", channelEntry, NULL);
                  dxeChannelRegisterDump(channelEntry, "!!! TX Async !!!", NULL);

                  wpalWriteRegister(channelEntry->channelRegister.chDXEDesclRegAddr,
                                    validCtrlBlk->linkedDescPhyAddr);
               }
               break;
            }
         }
      }
   }

   for(channelLoop = WDTS_CHANNEL_TX_LOW_PRI; channelLoop < WDTS_CHANNEL_RX_LOW_PRI; channelLoop++)
   {
      channelEntry = &pDxeCtrlBlk->dxeChannel[channelLoop];
      if(channelEntry->tailCtrlBlk == channelEntry->headCtrlBlk)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
                  "%11s : No TX Pending frame",
                  channelType[channelEntry->channelType]);
         
      }
      else
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                  "%11s : TX Pending frame, process it",
                  channelType[channelEntry->channelType]);
         validCtrlBlk = channelEntry->tailCtrlBlk;
         for(descLoop = 0; descLoop < channelEntry->numDesc; descLoop++)
         {
            if(validCtrlBlk->linkedDesc->descCtrl.ctrl & WLANDXE_DESC_CTRL_VALID)
            {
               HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
                        "%11s : when exit IMPS found valid descriptor",
                        channelType[channelEntry->channelType]);

               
               wpalWriteRegister(channelEntry->channelRegister.chDXECtrlRegAddr,
                                 channelEntry->extraConfig.chan_mask);
               break;
            }
            validCtrlBlk = (WLANDXE_DescCtrlBlkType *)validCtrlBlk->nextCtrlBlk;
            if(validCtrlBlk == channelEntry->headCtrlBlk->nextCtrlBlk)
            {
               break;
            }
         }
      }
   }

   wpalMemoryFree(msgPtr);
   return;
}

void dxeDebugTxDescReSync
(
   wpt_msg                  *msgPtr
)
{
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "%s: Check for DXE TX Async",__func__);
   
   dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);
   dxeNotifySmsm(eWLAN_PAL_TRUE, eWLAN_PAL_FALSE);

   wpalSleep(10);

   dxeTXReSyncDesc(msgPtr);
}
static void dxeTXISR
(
   void                    *hostCtxt
)
{
   WLANDXE_CtrlBlkType      *dxeCtxt    = (WLANDXE_CtrlBlkType *)hostCtxt;
   wpt_status                status  = eWLAN_PAL_STATUS_SUCCESS;
#ifdef FEATURE_R33D
   wpt_uint32                regValue;
#endif 

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if(WLANDXE_POWER_STATE_DOWN == dxeCtxt->hostPowerState)
   {
      dxeCtxt->txIntEnable = eWLAN_PAL_FALSE;
      status = wpalDisableInterrupt(DXE_INTERRUPT_TX_COMPLE);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "dxeRXFrameReadyISR Disable RX ready interrupt fail");
         return;         
      }
      dxeCtxt->txIntDisabledByIMPS = eWLAN_PAL_TRUE;
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
         "%s Riva is in %d, return from here ", __func__, dxeCtxt->hostPowerState);
      return;
   }

#ifdef FEATURE_R33D
   status = wpalReadRegister(WLANDXE_INT_SRC_RAW_ADDRESS,
                                  &regValue);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXCompISR Read INT_SRC_RAW fail");
      return;
   }
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "INT_SRC_RAW 0x%x", regValue);
   if(0 == regValue)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "This is not DXE Interrupt, Reject it");
      return;
   }
#endif 

   
   status = wpalDisableInterrupt(DXE_INTERRUPT_TX_COMPLE);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXCompISR Disable TX complete interrupt fail");
      return;         
   }
   dxeCtxt->txIntEnable = eWLAN_PAL_FALSE;


   if( dxeCtxt->ucTxMsgCnt )
   {
    HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO,
                 "Avoiding serializing TX Complete event");
    return;
   }
   
   dxeCtxt->ucTxMsgCnt = 1;

   
   if(NULL == dxeCtxt->txIsrMsg)
   {
       HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "Invalid message");
       HDXE_ASSERT(0);
       return;
   }
   status = wpalPostTxMsg(WDI_GET_PAL_CTX(),
                          dxeCtxt->txIsrMsg);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "dxeTXCompISR interrupt serialize fail status=%d", status);
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return;
}

void *WLANDXE_Open
(
   void
)
{
   wpt_status              status = eWLAN_PAL_STATUS_SUCCESS;
   unsigned int            idx;
   WLANDXE_ChannelCBType  *currentChannel = NULL;
   int                     smsmInitState;
#ifdef WLANDXE_TEST_CHANNEL_ENABLE
   wpt_uint32                 sIdx;
   WLANDXE_ChannelCBType     *channel = NULL;
   WLANDXE_DescCtrlBlkType   *crntDescCB = NULL;
   WLANDXE_DescCtrlBlkType   *nextDescCB = NULL;
#endif 

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   tempDxeCtrlBlk = (WLANDXE_CtrlBlkType *)wpalMemoryAllocate(sizeof(WLANDXE_CtrlBlkType));
   if(NULL == tempDxeCtrlBlk)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Open Control Block Alloc Fail");
      return NULL;  
   }
   wpalMemoryZero(tempDxeCtrlBlk, sizeof(WLANDXE_CtrlBlkType));

   status = dxeCommonDefaultConfig(tempDxeCtrlBlk);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Open Common Configuration Fail");
      WLANDXE_Close(tempDxeCtrlBlk);
      return NULL;         
   }

   for(idx = 0; idx < WDTS_CHANNEL_MAX; idx++)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "WLANDXE_Open Channel %s Open Start", channelType[idx]);
      currentChannel = &tempDxeCtrlBlk->dxeChannel[idx];
      if(idx == WDTS_CHANNEL_TX_LOW_PRI)
      {
         currentChannel->channelType = WDTS_CHANNEL_TX_LOW_PRI;
      }
      else if(idx == WDTS_CHANNEL_TX_HIGH_PRI)
      {
         currentChannel->channelType = WDTS_CHANNEL_TX_HIGH_PRI;
      }
      else if(idx == WDTS_CHANNEL_RX_LOW_PRI)
      {
         currentChannel->channelType = WDTS_CHANNEL_RX_LOW_PRI;
      }
      else if(idx == WDTS_CHANNEL_RX_HIGH_PRI)
      {
         currentChannel->channelType = WDTS_CHANNEL_RX_HIGH_PRI;
      }
#ifdef WLANDXE_TEST_CHANNEL_ENABLE
      else if(idx == WDTS_CHANNEL_H2H_TEST_TX)
      {
         currentChannel->channelType = WDTS_CHANNEL_H2H_TEST_TX;
      }
      else if(idx == WDTS_CHANNEL_H2H_TEST_RX)
      {
         currentChannel->channelType = WDTS_CHANNEL_H2H_TEST_RX;
      }
#endif 

      
      status = dxeChannelDefaultConfig(tempDxeCtrlBlk,
                                       currentChannel);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "WLANDXE_Open Channel Basic Configuration Fail for channel %d", idx);
         WLANDXE_Close(tempDxeCtrlBlk);
         return NULL;         
      }

      
      status = dxeCtrlBlkAlloc(tempDxeCtrlBlk, currentChannel);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "WLANDXE_Open Alloc DXE Control Block Fail for channel %d", idx);

         WLANDXE_Close(tempDxeCtrlBlk);
         return NULL;         
      }
      status = wpalMutexInit(&currentChannel->dxeChannelLock); 
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                  "WLANDXE_Open Lock Init Fail for channel %d", idx);
         WLANDXE_Close(tempDxeCtrlBlk);
         return NULL;
      }

      status = wpalTimerInit(&currentChannel->healthMonitorTimer,
                    dxeHealthMonitorTimeout,
                    (void *)currentChannel);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                  "WLANDXE_Open Health Monitor timer init fail %d", idx);
         WLANDXE_Close(tempDxeCtrlBlk);
         return NULL;
      }

      currentChannel->healthMonitorMsg = (wpt_msg *)wpalMemoryAllocate(sizeof(wpt_msg));
      if(NULL == currentChannel->healthMonitorMsg)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
                  "WLANDXE_Open Health Monitor MSG Alloc fail %d", idx);
         WLANDXE_Close(tempDxeCtrlBlk);
         return NULL;
      }
      wpalMemoryZero(currentChannel->healthMonitorMsg, sizeof(wpt_msg));
      currentChannel->healthMonitorMsg->callback = dxeTXHealthMonitor;
      currentChannel->healthMonitorMsg->pContext = (void *)currentChannel;

      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "WLANDXE_Open Channel %s Open Success", channelType[idx]);
   }

   
   tempDxeCtrlBlk->rxIsrMsg = (wpt_msg *)wpalMemoryAllocate(sizeof(wpt_msg));
   if(NULL == tempDxeCtrlBlk->rxIsrMsg)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Open Alloc RX ISR Fail");
      WLANDXE_Close(tempDxeCtrlBlk);
      return NULL;
   }
   wpalMemoryZero(tempDxeCtrlBlk->rxIsrMsg, sizeof(wpt_msg));
   tempDxeCtrlBlk->rxIsrMsg->callback = dxeRXEventHandler;
   tempDxeCtrlBlk->rxIsrMsg->pContext = (void *)tempDxeCtrlBlk;

   
   tempDxeCtrlBlk->txIsrMsg = (wpt_msg *)wpalMemoryAllocate(sizeof(wpt_msg));
   if(NULL == tempDxeCtrlBlk->txIsrMsg)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Open Alloc TX ISR Fail");
      WLANDXE_Close(tempDxeCtrlBlk);
      return NULL;
   }
   wpalMemoryZero(tempDxeCtrlBlk->txIsrMsg, sizeof(wpt_msg));
   tempDxeCtrlBlk->txIsrMsg->callback = dxeTXEventHandler;
   tempDxeCtrlBlk->txIsrMsg->pContext = (void *)tempDxeCtrlBlk;

   
   tempDxeCtrlBlk->rxPktAvailMsg = (wpt_msg *)wpalMemoryAllocate(sizeof(wpt_msg));
   if(NULL == tempDxeCtrlBlk->rxPktAvailMsg)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Open Alloc RX Packet Available Message Fail");
      WLANDXE_Close(tempDxeCtrlBlk);
      return NULL;
   }
   wpalMemoryZero(tempDxeCtrlBlk->rxPktAvailMsg, sizeof(wpt_msg));
   tempDxeCtrlBlk->rxPktAvailMsg->callback = dxeRXPacketAvailableEventHandler;
   tempDxeCtrlBlk->rxPktAvailMsg->pContext = (void *)tempDxeCtrlBlk;
   
   tempDxeCtrlBlk->freeRXPacket = NULL;
   tempDxeCtrlBlk->dxeCookie    = WLANDXE_CTXT_COOKIE;
   tempDxeCtrlBlk->rxIntDisabledByIMPS = eWLAN_PAL_FALSE;
   tempDxeCtrlBlk->txIntDisabledByIMPS = eWLAN_PAL_FALSE;
   tempDxeCtrlBlk->driverReloadInProcessing = eWLAN_PAL_FALSE;
   tempDxeCtrlBlk->smsmToggled              = eWLAN_PAL_FALSE;

   smsmInitState = wpalNotifySmsm(WPAL_SMSM_WLAN_TX_ENABLE,
                                  WPAL_SMSM_WLAN_TX_RINGS_EMPTY);
   if(0 != smsmInitState)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "SMSM Channel init fail %d", smsmInitState);
      for(idx = 0; idx < WDTS_CHANNEL_MAX; idx++)
      {
         dxeChannelClose(tempDxeCtrlBlk, &tempDxeCtrlBlk->dxeChannel[idx]);
      }
      wpalMemoryFree(tempDxeCtrlBlk->rxIsrMsg);
      wpalMemoryFree(tempDxeCtrlBlk->txIsrMsg);
      wpalMemoryFree(tempDxeCtrlBlk);
      return NULL;
   }

#ifdef WLAN_DXE_LOW_RESOURCE_TIMER
   wpalTimerInit(&tempDxeCtrlBlk->rxResourceAvailableTimer,
                 dxeRXResourceAvailableTimerExpHandler,
                 tempDxeCtrlBlk);
#endif

   wpalTimerInit(&tempDxeCtrlBlk->dxeSSRTimer,
                 dxeSSRTimerExpHandler, tempDxeCtrlBlk);

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
            "WLANDXE_Open Success");
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return (void *)tempDxeCtrlBlk;
}

wpt_status WLANDXE_ClientRegistration
(
   void                       *pDXEContext,
   WLANDXE_RxFrameReadyCbType  rxFrameReadyCB,
   WLANDXE_TxCompleteCbType    txCompleteCB,
   WLANDXE_LowResourceCbType   lowResourceCB,
   void                       *userContext
)
{
   wpt_status                 status  = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_CtrlBlkType       *dxeCtxt;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if(NULL == pDXEContext)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_ClientRegistration Invalid DXE CB");
      return eWLAN_PAL_STATUS_E_INVAL;   
   }

   if(NULL == rxFrameReadyCB)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_ClientRegistration Invalid RX READY CB");
      return eWLAN_PAL_STATUS_E_INVAL;   
   }

   if(NULL == txCompleteCB)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_ClientRegistration Invalid txCompleteCB");
      return eWLAN_PAL_STATUS_E_INVAL;   
   }

   if(NULL == lowResourceCB)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_ClientRegistration Invalid lowResourceCB");
      return eWLAN_PAL_STATUS_E_INVAL;   
   }

   if(NULL == userContext)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_ClientRegistration Invalid userContext");
      return eWLAN_PAL_STATUS_E_INVAL;   
   }

   dxeCtxt = (WLANDXE_CtrlBlkType *)pDXEContext;

   
   dxeCtxt->rxReadyCB     = rxFrameReadyCB;
   dxeCtxt->txCompCB      = txCompleteCB;
   dxeCtxt->lowResourceCB = lowResourceCB;
   dxeCtxt->clientCtxt    = userContext;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

wpt_status WLANDXE_Start
(
   void  *pDXEContext
)
{
   wpt_status                 status  = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                 idx;
   WLANDXE_CtrlBlkType       *dxeCtxt = NULL;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if(NULL == pDXEContext)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Start Invalid DXE CB");
      return eWLAN_PAL_STATUS_E_INVAL;   
   }
   dxeCtxt = (WLANDXE_CtrlBlkType *)pDXEContext;

   status = dxeEngineCoreStart(dxeCtxt);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Start DXE HW init Fail");
      return status;         
   }

   
   for(idx = 0; idx < WDTS_CHANNEL_MAX; idx++)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "WLANDXE_Start Channel %s Start", channelType[idx]);

      
      
      status = dxeDescAllocAndLink(tempDxeCtrlBlk, &dxeCtxt->dxeChannel[idx]);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "WLANDXE_Start Alloc DXE Descriptor Fail for channel %d", idx);
         return status;         
      }

      
      status = dxeChannelInitProgram(dxeCtxt,
                                     &dxeCtxt->dxeChannel[idx]);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "WLANDXE_Start %d Program DMA channel Fail", idx);
         return status;         
      }

      status = dxeChannelStart(dxeCtxt,
                               &dxeCtxt->dxeChannel[idx]);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "WLANDXE_Start %d Channel Start Fail", idx);
         return status;         
      }
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "WLANDXE_Start Channel %s Start Success", channelType[idx]);
   }

   
   
   status = wpalRegisterInterrupt(DXE_INTERRUPT_TX_COMPLE,
                                       dxeTXISR,
                                       dxeCtxt);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Start TX comp interrupt registration Fail");
      return status;         
   }

   
   status = wpalRegisterInterrupt(DXE_INTERRUPT_RX_READY,
                                       dxeRXISR,
                                       dxeCtxt);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Start RX Ready interrupt registration Fail");
      return status;         
   }

   
   
   status = wpalEnableInterrupt(DXE_INTERRUPT_RX_READY);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "dxeTXCompleteEventHandler Enable TX complete interrupt fail");
      return status;         
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

wpt_status WLANDXE_TxFrame
(
   void                *pDXEContext,
   wpt_packet          *pPacket,
   WDTS_ChannelType     channel
)
{
   wpt_status                 status         = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_ChannelCBType     *currentChannel = NULL;
   WLANDXE_CtrlBlkType       *dxeCtxt        = NULL;
   unsigned int              *lowThreshold   = NULL;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if(NULL == pDXEContext)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Start Invalid DXE CB");
      return eWLAN_PAL_STATUS_E_INVAL;   
   }

   if(NULL == pPacket)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Start Invalid pPacket");
      return eWLAN_PAL_STATUS_E_INVAL;   
   }

   if((WDTS_CHANNEL_MAX < channel) || (WDTS_CHANNEL_MAX == channel))
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Start Invalid channel");
      return eWLAN_PAL_STATUS_E_INVAL;   
   }

   dxeCtxt = (WLANDXE_CtrlBlkType *)pDXEContext;

   currentChannel = &dxeCtxt->dxeChannel[channel];
   

   status = wpalMutexAcquire(&currentChannel->dxeChannelLock);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_TxFrame Mutex Acquire fail");
      return status;
   }

   lowThreshold = currentChannel->channelType == WDTS_CHANNEL_TX_LOW_PRI?
      &(dxeCtxt->txCompInt.txLowResourceThreshold_LoPriCh):
      &(dxeCtxt->txCompInt.txLowResourceThreshold_HiPriCh);

   
   switch(dxeCtxt->txCompInt.txIntEnable)
   {
      
      case WLANDXE_TX_COMP_INT_LR_THRESHOLD:
         if((currentChannel->numFreeDesc <= *lowThreshold) &&
            (eWLAN_PAL_FALSE == dxeCtxt->txIntEnable))
         {
            dxeCtxt->txIntEnable = eWLAN_PAL_TRUE;
            dxeCtxt->lowResourceCB(dxeCtxt->clientCtxt,
                                   channel,
                                   eWLAN_PAL_FALSE);
         }
         break;

      
      case WLANDXE_TX_COMP_INT_PER_K_FRAMES:
         if(channel == WDTS_CHANNEL_TX_LOW_PRI)
         {
            currentChannel->numFrameBeforeInt++;
         }
         break;

      
      case WLANDXE_TX_COMP_INT_TIMER:
         break;
   }

   dxeCtxt->txCompletedFrames++;

   status = dxeTXPushFrame(currentChannel, pPacket);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_TxFrame TX Push Frame fail");
      status = wpalMutexRelease(&currentChannel->dxeChannelLock);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "WLANDXE_TxFrame Mutex Release fail");
      }
      return status;
   }

   
   if(currentChannel->numFreeDesc <= *lowThreshold)
   {
      dxeCtxt->lowResourceCB(dxeCtxt->clientCtxt,
                             channel,
                             eWLAN_PAL_FALSE);
      currentChannel->hitLowResource = eWLAN_PAL_TRUE;

      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_WARN,
               "%11s : Low Resource currentChannel->numRsvdDesc %d",
               channelType[currentChannel->channelType],
               currentChannel->numRsvdDesc);
      dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);
      dxeNotifySmsm(eWLAN_PAL_TRUE, eWLAN_PAL_FALSE);
      wpalTimerStart(&currentChannel->healthMonitorTimer,
                     T_WLANDXE_PERIODIC_HEALTH_M_TIME);
   }
   status = wpalMutexRelease(&currentChannel->dxeChannelLock);
   if(eWLAN_PAL_STATUS_SUCCESS != status)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_TxFrame Mutex Release fail");
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

wpt_status
WLANDXE_CompleteTX
(
  void* pContext,
  wpt_uint32 ucTxResReq
)
{
  wpt_status                status  = eWLAN_PAL_STATUS_SUCCESS;
  WLANDXE_CtrlBlkType      *dxeCtxt = (WLANDXE_CtrlBlkType *)(pContext);
  WLANDXE_ChannelCBType    *channelCb  = NULL;
  wpt_boolean               inLowRes;

  
  if( NULL == pContext )
  {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_CompleteTX invalid param");
      return eWLAN_PAL_STATUS_E_INVAL;
  }

  channelCb = &dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI];
  inLowRes  = channelCb->hitLowResource;

  if(WLANDXE_TX_LOW_RES_THRESHOLD < ucTxResReq)
  {
    
    dxeCtxt->txCompInt.txLowResourceThreshold_LoPriCh = ucTxResReq;

    if(eWLAN_PAL_FALSE == inLowRes)
    {
      
      dxeCtxt->lowResourceCB(dxeCtxt->clientCtxt,
                             WDTS_CHANNEL_TX_LOW_PRI,
                             eWLAN_PAL_FALSE);
      inLowRes = channelCb->hitLowResource = eWLAN_PAL_TRUE;
      wpalTimerStart(&channelCb->healthMonitorTimer,
                     T_WLANDXE_PERIODIC_HEALTH_M_TIME);
    }
  }

  
  dxeTXCompleteProcessing(dxeCtxt);

  if(0 < ucTxResReq)
  {
    
    if(WLANDXE_GetFreeTxDataResNumber(dxeCtxt) >= ucTxResReq)
    {
      if((eWLAN_PAL_FALSE == inLowRes) && 
         (eWLAN_PAL_FALSE == channelCb->hitLowResource))
      {
        dxeCtxt->lowResourceCB(dxeCtxt->clientCtxt,
                               WDTS_CHANNEL_TX_LOW_PRI,
                               eWLAN_PAL_FALSE);
        dxeCtxt->lowResourceCB(dxeCtxt->clientCtxt,
                               WDTS_CHANNEL_TX_LOW_PRI,
                               eWLAN_PAL_TRUE);
        channelCb->hitLowResource = eWLAN_PAL_FALSE;
      }
    }
    else
    {
      if(eWLAN_PAL_FALSE == channelCb->hitLowResource)
      {
        
        dxeCtxt->lowResourceCB(dxeCtxt->clientCtxt,
                             WDTS_CHANNEL_TX_LOW_PRI,
                             eWLAN_PAL_FALSE);
        channelCb->hitLowResource = eWLAN_PAL_TRUE;
        wpalTimerStart(&channelCb->healthMonitorTimer,
                       T_WLANDXE_PERIODIC_HEALTH_M_TIME);
      }
    }
  }
 
  return status; 
}

wpt_status WLANDXE_Stop
(
   void *pDXEContext
)
{
   wpt_status                 status = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                 idx;
   WLANDXE_CtrlBlkType       *dxeCtxt = NULL;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if(NULL == pDXEContext)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Stop Invalid DXE CB");
      return eWLAN_PAL_STATUS_E_INVAL;   
   }

   dxeCtxt = (WLANDXE_CtrlBlkType *)pDXEContext;
   for(idx = 0; idx < WDTS_CHANNEL_MAX; idx++)
   {
      if(VOS_TIMER_STATE_RUNNING == wpalTimerGetCurStatus(&dxeCtxt->dxeChannel[idx].healthMonitorTimer))
      {
         wpalTimerStop(&dxeCtxt->dxeChannel[idx].healthMonitorTimer);
      }

      status = dxeChannelStop(dxeCtxt, &dxeCtxt->dxeChannel[idx]);
      if(eWLAN_PAL_STATUS_SUCCESS != status)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "WLANDXE_Stop Channel %d Stop Fail", idx);
      }
   }

   
   wpalUnRegisterInterrupt(DXE_INTERRUPT_TX_COMPLE);
   wpalUnRegisterInterrupt(DXE_INTERRUPT_RX_READY);

#ifdef WLAN_DXE_LOW_RESOURCE_TIMER
   if(VOS_TIMER_STATE_STOPPED !=
      wpalTimerGetCurStatus(&dxeCtxt->rxResourceAvailableTimer))
   {
      wpalTimerStop(&dxeCtxt->rxResourceAvailableTimer);
   }
#endif

   if(VOS_TIMER_STATE_STOPPED !=
      wpalTimerGetCurStatus(&dxeCtxt->rxResourceAvailableTimer))
   {
      wpalTimerStop(&dxeCtxt->rxResourceAvailableTimer);
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

wpt_status WLANDXE_Close
(
   void *pDXEContext
)
{
   wpt_status               status = eWLAN_PAL_STATUS_SUCCESS;
   wpt_uint32                 idx;
   WLANDXE_CtrlBlkType       *dxeCtxt = NULL;
#ifdef WLANDXE_TEST_CHANNEL_ENABLE
   wpt_uint32                 sIdx;
   WLANDXE_ChannelCBType     *channel = NULL;
   WLANDXE_DescCtrlBlkType   *crntDescCB = NULL;
   WLANDXE_DescCtrlBlkType   *nextDescCB = NULL;
#endif 

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   if(NULL == pDXEContext)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "WLANDXE_Stop Invalid DXE CB");
      return eWLAN_PAL_STATUS_E_INVAL;   
   }

   dxeCtxt = (WLANDXE_CtrlBlkType *)pDXEContext;
#ifdef WLAN_DXE_LOW_RESOURCE_TIMER
   wpalTimerDelete(&dxeCtxt->rxResourceAvailableTimer);
#endif
   wpalTimerDelete(&dxeCtxt->dxeSSRTimer);
   for(idx = 0; idx < WDTS_CHANNEL_MAX; idx++)
   {
      wpalMutexDelete(&dxeCtxt->dxeChannel[idx].dxeChannelLock);
      wpalTimerDelete(&dxeCtxt->dxeChannel[idx].healthMonitorTimer);
      if(NULL != dxeCtxt->dxeChannel[idx].healthMonitorMsg)
      {
         wpalMemoryFree(dxeCtxt->dxeChannel[idx].healthMonitorMsg);
      }
      dxeChannelClose(dxeCtxt, &dxeCtxt->dxeChannel[idx]);
#ifdef WLANDXE_TEST_CHANNEL_ENABLE
      channel    = &dxeCtxt->dxeChannel[idx];
      crntDescCB = channel->headCtrlBlk;
      for(sIdx = 0; sIdx < channel->numDesc; sIdx++)
      {
         nextDescCB = (WLANDXE_DescCtrlBlkType *)crntDescCB->nextCtrlBlk;
         wpalMemoryFree((void *)crntDescCB);
         crntDescCB = nextDescCB;
         if(NULL == crntDescCB)
         {
            break;
         }
      }
#endif 
   }

   if(NULL != dxeCtxt->rxIsrMsg)
   {
      wpalMemoryFree(dxeCtxt->rxIsrMsg);
   }
   if(NULL != dxeCtxt->txIsrMsg)
   {
      wpalMemoryFree(dxeCtxt->txIsrMsg);
   }
   if(NULL != dxeCtxt->rxPktAvailMsg)
   {
      wpalMemoryFree(dxeCtxt->rxPktAvailMsg);
   }

   wpalMemoryFree(pDXEContext);

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

wpt_status WLANDXE_TriggerTX
(
   void *pDXEContext
)
{
   wpt_status               status = eWLAN_PAL_STATUS_SUCCESS;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return status;
}

void dxeTxThreadSetPowerStateEventHandler
(
    wpt_msg               *msgPtr
)
{
   wpt_msg                  *msgContent = (wpt_msg *)msgPtr;
   WLANDXE_CtrlBlkType      *dxeCtxt;
   wpt_status                status = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_PowerStateType    reqPowerState;
   wpt_int8                  i;
   WLANDXE_ChannelCBType     *channelEntry;
   wpt_log_data_stall_channel_type channelLog;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   dxeCtxt = (WLANDXE_CtrlBlkType *)(msgContent->pContext);
   reqPowerState = (WLANDXE_PowerStateType)msgContent->val;
   dxeCtxt->setPowerStateCb = (WLANDXE_SetPowerStateCbType)msgContent->ptr;

   switch(reqPowerState)
   {
      case WLANDXE_POWER_STATE_BMPS:
         if(WLANDXE_RIVA_POWER_STATE_ACTIVE == dxeCtxt->rivaPowerState)
         {
            
            
            
            dxeCtxt->rivaPowerState = WLANDXE_RIVA_POWER_STATE_BMPS_UNKNOWN;
            dxeCtxt->hostPowerState = reqPowerState;
         }
         else
         {
            status = eWLAN_PAL_STATUS_E_INVAL;
         }
         break;
      case WLANDXE_POWER_STATE_IMPS:
         if(WLANDXE_RIVA_POWER_STATE_ACTIVE == dxeCtxt->rivaPowerState)
         {

            for(i = WDTS_CHANNEL_TX_LOW_PRI; i < WDTS_CHANNEL_RX_LOW_PRI; i++)
            {
               channelEntry = &dxeCtxt->dxeChannel[i];
               if(channelEntry->tailCtrlBlk != channelEntry->headCtrlBlk)
               {
                  status = eWLAN_PAL_STATUS_E_FAILURE;
                  HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
                           "%11s : %s :TX Pending frame",
                           channelType[channelEntry->channelType], __func__);

                  dxeChannelMonitor("DXE_IMP_ERR", channelEntry, &channelLog);
                  dxeDescriptorDump(channelEntry,
                                    channelEntry->headCtrlBlk->linkedDesc, 0);
                  dxeChannelRegisterDump(channelEntry, "DXE_IMPS_ERR",
                                         &channelLog);
                  dxeChannelAllDescDump(channelEntry,
                                        channelEntry->channelType,
                                        &channelLog);
               }
            }

            if (eWLAN_PAL_STATUS_SUCCESS == status)
            {
               dxeCtxt->rivaPowerState = WLANDXE_RIVA_POWER_STATE_IMPS_UNKNOWN;
               dxeCtxt->hostPowerState = WLANDXE_POWER_STATE_IMPS;
            }
         }
         else
         {
            status = eWLAN_PAL_STATUS_E_INVAL;
         }
         break;
      case WLANDXE_POWER_STATE_FULL:
         if(WLANDXE_RIVA_POWER_STATE_BMPS_UNKNOWN == dxeCtxt->rivaPowerState)
         {
            dxeCtxt->rivaPowerState = WLANDXE_RIVA_POWER_STATE_ACTIVE;
         }
         dxeCtxt->hostPowerState = reqPowerState;
         dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);
         break;
      case WLANDXE_POWER_STATE_DOWN:
         WLANDXE_Stop((void *)dxeCtxt);         
         break;
      default:
         
         break;
   }

   if(WLANDXE_POWER_STATE_BMPS_PENDING != dxeCtxt->hostPowerState)
   {
      dxeCtxt->setPowerStateCb(status, 
                               dxeCtxt->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI].descBottomLocPhyAddr);
   }
   else
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
            "%s State of DXE is WLANDXE_POWER_STATE_BMPS_PENDING, so cannot proceed", __func__);
   }
   
   wpalMemoryFree(msgPtr);
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
   return;
}


void dxeRxThreadSetPowerStateEventHandler
(
    wpt_msg               *msgPtr
)
{
   wpt_status               status = eWLAN_PAL_STATUS_SUCCESS;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   
   msgPtr->callback = dxeTxThreadSetPowerStateEventHandler;
   status = wpalPostTxMsg(WDI_GET_PAL_CTX(),
                       msgPtr);
   if ( eWLAN_PAL_STATUS_SUCCESS != status )
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "Tx thread Set power state req serialize fail status=%d",
               status, 0, 0);
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);
}

wpt_status WLANDXE_SetPowerState
(
   void                    *pDXEContext,
   WDTS_PowerStateType      powerState,
   WDTS_SetPSCbType         cBack
)
{
   wpt_status               status = eWLAN_PAL_STATUS_SUCCESS;
   WLANDXE_CtrlBlkType     *pDxeCtrlBlk;
   WLANDXE_PowerStateType   hostPowerState;
   wpt_msg                 *rxCompMsg;
   wpt_msg                 *txDescReSyncMsg;

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);
   if(NULL == pDXEContext)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "NULL pDXEContext passed by caller", 0, 0, 0);
      return eWLAN_PAL_STATUS_E_FAILURE;
   }
   pDxeCtrlBlk = (WLANDXE_CtrlBlkType *)pDXEContext;

   switch(powerState)
   {
      case WDTS_POWER_STATE_FULL:
         if(WLANDXE_POWER_STATE_IMPS == pDxeCtrlBlk->hostPowerState)
         {
            txDescReSyncMsg = (wpt_msg *)wpalMemoryAllocate(sizeof(wpt_msg));
            if(NULL == txDescReSyncMsg)
            {
               HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                        "WLANDXE_SetPowerState, TX Resync MSG MEM alloc Fail");
            }
            else
            {
               txDescReSyncMsg->callback = dxeTXReSyncDesc;
               txDescReSyncMsg->pContext = pDxeCtrlBlk;
               status = wpalPostTxMsg(WDI_GET_PAL_CTX(),
                                      txDescReSyncMsg);
               if(eWLAN_PAL_STATUS_SUCCESS != status)
               {
                  HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                           "WLANDXE_SetPowerState, Post TX re-sync MSG fail");
               }
            }
         }
         hostPowerState = WLANDXE_POWER_STATE_FULL;
         break;
      case WDTS_POWER_STATE_BMPS:
         pDxeCtrlBlk->hostPowerState = WLANDXE_POWER_STATE_BMPS;
         hostPowerState = WLANDXE_POWER_STATE_BMPS;
         break;
      case WDTS_POWER_STATE_IMPS:
         hostPowerState = WLANDXE_POWER_STATE_IMPS;
         break;
      case WDTS_POWER_STATE_DOWN:
         pDxeCtrlBlk->hostPowerState = WLANDXE_POWER_STATE_DOWN;
         hostPowerState = WLANDXE_POWER_STATE_DOWN;
         break;
      default:
         hostPowerState = WLANDXE_POWER_STATE_MAX;
   }

   
   
   
   
   
   
   
   
   
   
   
   if ( cBack )
   {
      
      rxCompMsg          = (wpt_msg *)wpalMemoryAllocate(sizeof(wpt_msg));
      if(NULL == rxCompMsg)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "WLANDXE_SetPowerState, MSG MEM alloc Fail");
         return eWLAN_PAL_STATUS_E_RESOURCES;
      }

      
      rxCompMsg->callback = dxeRxThreadSetPowerStateEventHandler;
      rxCompMsg->pContext = pDxeCtrlBlk;
      rxCompMsg->val      = hostPowerState;
      rxCompMsg->ptr      = cBack;
      status = wpalPostRxMsg(WDI_GET_PAL_CTX(),
                          rxCompMsg);
      if ( eWLAN_PAL_STATUS_SUCCESS != status )
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "Rx thread Set power state req serialize fail status=%d",
                  status, 0, 0);
      }
   }
   else
   {
      if ( WLANDXE_POWER_STATE_FULL == hostPowerState )
      {
         if( WLANDXE_POWER_STATE_BMPS == pDxeCtrlBlk->hostPowerState )
         {
            dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);
         }
         else if( WLANDXE_POWER_STATE_IMPS == pDxeCtrlBlk->hostPowerState )
         {
            
            if(eWLAN_PAL_TRUE == pDxeCtrlBlk->rxIntDisabledByIMPS)
            {
               pDxeCtrlBlk->rxIntDisabledByIMPS = eWLAN_PAL_FALSE;
               
               status = wpalEnableInterrupt(DXE_INTERRUPT_RX_READY);
               if(eWLAN_PAL_STATUS_SUCCESS != status)
               {
                  HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                           "%s Enable RX ready interrupt fail", __func__);
                  return status;
               }
            }
            if(eWLAN_PAL_TRUE == pDxeCtrlBlk->txIntDisabledByIMPS)
            {
               pDxeCtrlBlk->txIntDisabledByIMPS = eWLAN_PAL_FALSE;
               pDxeCtrlBlk->txIntEnable =  eWLAN_PAL_TRUE;
               
               status = wpalEnableInterrupt(DXE_INTERRUPT_TX_COMPLE);
               if(eWLAN_PAL_STATUS_SUCCESS != status)
               {
                  HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                           "%s Enable TX comp interrupt fail", __func__);
                  return status;
               }
            }
         }
         pDxeCtrlBlk->hostPowerState = hostPowerState;
         pDxeCtrlBlk->rivaPowerState = WLANDXE_RIVA_POWER_STATE_ACTIVE;
      }
      else if ( hostPowerState == WLANDXE_POWER_STATE_BMPS )
      {
         pDxeCtrlBlk->hostPowerState = hostPowerState;
         pDxeCtrlBlk->rivaPowerState = WLANDXE_RIVA_POWER_STATE_BMPS_UNKNOWN;
      }
      else if ( hostPowerState == WLANDXE_POWER_STATE_IMPS )
      {
         pDxeCtrlBlk->hostPowerState = WLANDXE_POWER_STATE_IMPS;
      }
      else
      {
         HDXE_ASSERT(0);
      }
   }

   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Exit", __func__);

   return status;
}

wpt_uint32 WLANDXE_GetFreeTxDataResNumber
(
   void *pDXEContext
)
{
   HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_INFO_LOW,
            "%s Enter", __func__);

   if(NULL == pDXEContext)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "NULL parameter passed by caller", 0, 0, 0);
      return (0);
   }

   return 
      ((WLANDXE_CtrlBlkType *)pDXEContext)->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI].numFreeDesc;
}

void WLANDXE_ChannelDebug
(
   wpt_boolean displaySnapshot,
   wpt_uint8   debugFlags
)
{
   wpt_msg                  *channelDebugMsg;
   wpt_msg                  *txDescReSyncMsg ;
   wpt_uint32                regValue;
   wpt_status                status = eWLAN_PAL_STATUS_SUCCESS;

   
   if(displaySnapshot)
   {
      dxeNotifySmsm(eWLAN_PAL_FALSE, eWLAN_PAL_TRUE);
      dxeNotifySmsm(eWLAN_PAL_TRUE, eWLAN_PAL_FALSE);
      
      wpalSleep(10);
      wpalReadRegister(WLANDXE_BMU_AVAILABLE_BD_PDU, &regValue);
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_FATAL,
               "===== DXE Dump Start HPS %d, FWS %d, TX PFC %d, ABD %d =====",
               tempDxeCtrlBlk->hostPowerState, tempDxeCtrlBlk->rivaPowerState,
               tempDxeCtrlBlk->txCompletedFrames, regValue);

      wpalPacketStallUpdateInfo((wpt_uint32 *)&tempDxeCtrlBlk->rivaPowerState,
                                &regValue,
                                NULL,
                                0);

      channelDebugMsg = (wpt_msg *)wpalMemoryAllocate(sizeof(wpt_msg));
      if(NULL == channelDebugMsg)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "WLANDXE_ChannelDebug, MSG MEM alloc Fail");
         return ;
      }

      channelDebugMsg->callback = dxeRxThreadChannelDebugHandler;
      status = wpalPostRxMsg(WDI_GET_PAL_CTX(), channelDebugMsg);
      if ( eWLAN_PAL_STATUS_SUCCESS != status )
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "Tx thread Set power state req serialize fail status=%d",
                  status, 0, 0);
      }
   }

   if(debugFlags & WPAL_DEBUG_TX_DESC_RESYNC)
   {
      txDescReSyncMsg = (wpt_msg *)wpalMemoryAllocate(sizeof(wpt_msg));
      if(NULL == txDescReSyncMsg)
      {
         HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                  "%s: Resync MSG MEM alloc Fail",__func__);
      }
      else
      {
         txDescReSyncMsg->callback = dxeDebugTxDescReSync;
         txDescReSyncMsg->pContext = tempDxeCtrlBlk;
         status = wpalPostTxMsg(WDI_GET_PAL_CTX(),
                                txDescReSyncMsg);
         if(eWLAN_PAL_STATUS_SUCCESS != status)
         {
            HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
                     "%s: Post TX re-sync MSG fail",__func__);
         }
      }
   }

   if(debugFlags & WPAL_DEBUG_START_HEALTH_TIMER)
   {
      HDXE_MSG(eWLAN_MODULE_DAL_DATA, eWLAN_PAL_TRACE_LEVEL_ERROR,
               "DXE TX Stall detect");
      
      wpalTimerStart(&tempDxeCtrlBlk->dxeChannel[WDTS_CHANNEL_TX_LOW_PRI].healthMonitorTimer,
                     T_WLANDXE_PERIODIC_HEALTH_M_TIME);
   }
   return;
}
