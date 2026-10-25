/**
 * @file    Dcm_Dsp.c
 * @brief   Diagnostic Service Processing (DSP): UDS 0x10 and 0x3E.
 *
 * References:
 *   - AUTOSAR CP R25-11 SWS_Dcm 7.4.2.2  Service 0x10 [SWS_Dcm_00250] [SWS_Dcm_00307] [SWS_Dcm_00311]
 *   - AUTOSAR CP R25-11 SWS_Dcm 7.4.2.16 Service 0x3E [SWS_Dcm_00251]
 *   - ISO 14229-1:2013 9.2 DiagnosticSessionControl, 9.6 TesterPresent
 *
 * Sub-function support (NRC 0x12) and SPRMIB are already handled by DSD.
 * pMsgContext->reqData[0] is the sub-function with bit 7 masked.
 */
#include "Dcm_Internal.h"

/* ======================== Local constants ================================= */
/* DSC: request = SID + diagnosticSessionType -> reqDataLen = 1 */
#define DCM_DSP_DSC_REQ_DATA_LENGTH      ((Dcm_MsgLenType)1u)
/* DSC: response = sessionType + P2 (2 bytes) + P2* (2 bytes), Table 26 */
#define DCM_DSP_DSC_RES_DATA_LENGTH      ((Dcm_MsgLenType)5u)
/* P2*Server_max is reported with 10 ms resolution, Table 29 */
#define DCM_DSP_P2STAR_RESOLUTION_MS     ((uint32)10u)
#define DCM_DSP_BYTE_SHIFT               8u
#define DCM_DSP_BYTE_MASK                ((uint32)0xFFu)

/* TP: request = SID + zeroSubFunction -> reqDataLen = 1, Table 65 */
#define DCM_DSP_TP_REQ_DATA_LENGTH       ((Dcm_MsgLenType)1u)
#define DCM_DSP_TP_RES_DATA_LENGTH       ((Dcm_MsgLenType)1u)

/* Response byte positions inside resData (SID excluded) */
#define DCM_DSP_RES_POS_SUBFUNCTION      0u
#define DCM_DSP_DSC_RES_POS_P2_HIGH      1u
#define DCM_DSP_DSC_RES_POS_P2_LOW       2u
#define DCM_DSP_DSC_RES_POS_P2STAR_HIGH  3u
#define DCM_DSP_DSC_RES_POS_P2STAR_LOW   4u

/* ======================== Local data ====================================== */
typedef struct
{
    boolean         Pending;  /* session change waits for the confirmation */
    Dcm_SesCtrlType Session;
} Dcm_DspSessionChangeType;

static Dcm_DspSessionChangeType Dcm_DspSessionChange;

/* ======================== Local helpers =================================== */
static const Dcm_DspSessionRowCfgType* Dcm_DspFindSessionRow(Dcm_SesCtrlType SessionLevel)
{
    const Dcm_DspSessionRowCfgType* row = NULL_PTR;
    uint8 index;

    for (index = 0u; (index < DCM_CFG_NUM_SESSION_ROWS) && (row == NULL_PTR); index++)
    {
        if (Dcm_DspSessionRowCfg[index].SessionLevel == SessionLevel)
        {
            row = &Dcm_DspSessionRowCfg[index];
        }
    }
    return row;
}

static void Dcm_DspWriteUint16(Dcm_MsgType Dest, uint32 Value)
{
    Dest[0] = (uint8)((Value >> DCM_DSP_BYTE_SHIFT) & DCM_DSP_BYTE_MASK);
    Dest[1] = (uint8)(Value & DCM_DSP_BYTE_MASK);
}

static void Dcm_DspWriteSessionResponse(const Dcm_DspSessionRowCfgType* Row,
                                        Dcm_MsgContextType* pMsgContext)
{
    pMsgContext->resData[DCM_DSP_RES_POS_SUBFUNCTION] = Row->SessionLevel;
    Dcm_DspWriteUint16(&pMsgContext->resData[DCM_DSP_DSC_RES_POS_P2_HIGH],
                       Row->P2ServerMaxMs);
    Dcm_DspWriteUint16(&pMsgContext->resData[DCM_DSP_DSC_RES_POS_P2STAR_HIGH],
                       Row->P2StarServerMaxMs / DCM_DSP_P2STAR_RESOLUTION_MS);
    pMsgContext->resDataLen = DCM_DSP_DSC_RES_DATA_LENGTH;
}

/* ======================== Internal interface ============================== */
void DspInternal_Init(void)
{
    Dcm_DspSessionChange.Pending = FALSE;
    Dcm_DspSessionChange.Session = DCM_DEFAULT_SESSION;
}

Std_ReturnType DspInternal_DiagnosticSessionControl(Dcm_ExtendedOpStatusType OpStatus,
                                                    Dcm_MsgContextType* pMsgContext,
                                                    Dcm_NegativeResponseCodeType* ErrorCode)
{
    Std_ReturnType result = E_NOT_OK;
    const Dcm_DspSessionRowCfgType* row = NULL_PTR;

    (void)OpStatus; /* synchronous service: always DCM_INITIAL */

    if (pMsgContext->reqDataLen != DCM_DSP_DSC_REQ_DATA_LENGTH)
    {
        *ErrorCode = DCM_E_INCORRECTMESSAGELENGTHORINVALIDFORMAT;  /* ISO Table 30 */
    }
    else
    {
        row = Dcm_DspFindSessionRow(pMsgContext->reqData[0]);
        if (row == NULL_PTR)
        {
            *ErrorCode = DCM_E_SUBFUNCTIONNOTSUPPORTED;            /* [SWS_Dcm_00307] */
        }
        else if (pMsgContext->resMaxDataLen < DCM_DSP_DSC_RES_DATA_LENGTH)
        {
            *ErrorCode = DCM_E_RESPONSETOOLONG;                    /* [Inference] */
        }
        else
        {
            Dcm_DspWriteSessionResponse(row, pMsgContext);
            /* new session is set in the send confirmation [SWS_Dcm_00311] */
            Dcm_DspSessionChange.Pending = TRUE;
            Dcm_DspSessionChange.Session = row->SessionLevel;
            result = E_OK;
        }
    }
    return result;
}

Std_ReturnType DspInternal_TesterPresent(Dcm_ExtendedOpStatusType OpStatus,
                                         Dcm_MsgContextType* pMsgContext,
                                         Dcm_NegativeResponseCodeType* ErrorCode)
{
    Std_ReturnType result = E_NOT_OK;

    (void)OpStatus;

    if (pMsgContext->reqDataLen != DCM_DSP_TP_REQ_DATA_LENGTH)
    {
        *ErrorCode = DCM_E_INCORRECTMESSAGELENGTHORINVALIDFORMAT;  /* ISO Table 69 */
    }
    else
    {
        /* echo of bits 6-0 of the sub-function (ISO Table 68) */
        pMsgContext->resData[DCM_DSP_RES_POS_SUBFUNCTION] = pMsgContext->reqData[0];
        pMsgContext->resDataLen = DCM_DSP_TP_RES_DATA_LENGTH;
        result = E_OK;
    }
    return result;
}

/* Called for every finished request, also when the response is suppressed
 * [SWS_Dcm_00238] [SWS_Dcm_00240]. */
void DspInternal_DcmConfirmation(Dcm_IdContextType idContext,
                                 uint16 ConnectionId,
                                 Dcm_ConfirmationStatusType status)
{
    (void)idContext;
    (void)ConnectionId;

    /* Session switch only after a positive response was sent (or suppressed).
     * On a negative response the active session continues
     * (ISO 14229-1:2013 Table 25, defaultSession). */
    if ((Dcm_DspSessionChange.Pending == TRUE) && (status == DCM_RES_POS_OK))
    {
        DslInternal_SetSesCtrlType(Dcm_DspSessionChange.Session);
    }
    Dcm_DspSessionChange.Pending = FALSE;
}
