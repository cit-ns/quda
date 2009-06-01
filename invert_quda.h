#ifndef _INVERT_QUDA_H
#define _INVERT_QUDA_H

#include <enum_quda.h>

#ifdef __cplusplus
extern "C" {
#endif

  typedef struct QudaGaugeParam_s {

    int X;
    int Y;
    int Z;
    int T;

    float anisotropy;

    QudaGaugeFieldOrder gauge_order;

    QudaPrecision cpu_prec;
    QudaPrecision cuda_prec;

    QudaReconstructType reconstruct;
    QudaGaugeFixed gauge_fix;

    QudaTboundary t_boundary;

    int packed_size;
    float gaugeGiB;

  } QudaGaugeParam;

  typedef struct QudaInvertParam_s {
    
    float kappa;  
    QudaMassNormalization mass_normalization;

    QudaDslashType dslash_type;
    QudaInverterType inv_type;
    float tol;
    int iter;
    int maxiter;
    float reliable_delta; // reliable update tolerance

    QudaMatPCType matpc_type;
    QudaSolutionType solution_type;

    QudaPreserveSource preserve_source;

    QudaPrecision cpu_prec;
    QudaPrecision cuda_prec;
    QudaDiracFieldOrder dirac_order;

    float spinorGiB;
    float gflops;
    float secs;

  } QudaInvertParam;

  // Interface functions
  void initQuda(int dev);
  void loadGaugeQuda(void *h_gauge, QudaGaugeParam *param);
  void invertQuda(void *h_x, void *h_b, QudaInvertParam *param);

  void dslashQuda(void *h_out, void *h_in, QudaInvertParam *inv_param, int parity, int dagger);
  void MatPCQuda(void *h_out, void *h_in, QudaInvertParam *inv_param);
  void MatPCDagQuda(void *h_out, void *h_in, QudaInvertParam *inv_param);
  void MatPCDagMatPCQuda(void *h_out, void *h_in, QudaInvertParam *inv_param);

  void MatQuda(void *h_out, void *h_in, QudaInvertParam *inv_param);
  void MatDagQuda(void *h_out, void *h_in, QudaInvertParam *inv_param);

  void endQuda(void);

  void printGaugeParam(QudaGaugeParam *);
  void printInvertParam(QudaInvertParam *);

#ifdef __cplusplus
}
#endif

#endif // _INVERT_CUDA_H
