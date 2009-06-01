#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cuComplex.h>

#include <quda.h>
#include <util_quda.h>
#include <field_quda.h>

void invertBiCGstabCuda(ParitySpinor x, ParitySpinor source, FullGauge gaugeSloppy, 
			FullGauge gaugePrecise, ParitySpinor tmp, 
			QudaInvertParam *invert_param, DagType dag_type)
{
  int len = Nh*spinorSiteSize;
  Precision prec = QUDA_SINGLE_PRECISION;

  ParitySpinor r = allocateParitySpinor(prec);
  ParitySpinor p = allocateParitySpinor(prec);
  ParitySpinor v = allocateParitySpinor(prec);
  ParitySpinor t = allocateParitySpinor(prec);

  ParitySpinor y = allocateParitySpinor(prec);
  ParitySpinor b = allocateParitySpinor(prec);

  copyCuda((float *)b.spinor, (float *)source.spinor, len);
  copyCuda((float *)r.spinor, (float *)b.spinor, len);
  zeroCuda((float *)y.spinor, len);
  zeroCuda((float *)x.spinor, len);

  QudaSumFloat b2 = normCuda((float *)b.spinor, len);
  QudaSumFloat r2 = b2;
  QudaSumFloat stop = b2*invert_param->tol*invert_param->tol; // stopping condition of solver

  cuComplex rho = make_cuFloatComplex(1.0f, 0.0f);
  cuComplex rho0 = rho;
  cuComplex alpha = make_cuFloatComplex(1.0f, 0.0f);
  cuComplex omega = make_cuFloatComplex(1.0f, 0.0f);
  cuComplex beta;

  QudaSumComplex rv;
  cuComplex rho_rho0;
  cuComplex alpha_omega;
  cuComplex beta_omega;
  cuComplex one = make_cuFloatComplex(1.0f, 0.0f);

  QudaSumFloat3 rho_r2;
  QudaSumFloat3 omega_t2;

  QudaSumFloat rNorm = sqrt(r2);
  QudaSumFloat r0Norm = rNorm;
  QudaSumFloat maxrx = rNorm;
  QudaSumFloat maxrr = rNorm;
  QudaSumFloat delta = invert_param->reliable_delta;

  int k=0;
  int xUpdate = 0, rUpdate = 0;

  //printf("%d iterations, r2 = %e\n", k, r2);
  stopwatchStart();
  while (r2 > stop && k<invert_param->maxiter) {

    if (k==0) {
      rho = make_cuFloatComplex(r2, 0.0);
      copyCuda((float *)p.spinor, (float *)r.spinor, len);
    } else {
      alpha_omega = cuCdivf(alpha, omega);
      rho_rho0 = cuCdivf(rho, rho0);
      beta = cuCmulf(rho_rho0, alpha_omega);

      // p = r - beta*omega*v + beta*(p)
      beta_omega = cuCmulf(beta, omega); beta_omega.x *= -1.0f; beta_omega.y *= -1.0f;
      cxpaypbzCuda((float2*)r.spinor, beta_omega, (float2*)v.spinor, beta, (float2*)p.spinor, len/2); // 8
    }

    if (dag_type == QUDA_DAG_NO) 
      //rv = MatPCcDotWXCuda(v, gauge, p, invert_param->kappa, tmp, b, invert_param->matpc_type);
      MatPCCuda(v, gaugeSloppy, p, invert_param->kappa, tmp, invert_param->matpc_type);
    else 
      //rv = MatPCDagcDotWXCuda(v, gauge, p, invert_param->kappa, tmp, b, invert_param->matpc_type);
      MatPCDagCuda(v, gaugeSloppy, p, invert_param->kappa, tmp, invert_param->matpc_type);

    rv = cDotProductCuda((float2*)source.spinor, (float2*)v.spinor, len/2);
    cuComplex rv32 = make_cuFloatComplex((float)rv.x, (float)rv.y);
    alpha = cuCdivf(rho, rv32);

    // r -= alpha*v
    alpha.x *= -1.0f; alpha.y *= -1.0f;
    caxpyCuda(alpha, (float2*)v.spinor, (float2*)r.spinor, len/2); // 4
    alpha.x *= -1.0f; alpha.y *= -1.0f;

    if (dag_type == QUDA_DAG_NO) 
      MatPCCuda(t, gaugeSloppy, r, invert_param->kappa, tmp, invert_param->matpc_type);
    else  
      MatPCDagCuda(t, gaugeSloppy, r, invert_param->kappa, tmp, invert_param->matpc_type);

    // omega = (t, r) / (t, t)
    omega_t2 = cDotProductNormACuda((float2*)t.spinor, (float2*)r.spinor, len/2); // 6
    omega.x = omega_t2.x / omega_t2.z; omega.y = omega_t2.y/omega_t2.z;

    //x += alpha*p + omega*r, r -= omega*t, r2 = (r,r), rho = (r0, r)
    rho_r2 = caxpbypzYmbwcDotProductWYNormYCuda(alpha, (float2*)p.spinor, omega, (float2*)r.spinor, 
						(float2*)x.spinor, (float2*)t.spinor, (float2*)source.spinor, len/2);
    rho0 = rho; rho.x = rho_r2.x; rho.y = rho_r2.y; r2 = rho_r2.z;
    
    // reliable updates (ideally should be double precision)
    rNorm = sqrt(r2);
    if (rNorm > maxrx) maxrx = rNorm;
    if (rNorm > maxrr) maxrr = rNorm;
    int updateX = (rNorm < delta*r0Norm && r0Norm <= maxrx) ? 1 : 0;
    int updateR = ((rNorm < delta*maxrr && r0Norm <= maxrr) || updateX) ? 1 : 0;

    if (updateR) {
      QudaPrecision spinorPrec = invert_param->cuda_prec;
      invert_param -> cuda_prec = QUDA_SINGLE_PRECISION;

      if (dag_type == QUDA_DAG_NO) 
	MatPCCuda(t, gaugePrecise, x, invert_param->kappa, tmp, invert_param->matpc_type);
      else 
	MatPCDagCuda(t, gaugePrecise, x, invert_param->kappa, tmp, invert_param->matpc_type);

      invert_param -> cuda_prec = spinorPrec;

      copyCuda((float*)r.spinor, (float*)b.spinor, len);
      mxpyCuda((float*)t.spinor, (float*)r.spinor, len);
      r2 = normCuda((float*)r.spinor, len);
      rNorm = sqrt(r2);

      maxrr = rNorm;
      rUpdate++;

      if (updateX) {
	axpyCuda(1.0f, (float*)x.spinor, (float*)y.spinor, len);
	zeroCuda((float*)x.spinor, len);
	copyCuda((float*)b.spinor, (float*)r.spinor, len);
	r0Norm = rNorm;

	maxrx = rNorm;
	xUpdate++;
      }
      
    }
      

    k++;
    printf("%d iterations, r2 = %e, x2 = %e\n", k, r2, normCuda((float*)x.spinor, len));
  }
  axpyCuda(1.0f, (float*)y.spinor, (float*)x.spinor, len);

  invert_param->secs += stopwatchReadSeconds();

  //if (k==maxiters) printf("Exceeded maximum iterations %d\n", maxiters);

  printf("Residual updates = %d, Solution updates = %d\n", rUpdate, xUpdate);

  float gflops = (1.0e-9*Nh)*(2*(2*1320+48)*k + (32*k + 8*(k-1))*spinorSiteSize);
  gflops += 1.0e-9*Nh*rUpdate*((2*1320+48) + 3*spinorSiteSize);
  gflops += 1.0e-9*Nh*xUpdate*spinorSiteSize;
  //printf("%f gflops\n", k*gflops / stopwatchReadSeconds());
  invert_param->gflops += gflops;
  invert_param->iter += k;

#if 0
  // Calculate the true residual
  if (dag_type == QUDA_DAG_NO) 
    MatPCCuda(t.spinor, gauge, x.spinor, invert_param->kappa, tmp.spinor, invert_param->matpc_type);
  else 
    MatPCDagCuda(t.spinor, gauge, x.spinor, invert_param->kappa, tmp.spinor, invert_param->matpc_type);
  copyCuda((float *)r.spinor, (float *)b.spinor, len);
  mxpyCuda((float *)t.spinor, (float *)r.spinor, len);
  double true_res = normCuda((float *)r.spinor, len);
  
  printf("Converged after %d iterations, r2 = %e, true_r2 = %e\n", 
	 k, r2, true_res / b2);
#endif

  freeParitySpinor(b);
  freeParitySpinor(y);
  freeParitySpinor(r);
  freeParitySpinor(v);
  freeParitySpinor(t);
  freeParitySpinor(p);

  return;
}
