project (whispermedialib)

add_library(whisper_imported_aacplusenc SHARED
  aac_ram.c
  aac_rom.c
  aacenc.c
  adj_thr.c
  band_nrg.c
  bit_cnt.c
  bitenc.c
  block_switch.c
  channel_map.c
  dyn_bits.c
  grp_data.c
  interface.c
  line_pe.c
  ms_stereo.c
  pre_echo_control.c
  psy_configuration.c
  psy_main.c
  qc_main.c
  quantize.c
  sf_estim.c
  spreading.c
  stat_bits.c
  stprepro.c
  tns.c
  tns_param.c
  transform.c
  bitbuffer.c
  cfftn.c
  transcendent.c
  iir32resample.c
  resampler.c
  bit_sbr.c
  code_env.c
  env_bit.c
  env_est.c
  fram_gen.c
  freq_sca.c
  hybrid.c
  invf_est.c
  mh_det.c
  nf_est.c
  ps_bitenc.c
  ps_enc.c
  qmf_enc.c
  sbr_main.c
  sbr_misc.c
  sbr_ram.c
  sbr_rom.c
  ton_corr.c
  tran_det.c
)

install (
  TARGETS whisper_imported_aacplusenc
  DESTINATION lib)
