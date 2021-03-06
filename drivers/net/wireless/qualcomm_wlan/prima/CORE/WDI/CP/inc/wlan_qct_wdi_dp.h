/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
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

#ifndef WLAN_QCT_WDI_DP_H
#define WLAN_QCT_WDI_DP_H




#include "wlan_qct_pal_type.h"
#include "wlan_qct_wdi_i.h"
#include "wlan_qct_wdi_bd.h"

 

#define WDI_TXBD_BDRATE_DEFAULT 0
#define WDI_TXBD_BDRATE_FIRST   1
#define WDI_TXBD_BDRATE_SECOND  2
#define WDI_TXBD_BDRATE_THIRD   3

#define WDI_FRAME_TYPE_MASK     0x30
#define WDI_FRAME_TYPE_OFFSET   0x4
#define WDI_FRAME_SUBTYPE_MASK  0x0F

#define WDI_TXBD_BD_SSN_FILL_HOST         0
#define WDI_TXBD_BD_SSN_FILL_DPU_NON_QOS  1
#define WDI_TXBD_BD_SSN_FILL_DPU_QOS      2

#define WDI_ACKPOLICY_ACK_REQUIRED        0
#define WDI_ACKPOLICY_ACK_NOTREQUIRED     1

#define WDI_BDRATE_BCDATA_FRAME           1
#define WDI_BDRATE_BCMGMT_FRAME           2
#define WDI_BDRATE_CTRL_FRAME             3
    
#define WDI_DEFAULT_UNICAST_ENABLED       1
#define WDI_RMF_DISABLED                  0
#define WDI_RMF_ENABLED                   1
#define WDI_NO_ENCRYPTION_DISABLED        0
#define WDI_NO_ENCRYPTION_ENABLED         1
    
#define WDI_RX_BD_ADDR3_SELF_IDX          0

#define WDI_TXCOMP_REQUESTED_MASK           0x1  
#define WDI_USE_SELF_STA_REQUESTED_MASK     0x2  
#define WDI_TX_NO_ENCRYPTION_MASK           0x4  
#if defined(LIBRA_WAPI_SUPPORT)
#define WDI_WAPI_STA_MASK            0x8  
#endif

#define WDI_TRIGGER_ENABLED_AC_MASK         0x10 
#define WDI_USE_NO_ACK_REQUESTED_MASK       0x20

#define WDI_USE_BD_RATE2_FOR_MANAGEMENT_FRAME 0x40 

#ifdef FEATURE_WLAN_TDLS
#define HAL_TDLS_PEER_STA_MASK              0x80 
#endif

#ifdef WLAN_FEATURE_RELIABLE_MCAST
#define WDI_RELIABLE_MCAST_REQUESTED_MASK 0x100
#endif

#define WDI_USE_BD_RATE_MASK              0x1000
#define WDI_USE_FW_IN_TX_PATH             0x200 

#define WDI_TX_BD_HEADER_SIZE        sizeof(WDI_TxBdType)

#define WDI_RX_BD_HEADER_SIZE        sizeof(WDI_RxBdType)

#define WDI_RX_BD_HEADER_OFFSET       0

#define WDI_DPU_FEEDBACK_OFFSET       1


#define WDI_MAC_MGMT_FRAME    0x0
#define WDI_MAC_CTRL_FRAME    0x1
#define WDI_MAC_DATA_FRAME    0x2

#define WDI_MAC_DATA_DATA                 0
#define WDI_MAC_DATA_DATA_ACK             1
#define WDI_MAC_DATA_DATA_POLL            2
#define WDI_MAC_DATA_DATA_ACK_POLL        3
#define WDI_MAC_DATA_NULL                 4
#define WDI_MAC_DATA_NULL_ACK             5
#define WDI_MAC_DATA_NULL_POLL            6
#define WDI_MAC_DATA_NULL_ACK_POLL        7
#define WDI_MAC_DATA_QOS_DATA             8
#define WDI_MAC_DATA_QOS_DATA_ACK         9
#define WDI_MAC_DATA_QOS_DATA_POLL        10
#define WDI_MAC_DATA_QOS_DATA_ACK_POLL    11
#define WDI_MAC_DATA_QOS_NULL             12
#define WDI_MAC_DATA_QOS_NULL_ACK         13
#define WDI_MAC_DATA_QOS_NULL_POLL        14
#define WDI_MAC_DATA_QOS_NULL_ACK_POLL    15

#define WDI_MAC_FRAME_SUBTYPE_START       0
#define WDI_MAC_FRAME_SUBTYPE_END         16

#define WDI_MAC_DATA_QOS_MASK             8
#define WDI_MAC_DATA_NULL_MASK            4
#define WDI_MAC_DATA_POLL_MASK            2
#define WDI_MAC_DATA_ACK_MASK             1


#define WDI_MAC_MGMT_ASSOC_REQ    0x0
#define WDI_MAC_MGMT_ASSOC_RSP    0x1
#define WDI_MAC_MGMT_REASSOC_REQ  0x2
#define WDI_MAC_MGMT_REASSOC_RSP  0x3
#define WDI_MAC_MGMT_PROBE_REQ    0x4
#define WDI_MAC_MGMT_PROBE_RSP    0x5
#define WDI_MAC_MGMT_BEACON       0x8
#define WDI_MAC_MGMT_ATIM         0x9
#define WDI_MAC_MGMT_DISASSOC     0xA
#define WDI_MAC_MGMT_AUTH         0xB
#define WDI_MAC_MGMT_DEAUTH       0xC
#define WDI_MAC_MGMT_ACTION       0xD
#define WDI_MAC_MGMT_RESERVED15   0xF


#define WDI_MAC_ACTION_SPECTRUM_MGMT    0
#define WDI_MAC_ACTION_QOS_MGMT         1
#define WDI_MAC_ACTION_DLP              2
#define WDI_MAC_ACTION_BLKACK           3
#define WDI_MAC_ACTION_HT               7
#define WDI_MAC_ACTION_WME              17


#define WDI_MAC_QOS_ADD_TS_REQ      0
#define WDI_MAC_QOS_ADD_TS_RSP      1
#define WDI_MAC_QOS_DEL_TS_REQ      2
#define WDI_MAC_QOS_SCHEDULE        3
#define WDI_MAC_QOS_DEF_BA_REQ      4
#define WDI_MAC_QOS_DEF_BA_RSP      5
#define WDI_MAC_QOS_DEL_BA_REQ      6
#define WDI_MAC_QOS_DEL_BA_RSP      7

#ifdef WLAN_PERF
#define WDI_TXBD_SIG_SERIAL_OFFSET        0   
#define WDI_TXBD_SIG_TID_OFFSET           8
#define WDI_TXBD_SIG_UCAST_DATA_OFFSET    9
#define WDI_TXBD_SIG_MACADDR_HASH_OFFSET  16
#define WDI_TXBD_SIG_MGMT_MAGIC           0xbdbdbdbd

#endif

#define WDI_RX_BD_GET_MPDU_H_OFFSET( _pvBDHeader )   (((WDI_RxBdType*)_pvBDHeader)->mpduHeaderOffset)

#define WDI_RX_BD_GET_MPDU_D_OFFSET( _pvBDHeader )   (((WDI_RxBdType*)_pvBDHeader)->mpduDataOffset)

#define WDI_RX_BD_GET_MPDU_LEN( _pvBDHeader )        (((WDI_RxBdType*)_pvBDHeader)->mpduLength)

#define WDI_RX_BD_GET_MPDU_H_LEN( _pvBDHeader )      (((WDI_RxBdType*)_pvBDHeader)->mpduHeaderLength)

#define WDI_RX_BD_GET_FT( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->ft)

#define WDI_RX_BD_GET_DPU_FEEDBACK( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->dpuFeedback)

#define WDI_RX_BD_GET_RX_CHANNEL( _pvBDHeader )         \
        (( (((WDI_RxBdType*)_pvBDHeader)->reserved0) << 4) | (((WDI_RxBdType*)_pvBDHeader)->rxChannel))

#define WDI_FRAME_TYPE_MASK     0x30
#define WDI_FRAME_TYPE_OFFSET   0x4
#define WDI_FRAME_SUBTYPE_MASK  0x0F

#define WDI_RX_BD_GET_SUBTYPE( _pvBDHeader )        ((((WDI_RxBdType*)_pvBDHeader)->frameTypeSubtype) & WDI_FRAME_SUBTYPE_MASK)

#define WDI_RX_BD_GET_TYPE( _pvBDHeader )     (((((WDI_RxBdType*)_pvBDHeader)->frameTypeSubtype) & WDI_FRAME_TYPE_MASK) >> WDI_FRAME_TYPE_OFFSET)

#define WDI_RX_BD_GET_RTSF( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->rtsf)

#define WDI_RX_BD_GET_BSF( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->bsf)

#define WDI_RX_BD_GET_SCAN( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->scanLearn)

#define WDI_RX_BD_GET_DPU_SIG( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->dpuSignature)

#define WDI_RX_BD_GET_NE( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->dpuNE)

#define WDI_RX_BD_GET_LLCR( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->llcr)

#define WDI_RX_BD_GET_TIMESTAMP( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->mclkRxTimestamp)

#define WDI_RX_BD_GET_RXPFLAGS( _pvBDHeader )        (((WDI_RxBdType*)_pvBDHeader)->rxpFlags)

#define WDI_RX_BD_GET_RATEINDEX( _pvBDHeader )        (((WDI_RxBdType*)_pvBDHeader)->rateIndex)

#define WDI_RX_BD_GET_AMSDU_SIZE( _pvBDHeader )        (((WDI_RxBdType*)_pvBDHeader)->totalMsduSize)

#define WDI_RX_BD_GET_LLC( _pvBDHeader )        (((WDI_RxBdType*)_pvBDHeader)->llc)

#define WDI_RX_BD_GET_TID( _pvBDHeader )        (((WDI_RxBdType*)_pvBDHeader)->tid)

#define WDI_RX_BD_GET_RFBAND( _pvBDHeader )        (((WDI_RxBdType*)_pvBDHeader)->rfBand)

#define WDI_RX_BD_GET_ASF( _pvBDHeader )        (((WDI_RxBdType*)_pvBDHeader)->asf)

#define WDI_RX_BD_GET_AEF( _pvBDHeader )        (((WDI_RxBdType*)_pvBDHeader)->aef)

#define WDI_RX_BD_GET_LSF( _pvBDHeader )        (((WDI_RxBdType*)_pvBDHeader)->lsf)

#define WDI_RX_BD_GET_ESF( _pvBDHeader )        (((WDI_RxBdType*)_pvBDHeader)->esf)

#define WDI_RX_BD_GET_STA_ID( _pvBDHeader )     (((WDI_RxBdType*)_pvBDHeader)->addr2Index)
#define WDI_RX_BD_GET_UB( _pvBDHeader )     (((WDI_RxBdType*)_pvBDHeader)->ub)
#define WDI_RX_BD_GET_ADDR3_IDX( _pvBDHeader )  (((WDI_RxBdType*)_pvBDHeader)->addr3Index)
#define WDI_RX_BD_GET_ADDR1_IDX( _pvBDHeader )  (((WDI_RxBdType*)_pvBDHeader)->addr1Index)

#define WDI_TX_BD_GET_TID( _pvBDHeader )   (((WDI_TxBdType*)_pvBDHeader)->tid)
#define WDI_TX_BD_GET_STA_ID( _pvBDHeader ) (((WDI_TxBdType*)_pvBDHeader)->staIndex)

#define WDI_RX_BD_GET_DPU_SIG( _pvBDHeader )     (((WDI_RxBdType*)_pvBDHeader)->dpuSignature)

#define WDI_RX_FC_BD_GET_STA_TX_DISABLED_BITMAP( _pvBDHeader )     (((WDI_FcRxBdType*)_pvBDHeader)->fcStaTxDisabledBitmap)
#define WDI_RX_FC_BD_GET_FC( _pvBDHeader )     (((WDI_FcRxBdType*)_pvBDHeader)->fc)
#define WDI_RX_FC_BD_GET_STA_VALID_MASK( _pvBDHeader )     (((WDI_FcRxBdType*)_pvBDHeader)->fcSTAValidMask)

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
#define WDI_RX_BD_GET_OFFLOADSCANLEARN( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->offloadScanLearn)
#define WDI_RX_BD_GET_ROAMCANDIDATEIND( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->roamCandidateInd)
#endif

#define WDI_RX_BD_GET_RSSI0( _pvBDHeader )  \
    (((((WDI_RxBdType*)_pvBDHeader)->phyStats0) >> 24) & 0xff)
#define WDI_RX_BD_GET_RSSI1( _pvBDHeader )  \
    (((((WDI_RxBdType*)_pvBDHeader)->phyStats0) >> 16) & 0xff)
#define WDI_RX_BD_GET_RSSI2( _pvBDHeader )  \
    (((((WDI_RxBdType*)_pvBDHeader)->phyStats0) >> 0) & 0xff)
#define WDI_RX_BD_GET_RSSI3( _pvBDHeader )  \
    ((((WDI_RxBdType*)_pvBDHeader)->phyStats0) & 0xff)

#define WDI_GET_RSSI_AVERAGE( _pvBDHeader ) \
    (((WDI_RX_BD_GET_RSSI0(_pvBDHeader)) + \
      (WDI_RX_BD_GET_RSSI1(_pvBDHeader)) + \
      (WDI_RX_BD_GET_RSSI2(_pvBDHeader)) + \
      (WDI_RX_BD_GET_RSSI3(_pvBDHeader))) / 4)

#define WDI_RX_BD_GET_SNR( _pvBDHeader )    \
    (((((WDI_RxBdType*)_pvBDHeader)->phyStats1) >> 24) & 0xff)

#define WDI_TX_BD_SET_MPDU_DATA_OFFSET( _bd, _off )      (((WDI_TxBdType*)_bd)->mpduDataOffset = _off)
 
#define WDI_TX_BD_SET_MPDU_HEADER_OFFSET( _bd, _off )    (((WDI_TxBdType*)_bd)->mpduHeaderOffset = _off)

#define WDI_TX_BD_SET_MPDU_HEADER_LEN( _bd, _len )       (((WDI_TxBdType*)_bd)->mpduHeaderLength = _len)

#define WDI_TX_BD_SET_MPDU_LEN( _bd, _len )              (((WDI_TxBdType*)_bd)->mpduLength = _len)

#define WDI_RX_BD_GET_BA_OPCODE(_pvBDHeader)        (((WDI_RxBdType*)_pvBDHeader)->reorderOpcode)

#define WDI_RX_BD_GET_BA_FI(_pvBDHeader)            (((WDI_RxBdType*)_pvBDHeader)->reorderFwdIdx)

#define WDI_RX_BD_GET_BA_SI(_pvBDHeader)            (((WDI_RxBdType*)_pvBDHeader)->reorderSlotIdx)

#define WDI_RX_BD_GET_BA_CSN(_pvBDHeader)           (((WDI_RxBdType*)_pvBDHeader)->currentPktSeqNo)

#define WDI_RX_BD_GET_BA_ESN(_pvBDHeader)           (((WDI_RxBdType*)_pvBDHeader)->expectedPktSeqNo)

#define WDI_RX_BD_GET_RXP_FLAGS(_pvBDHeader)        (((WDI_RxBdType*)_pvBDHeader)->rxpFlags)

#define WDI_RX_BD_GET_PMICMD_20TO23(_pvBDHeader)        (((WDI_RxBdType*)_pvBDHeader)->pmiCmd4to23[4])

#define WDI_RX_BD_GET_PMICMD_24TO25(_pvBDHeader)        (((WDI_RxBdType*)_pvBDHeader)->pmiCmd24to25)

#ifdef WLAN_FEATURE_11W
#define WDI_RX_BD_GET_RMF( _pvBDHeader )         (((WDI_RxBdType*)_pvBDHeader)->rmf)
#endif

#define WDI_RX_BD_ASF_SET               1 

#define WDI_RX_BD_FSF_SET               1

#define WDI_RX_BD_LSF_SET               1

#define WDI_RX_BD_AEF_SET               1

 
#define WDI_RX_BD_LLC_PRESENT           0 

#define WDI_RX_BD_FT_DONE                1 

 



wpt_uint8 
WDI_RxBD_GetFrameTypeSubType
(
  void*       _pvBDHeader, 
  wpt_uint16  usFrmCtrl
);



WDI_Status
WDI_FillTxBd
(
    WDI_ControlBlockType*  pWDICtx, 
    wpt_uint8              ucTypeSubtype, 
    void*                  pDestMacAddr,
    void*                  pAddr2,
    wpt_uint8*             pTid, 
    wpt_uint8              ucDisableFrmXtl, 
    void*                  pTxBd, 
    wpt_uint32             ucTxFlag,
    wpt_uint8              ucProtMgmtFrame,
    wpt_uint32             uTimeStamp,
    wpt_uint8              isEapol,
    wpt_uint8*             staIndex
);

void 
WDI_SwapRxBd
(
  wpt_uint8 *pBd
);

void 
WDI_SwapTxBd
(
  wpt_uint8 *pBd
);

void 
WDI_RxAmsduBdFix
(
  WDI_ControlBlockType*  pWDICtx, 
  void*                  pBDHeader
);

#ifdef WLAN_PERF
wpt_uint32 
WDI_TxBdFastFwd
(
  WDI_ControlBlockType*  pWDICtx,  
  wpt_uint8*             pDestMac, 
  wpt_uint8              ucTid, 
  wpt_uint8              ucUnicastDst,  
  void*                  pTxBd, 
  wpt_uint16             usMpduLength);
#endif 

WDI_Status 
WDI_DP_UtilsInit
(
  WDI_ControlBlockType*  pWDICtx
);

WDI_Status
WDI_DP_UtilsExit
( 
    WDI_ControlBlockType*  pWDICtx
);

#endif 

