/*
  Memory layout
*/
#ifndef __SBR_RAM_H
#define __SBR_RAM_H


extern __thread float *PsBuf2;
extern __thread float PsBuf3[];
extern __thread float *PsBuf4;
extern __thread float *PsBuf5;

extern __thread  float sbr_envYBuffer[];
extern __thread  float sbr_QmfStatesAnalysis[];
extern __thread  float sbr_envRBuffer[];
extern __thread  float sbr_envIBuffer[];
extern __thread  float sbr_quotaMatrix[];
extern __thread  float sbr_thresholds[];
extern __thread  float sbr_transients[];
extern __thread  unsigned char sbr_freqBandTableLO[];
extern __thread  unsigned char sbr_freqBandTableHI[];
extern __thread  unsigned char sbr_v_k_master[];

extern __thread unsigned char  sbr_detectionVectors[];
extern __thread char           sbr_prevEnvelopeCompensation[];
extern __thread unsigned char  sbr_guideScfb[];
extern __thread unsigned char  sbr_guideVectorDetected[];
extern __thread float          sbr_toncorrBuff[];

#define PS_BUF4_SIZE (4*(QMF_TIME_SLOTS + QMF_BUFFER_MOVE) + 4*(NO_QMF_BANDS_IN_HYBRID + NO_QMF_BANDS_IN_HYBRID*QMF_BUFFER_MOVE))
#define PS_BUF5_SIZE (QMF_FILTER_LENGTH/2 + QMF_CHANNELS)

#endif

